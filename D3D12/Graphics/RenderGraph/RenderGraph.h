#pragma once
#include "../Texture.h"

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
	struct Buffer
	{
		struct Descriptor
		{
			int Size;
		};
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

	class ResourceBase
	{
	public:
		ResourceBase(const std::string& name, int id, bool isImported)
			: m_Name(name), m_Id(id), m_IsImported(isImported)
		{
		}

		void Create()
		{
		}

		void Destroy()
		{
		}

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
	class Resource : public ResourceBase
	{
	public:
		using Descriptor = typename T::Descriptor;

		Resource(const Descriptor& desc, const std::string& name, int id, bool isImported)
			: ResourceBase(name, id, isImported), m_Descriptor(desc)
		{}

		Resource(const std::string& name, int id, T* pResource)
			: ResourceBase(name, id, true), m_pResource(pResource)
		{}

		T* GetResource() const { return m_pResource; }

	private:
		T* m_pResource = nullptr;
		Descriptor m_Descriptor;
	};

	struct ResourceNode
	{
	public:
		ResourceNode(ResourceBase* pResource)
			: m_pResource(pResource), m_Version(pResource->m_Version)
		{
		}

		ResourceBase* m_pResource;
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
		ResourceHandleMutable CreateTexture(const std::string& name, const Texture::Descriptor& desc);
		ResourceHandleMutable CreateBuffer(const std::string& name, const Buffer::Descriptor& desc);
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
			return static_cast<Resource<T>*>(GetResourceInternal(handle))->GetResource();
		}

	private:
		ResourceBase* GetResourceInternal(ResourceHandle handle) const;
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
		std::vector<ResourceBase*> m_ResourcesToCreate;
		std::vector<ResourceBase*> m_ResourcesToDestroy;
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

		template<typename T>
		ResourceHandleMutable CreateResource(const std::string& name, const typename T::Descriptor& desc)
		{
			ResourceBase* pResource = new Resource<T>(desc, name, (int)m_Resources.size(), false);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		template<typename T>
		ResourceHandleMutable ImportResource(const std::string& name, T* pTexture)
		{
			ResourceBase* pResource = new Resource<T>(name, (int)m_Resources.size(), pTexture);
			m_Resources.push_back(pResource);
			return CreateResourceNode(pResource);
		}

		template<typename T>
		T* ExtractResource(ResourceHandle handle)
		{
			const ResourceNode& node = GetResourceNode(handle);
			return node.m_pResource;
		}

		ResourceHandleMutable CreateResourceNode(ResourceBase* pResource)
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

		ResourceBase* GetResource(ResourceHandle handle) const
		{
			const ResourceNode& node = GetResourceNode(handle);
			return node.m_pResource;
		}

	private:
		constexpr static const int MAX_EXECUTE_CAPTURE_SIZE = 1 << 15;

		struct ResourceAlias
		{
			ResourceHandle From;
			ResourceHandle To;
		};

		std::vector<ResourceAlias> m_Aliases;
		std::vector<RenderPassBase*> m_RenderPasses;
		std::vector<ResourceBase*> m_Resources;
		std::vector<ResourceNode> m_ResourceNodes;
	};
}