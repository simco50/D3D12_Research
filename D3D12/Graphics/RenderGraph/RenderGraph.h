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
	struct BufferDesc
	{
		int Size;
	};

	class Blackboard
	{
#define RG_BLACKBOARD_DATA(clazz) constexpr static const char* Type() { return #clazz; }
	public:
		Blackboard()
		{}

		Blackboard(const Blackboard& other) = delete;
		Blackboard& operator=(const Blackboard& other) = delete;

		template<typename T>
		T& Add()
		{
			RG_ASSERT(m_DataMap.find(T::Type()) == m_DataMap.end(), "Data type already exists in blackboard");
			T* pData = new T();
			m_DataMap[T::Type()] = pData;
			return *pData;
		}

		template<typename T>
		T& Get()
		{
			void* pData = GetData(T::Type());
			RG_ASSERT(pData, "Data for given type does not exist in blackboard");
			return *static_cast<T*>(pData);
		}

		template<typename T>
		const T& Get() const
		{
			void* pData = GetData(T::Type());
			RG_ASSERT(pData, "Data for given type does not exist in blackboard");
			return *static_cast<T*>(pData);
		}

		Blackboard& Branch()
		{
			m_Children.emplace_back(std::make_unique<Blackboard>());
			Blackboard& b = *m_Children.back();
			b.m_pParent = this;
			return b;
		}

	private:
		void* GetData(const std::string& name)
		{
			auto it = m_DataMap.find(name);
			if (it != m_DataMap.end())
			{
				return it->second;
			}
			if (m_pParent)
			{
				return m_pParent->GetData(name);
			}
			return nullptr;
		}

		std::map<std::string, void*> m_DataMap;
		std::vector<std::unique_ptr<Blackboard>> m_Children;
		Blackboard* m_pParent = nullptr;
	};

	class RenderPassBase;

	class VirtualResourceBase
	{
	public:
		VirtualResourceBase(const std::string& name, int id, bool isImported)
			: m_Name(name), m_Id(id), m_IsImported(isImported)
		{
		}

		virtual void Create() = 0;
		virtual void Destroy() = 0;

		std::string m_Name;
		int m_Id;
		bool m_IsImported;
		int m_Version = 0;

		//RenderGraph compile-time values
		int m_References = 0;
		RenderPassBase* pFirstPassUsage = nullptr;
		RenderPassBase* pLastPassUsage = nullptr;
	};
	
	template<typename T>
	class VirtualResource : public VirtualResourceBase
	{
	public:
		VirtualResource(const std::string& name, int id)
			: VirtualResourceBase(name, id, false), m_pResource(nullptr)
		{}

		VirtualResource(const std::string& name, int id, T* pResource)
			: VirtualResourceBase(name, id, true), m_pResource(pResource)
		{}

		T* GetResource() const { return m_pResource; }

	protected:
		T* m_pResource;
	};

	class TextureResource : public VirtualResource<Texture>
	{
	public:
		TextureResource(const std::string& name, int id, const TextureDesc& desc)
			: VirtualResource(name, id), m_Desc(desc)
		{}
		TextureResource(const std::string& name, int id, Texture* pTexture)
			: VirtualResource(name, id, pTexture), m_Desc(pTexture->GetDesc())
		{}
		virtual void Create() override {}
		virtual void Destroy() override {}

		const TextureDesc& GetDesc() const { return m_Desc; }

	private:
		TextureDesc m_Desc;
	};

	class BufferResource : public VirtualResource<GraphicsBuffer>
	{
	public:
		BufferResource(const std::string& name, int id, const BufferDesc& desc)
			: VirtualResource(name, id), m_Desc(desc)
		{}
		BufferResource(const std::string& name, int id, GraphicsBuffer* pBuffer)
			: VirtualResource(name, id, pBuffer), m_Desc{}
		{}
		virtual void Create() override {}
		virtual void Destroy() override {}

		const BufferDesc& GetDesc() const { return m_Desc; }

	private:
		BufferDesc m_Desc;
	};

	struct ResourceNode
	{
	public:
		ResourceNode(VirtualResourceBase* pResource)
			: m_pResource(pResource), m_Version(pResource->m_Version)
		{
		}

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

		int Index; //Index into the ResourceNodes array of the RenderGraph
	};

	struct ResourceHandleMutable : public ResourceHandle
	{
		explicit ResourceHandleMutable(int id = InvalidIndex)
			: ResourceHandle(id)
		{}
	};

	class RenderGraph;

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
		ResourceHandleMutable CreateTexture(const std::string& name, const TextureDesc& desc);
		ResourceHandleMutable CreateBuffer(const std::string& name, const BufferDesc& desc);
		const TextureDesc& GetTextureDesc(const ResourceHandle& handle) const;
		const BufferDesc& GetBufferDesc(const ResourceHandle& handle) const;
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
		{
		}

		RenderPassResources(const RenderPassResources& other) = delete;
		RenderPassResources& operator=(const RenderPassResources& other) = delete;

		template<typename T>
		T* GetResource(ResourceHandle handle) const
		{
			const ResourceNode& node = m_Graph.GetResourceNode(handle);
			assert(node.m_pResource);
			return static_cast<VirtualResource<T>*>(node.m_pResource)->GetResource();
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

		RenderPassBase(RenderGraph& graph, const std::string& name, int id)
			: m_Name(name), m_RenderGraph(graph), m_Id(id)
		{}

		virtual ~RenderPassBase()
		{
		}

		virtual void Execute(const RenderPassResources& resources, CommandContext& renderContext) = 0;

		const std::string& GetName() const
		{
			return m_Name;
		}

		bool ReadsFrom(ResourceHandle handle)
		{
			for (const ResourceHandle& read : m_Reads)
			{
				if (read == handle)
				{
					return true;
				}
			}
			return false;
		}

		bool WritesTo(ResourceHandle handle)
		{
			for (const ResourceHandle& write : m_Writes)
			{
				if (write == handle)
				{
					return true;
				}
			}
			return false;
		}

	protected:
		std::string m_Name;
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
		RenderPass(RenderGraph& graph, const std::string& name, int id)
			: RenderPassBase(graph, name, id),
			m_PassData{}
		{}

		const PassData& GetData() const
		{
			return m_PassData;
		}

		PassData& GetData()
		{
			return m_PassData;
		}

	protected:
		PassData m_PassData;
	};

	template<typename PassData, typename ExecuteCallback>
	class LambdaRenderPass : public RenderPass<PassData>
	{
	public:
		LambdaRenderPass(RenderGraph& graph, const std::string& name, int id, ExecuteCallback&& executeCallback)
			: RenderPass(graph, name, id),
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
		RenderGraph();
		~RenderGraph();

		RenderGraph(const RenderGraph& other) = delete;
		RenderGraph& operator=(const RenderGraph& other) = delete;

		void Compile();
		int64 Execute(Graphics* pGraphics);
		void Present(ResourceHandle resource);
		void DumpGraphViz(const char* pPath) const;

		ResourceHandle MoveResource(ResourceHandle From, ResourceHandle To);

		template<typename PassData, typename SetupCallback, typename ExecuteCallback>
		RenderPass<PassData>& AddCallbackPass(const std::string& name, const SetupCallback& setupCallback, ExecuteCallback&& executeCallback)
		{
			RG_STATIC_ASSERT(sizeof(ExecuteCallback) < MAX_EXECUTE_CAPTURE_SIZE, "The Execute callback exceeds the maximum size");
			RenderPass<PassData>* pPass = new LambdaRenderPass<PassData, ExecuteCallback>(*this, name, (int)m_RenderPasses.size(), std::forward<ExecuteCallback>(executeCallback));
			RenderPassBuilder builder(*this, *pPass);
			setupCallback(builder, pPass->GetData());
			m_RenderPasses.push_back(pPass);
			return *pPass;
		}

		ResourceHandleMutable CreateTexture(const std::string& name, const TextureDesc& desc)
		{
			VirtualResourceBase* pResource = new TextureResource(name, (int)m_Resources.size(), desc);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable CreateBuffer(const std::string& name, const BufferDesc& desc)
		{
			VirtualResourceBase* pResource = new BufferResource(name, (int)m_Resources.size(), desc);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable ImportTexture(const std::string& name, Texture* pTexture)
		{
			assert(pTexture);
			VirtualResourceBase* pResource = new TextureResource(name, (int)m_Resources.size(), pTexture);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		ResourceHandleMutable ImportBuffer(const std::string& name, GraphicsBuffer* pBuffer)
		{
			assert(pBuffer);
			VirtualResourceBase* pResource = new BufferResource(name, (int)m_Resources.size(), pBuffer);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		template<typename T>
		T* ExtractResource(ResourceHandle handle)
		{
			const ResourceNode& node = GetResourceNode(handle);
			return node.m_pResource;
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
		constexpr static const int MAX_EXECUTE_CAPTURE_SIZE = 1 << 10;

		struct ResourceAlias
		{
			ResourceHandle From;
			ResourceHandle To;
		};

		std::vector<ResourceAlias> m_Aliases;
		std::vector<RenderPassBase*> m_RenderPasses;
		std::vector<VirtualResourceBase*> m_Resources;
		std::vector<ResourceNode> m_ResourceNodes;
	};
}