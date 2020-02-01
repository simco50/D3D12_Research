#pragma once
#include "../Texture.h"
#include "../GraphicsBuffer.h"

#define RG_DEBUG 1

#ifndef RG_DEBUG
#define RG_DEBUG 0
#endif

#ifndef RG_ASSERT
#define RG_ASSERT(expression, msg) assert((expression) && #msg)
#endif

#ifndef RG_STATIC_ASSERT
#define RG_STATIC_ASSERT(expression, msg) static_assert(expression, msg)
#endif

class Graphics;
class CommandContext;

namespace RG
{
	class ResourceAllocator;
	class RenderGraph;
	class RenderPassBase;

	struct BufferDesc
	{
		int Size;
	};

	enum class ResourceType
	{
		None,
		Texture,
		Buffer,
	};

	class VirtualResourceBase
	{
	public:
		VirtualResourceBase(const char* pName, int id, bool isImported, ResourceType type, void* pResource)
			: m_Name(pName), m_Id(id), m_IsImported(isImported), m_pPhysicalResource(pResource), m_Type(type)
		{}
		const char* m_Name;
		int m_Id;
		bool m_IsImported;
		int m_Version = 0;

		//RenderGraph compile-time values
		int m_References = 0;
		RenderPassBase* pFirstPassUsage = nullptr;
		RenderPassBase* pLastPassUsage = nullptr;

		ResourceType m_Type;
		void* m_pPhysicalResource;
	};

	class TextureResource : public VirtualResourceBase
	{
	public:
		TextureResource(const char* pName, int id, const TextureDesc& desc, Texture* pTexture)
			: VirtualResourceBase(pName, id, pTexture != nullptr, ResourceType::Texture, pTexture), m_Desc(desc)
		{}
		const TextureDesc& GetDesc() const { return m_Desc; }
	private:
		TextureDesc m_Desc;
	};

	class BufferResource : public VirtualResourceBase
	{
	public:
		BufferResource(const char* pName, int id, const BufferDesc& desc, GraphicsBuffer* pBuffer)
			: VirtualResourceBase(pName, id, pBuffer != nullptr, ResourceType::Buffer, pBuffer), m_Desc(desc)
		{}
		const BufferDesc& GetDesc() const { return m_Desc; }
	private:
		BufferDesc m_Desc;
	};

	struct ResourceNode
	{
	public:
		ResourceNode(VirtualResourceBase* pResource)
			: m_pResource(pResource), m_Version(pResource->m_Version)
		{}

		VirtualResourceBase* m_pResource;
		int m_Version;

		//RenderGraph compile-time values
		RenderPassBase* m_pWriter = nullptr;
		int m_Reads = 0;
	};

	struct ResourceHandle
	{
		explicit ResourceHandle(int id = InvalidIndex)
			: Index(id)
		{}

		bool operator==(const ResourceHandle& other) const { return Index == other.Index; }
		bool operator!=(const ResourceHandle& other) const { return Index != other.Index; }

		constexpr static const int InvalidIndex = -1;

		inline void Invalidate() { Index = InvalidIndex; }
		inline bool IsValid() const { return Index != InvalidIndex; }

		int Index;
	};

	struct ResourceHandleMutable : public ResourceHandle
	{
		explicit ResourceHandleMutable(int id = InvalidIndex)
			: ResourceHandle(id)
		{}
	};

	class RenderPassBuilder
	{
	public:
		RenderPassBuilder(RenderGraph& renderGraph, RenderPassBase& pass)
			: m_RenderGraph(renderGraph),
			m_Pass(pass)
		{}

		RenderPassBuilder(const RenderPassBuilder& other) = delete;
		RenderPassBuilder& operator=(const RenderPassBuilder& other) = delete;

		ResourceHandle Read(const ResourceHandle& resource);
		ResourceHandleMutable Write(ResourceHandleMutable& resource);
		ResourceHandleMutable CreateTexture(const char* pName, const TextureDesc& desc);
		ResourceHandleMutable CreateBuffer(const char* pName, const BufferDesc& desc);
		void NeverCull();

	private:
		RenderPassBase& m_Pass;
		RenderGraph& m_RenderGraph;
	};

	class RenderPassResources
	{
	public:
		RenderPassResources(RenderGraph& graph, RenderPassBase& pass)
			: m_Graph(graph), m_Pass(pass)
		{}

		RenderPassResources(const RenderPassResources& other) = delete;
		RenderPassResources& operator=(const RenderPassResources& other) = delete;

		template<typename T>
		T* GetResource(ResourceHandle handle) const
		{
			const ResourceNode& node = m_Graph.GetResourceNode(handle);
			assert(node.m_pResource);
			return static_cast<T*>(node.m_pResource->m_pPhysicalResource);
		}

	private:
		RenderGraph& m_Graph;
		RenderPassBase& m_Pass;
	};

	class RenderPassBase
	{
	public:
		friend class RenderPassBuilder;
		friend class RenderGraph;

		RenderPassBase(RenderGraph& graph, const char* pName, int id)
			: m_Name(pName), m_RenderGraph(graph), m_Id(id)
		{}

		virtual ~RenderPassBase() = default;

		virtual void Execute(const RenderPassResources& resources, CommandContext& renderContext) = 0;
		const char* GetName() const { return m_Name; }

		bool ReadsFrom(ResourceHandle handle) const
		{
			return std::find(m_Reads.begin(), m_Reads.end(), handle) != m_Reads.end();
		}

		bool WritesTo(ResourceHandle handle) const
		{
			return std::find(m_Writes.begin(), m_Writes.end(), handle) != m_Writes.end();
		}

		void PrepareResources(ResourceAllocator* pAllocator);
		void ReleaseResources(ResourceAllocator* pAllocator);

	protected:
		const char* m_Name;
		std::vector<ResourceHandle> m_Reads;
		std::vector<ResourceHandle> m_Writes;
		RenderGraph& m_RenderGraph;
		bool m_NeverCull = false;
		int m_Id;

		//RenderGraph compile-time values
		int m_References = 0;
		std::vector<VirtualResourceBase*> m_ResourcesToCreate;
		std::vector<VirtualResourceBase*> m_ResourcesToDestroy;
	};

	template<typename PassData>
	class RenderPass : public RenderPassBase
	{
	public:
		RenderPass(RenderGraph& graph, const char* pName, int id)
			: RenderPassBase(graph, pName, id),
			m_PassData{}
		{}
		inline const PassData& GetData() const { return m_PassData; }
		inline PassData& GetData() { return m_PassData; }
	protected:
		PassData m_PassData;
	};

	template<typename PassData, typename ExecuteCallback>
	class LambdaRenderPass : public RenderPass<PassData>
	{
	public:
		LambdaRenderPass(RenderGraph& graph, const char* pName, int id, ExecuteCallback&& executeCallback)
			: RenderPass(graph, pName, id),
			m_ExecuteCallback(std::forward<ExecuteCallback>(executeCallback))
		{}
		virtual void Execute(const RenderPassResources& resources, CommandContext& renderContext) override
		{
			m_ExecuteCallback(renderContext, resources, m_PassData);
		}
	private:
		ExecuteCallback m_ExecuteCallback;
	};

	class RenderGraph
	{
	public:
		RenderGraph(ResourceAllocator* pAllocator);
		~RenderGraph();

		RenderGraph(const RenderGraph& other) = delete;
		RenderGraph& operator=(const RenderGraph& other) = delete;

		void Compile();
		int64 Execute(Graphics* pGraphics);
		void Present(ResourceHandle resource);
		void DumpGraphViz(const char* pPath) const;
		void DumpGraphMermaid(const char* pPath) const;
		ResourceHandle MoveResource(ResourceHandle From, ResourceHandle To);

		template<typename PassData, typename SetupCallback, typename ExecuteCallback>
		RenderPass<PassData>& AddCallbackPass(const char* pName, const SetupCallback& setupCallback, ExecuteCallback&& executeCallback)
		{
			RG_STATIC_ASSERT(sizeof(ExecuteCallback) < 1024, "The Execute callback exceeds the maximum size");
			RenderPass<PassData>* pPass = new LambdaRenderPass<PassData, ExecuteCallback>(*this, pName, (int)m_RenderPasses.size(), std::forward<ExecuteCallback>(executeCallback));
			RenderPassBuilder builder(*this, *pPass);
			setupCallback(builder, pPass->GetData());
			m_RenderPasses.push_back(pPass);
			return *pPass;
		}

		ResourceHandleMutable CreateTexture(const char* pName, const TextureDesc& desc)
		{
			VirtualResourceBase* pResource = new TextureResource(pName, (int)m_Resources.size(), desc, nullptr);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable CreateBuffer(const char* pName, const BufferDesc& desc)
		{
			VirtualResourceBase* pResource = new BufferResource(pName, (int)m_Resources.size(), desc, nullptr);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable ImportTexture(const char* pName, Texture* pTexture)
		{
			assert(pTexture);
			VirtualResourceBase* pResource = new TextureResource(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable ImportBuffer(const char* pName, GraphicsBuffer* pBuffer)
		{
			assert(pBuffer);
			BufferDesc desc{};
			VirtualResourceBase* pResource = new BufferResource(pName, (int)m_Resources.size(), desc, pBuffer);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable CreateResourceNode(VirtualResourceBase* pResource)
		{
			ResourceNode node(pResource);
			m_ResourceNodes.push_back(node);
			return ResourceHandleMutable((int)m_ResourceNodes.size() - 1);
		}

		bool IsValidHandle(ResourceHandle handle) const
		{
			return handle.IsValid() && handle.Index < (int)m_ResourceNodes.size();
		}

		const ResourceNode& GetResourceNode(ResourceHandle handle) const
		{
			RG_ASSERT(IsValidHandle(handle), "Invalid handle");
			return m_ResourceNodes[handle.Index];
		}

		VirtualResourceBase* GetResource(ResourceHandle handle) const
		{
			const ResourceNode& node = GetResourceNode(handle);
			return node.m_pResource;
		}

	private:
		struct ResourceAlias
		{
			ResourceHandle From;
			ResourceHandle To;
		};
		ResourceAllocator* m_pAllocator;

		std::vector<ResourceAlias> m_Aliases;
		std::vector<RenderPassBase*> m_RenderPasses;
		std::vector<VirtualResourceBase*> m_Resources;
		std::vector<ResourceNode> m_ResourceNodes;
	};
}