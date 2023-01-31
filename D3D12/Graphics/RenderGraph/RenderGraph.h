#pragma once
#include "RenderGraphDefinitions.h"
#include "Graphics/RHI/Fence.h"
#include "Graphics/RHI/CommandContext.h"
#include "Blackboard.h"

#define RG_GRAPH_SCOPE(name, graph) RGGraphScope MACRO_CONCAT(rgScope_,__COUNTER__)(name, graph)

class RGGraph;
class RGPass;

// Flags assigned to a pass that can determine various things
enum class RGPassFlag
{
	None =		0,
	// Raster pass
	Raster =	1 << 0,
	// Compute pass
	Compute =	1 << 1,
	// Pass that performs a copy resource operation. Does not play well with Raster/Compute passes
	Copy =		1 << 2,
	// Makes a pass never be culled when not referenced.
	NeverCull = 1 << 3,
	// Automatically begin/end render pass
	NoRenderPass = 1 << 4,
};
DECLARE_BITMASK_TYPE(RGPassFlag);

class RGPassResources
{
public:
	RGPassResources(RGPass& pass)
		: m_Pass(pass)
	{}

	RGPassResources(const RGPassResources& other) = delete;
	RGPassResources& operator=(const RGPassResources& other) = delete;

	RenderPassInfo GetRenderPassInfo() const;

private:
	RGPass& m_Pass;
};

class RGGraphAllocator
{
public:
	struct AllocatedObject
	{
		virtual ~AllocatedObject() = default;
	};

	template<typename T>
	struct TAllocatedObject : public AllocatedObject
	{
		template<typename... Args>
		TAllocatedObject(Args&&... args)
			: Object(std::forward<Args&&>(args)...)
		{}
		T Object;
	};

	RGGraphAllocator(uint64 size)
		: m_Size(size), m_pData(new char[size]), m_pCurrentOffset(m_pData)
	{}

	~RGGraphAllocator()
	{
		for (size_t i = 0; i < m_NonPODAllocations.size(); ++i)
		{
			m_NonPODAllocations[i]->~AllocatedObject();
		}
		delete[] m_pData;
	}

	template<typename T, typename ...Args>
	T* Allocate(Args&&... args)
	{
		using AllocatedType = std::conditional_t<std::is_trivial_v<T>, T, TAllocatedObject<T>>;
		check(m_pCurrentOffset - m_pData + sizeof(AllocatedType) < m_Size);
		void* pData = m_pCurrentOffset;
		m_pCurrentOffset += sizeof(AllocatedType);
		AllocatedType* pAllocation = new (pData) AllocatedType(std::forward<Args&&>(args)...);

		if constexpr (std::is_trivial_v<T>)
		{
			return pAllocation;
		}
		else
		{
			m_NonPODAllocations.push_back(pAllocation);
			return &pAllocation->Object;
		}
	}

private:
	std::vector<AllocatedObject*> m_NonPODAllocations;
	uint64 m_Size;
	char* m_pData;
	char* m_pCurrentOffset;
};

class RGPass
{
private:
	struct IRGPassCallback
	{
		virtual ~IRGPassCallback() = default;
		virtual void Execute(CommandContext& context, const RGPassResources& resources) = 0;
	};

	template<typename TLambda>
	struct RGPassCallback : public IRGPassCallback
	{
		constexpr static bool HasPassResources = std::is_invocable<TLambda, CommandContext&, const RGPassResources&>::value;

		RGPassCallback(TLambda&& lambda)
			: Lambda(std::forward<TLambda&&>(lambda))
		{}

		virtual void Execute(CommandContext& context, const RGPassResources& resources)
		{
			if constexpr (HasPassResources)
			{
				(Lambda)(context, resources);
			}
			else
			{
				(Lambda)(context);
			}
		}

		TLambda Lambda;
	};

public:
	friend class RGGraph;
	friend class RGPassResources;

	struct RenderTargetAccess
	{
		RGTexture* pResource = nullptr;
		RenderTargetLoadAction LoadAccess;
		RGTexture* pResolveTarget = nullptr;
	};

	struct DepthStencilAccess
	{
		RGTexture* pResource = nullptr;
		RenderTargetLoadAction LoadAccess;
		RenderTargetLoadAction StencilLoadAccess;
		bool Write;
	};

	RGPass(RGGraph& graph, RGGraphAllocator& allocator, const char* pName, RGPassFlag flags, uint32 id)
		: Graph(graph), Allocator(allocator), ID(id), Flags(flags)
	{
		strcpy_s(Name, pName);
	}

	RGPass(const RGPass& rhs) = delete;
	RGPass& operator=(const RGPass& rhs) = delete;

	template<typename ExecuteFn>
	RGPass& Bind(ExecuteFn&& callback)
	{
		static_assert(sizeof(ExecuteFn) < 1024, "The Execute callback exceeds the maximum size");
		checkf(!pExecuteCallback, "Pass is already bound! This may be unintentional");
		pExecuteCallback = Allocator.Allocate<RGPassCallback<ExecuteFn>>(std::forward<ExecuteFn&&>(callback));
		if constexpr (RGPassCallback<ExecuteFn>::HasPassResources)
		{
			Flags |= RGPassFlag::NoRenderPass;
		}
		return *this;
	}

	RGPass& Write(Span<RGResource*> resources, bool condition = true);
	RGPass& Read(Span<RGResource*> resources, bool condition = true);
	RGPass& RenderTarget(RGTexture* pResource, RenderTargetLoadAction access, RGTexture* pResolveTarget = nullptr);
	RGPass& DepthStencil(RGTexture* pResource, RenderTargetLoadAction depthAccess, bool write, RenderTargetLoadAction stencilAccess = RenderTargetLoadAction::NoAccess);

private:
	struct ResourceAccess
	{
		RGResource* pResource;
		D3D12_RESOURCE_STATES Access;
	};

	void AddAccess(RGResource* pResource, D3D12_RESOURCE_STATES state);

	char Name[128];
	RGGraph& Graph;
	RGGraphAllocator& Allocator;
	uint32 ID;
	RGPassFlag Flags;
	bool IsCulled = true;
	uint32 m_NumEventsToEnd = 0;
	std::vector<std::string> m_EventsToStart;

	std::vector<ResourceAccess> Accesses;
	std::vector<RGPass*> PassDependencies;
	std::vector<RenderTargetAccess> RenderTargets;
	DepthStencilAccess DepthStencilTarget{};
	IRGPassCallback* pExecuteCallback = nullptr;
};

class RGResourcePool : public GraphicsObject
{
public:
	RGResourcePool(GraphicsDevice* pDevice)
		: GraphicsObject(pDevice)
	{}

	RefCountPtr<Texture> Allocate(const char* pName, const TextureDesc& desc);
	RefCountPtr<Buffer> Allocate(const char* pName, const BufferDesc& desc);
	void Tick();

private:
	template<typename T>
	struct PooledResource
	{
		RefCountPtr<T> pResource;
		uint32 LastUsedFrame;
	};
	using PooledTexture = PooledResource<Texture>;
	using PooledBuffer = PooledResource<Buffer>;
	std::vector<PooledTexture> m_TexturePool;
	std::vector<PooledBuffer> m_BufferPool;
	uint32 m_FrameIndex = 0;
};

class RGGraph
{
public:
	RGGraph(RGResourcePool& resourcePool, uint64 allocatorSize = 0xFFFF);
	~RGGraph();

	RGGraph(const RGGraph& other) = delete;
	RGGraph& operator=(const RGGraph& other) = delete;

	void Compile();
	void Execute(CommandContext* pContext);
	void DumpGraph(const char* pPath) const;

	template<typename T, typename... Args>
	T* Allocate(Args&&... args)
	{
		return m_Allocator.Allocate<T>(std::forward<Args&&>(args)...);
	}

	RGPass& AddPass(const char* pName, RGPassFlag flags)
	{
		RGPass* pPass = Allocate<RGPass>(std::ref(*this), m_Allocator, pName, flags, (int)m_RenderPasses.size());

		for(const std::string& eventName : m_Events)
		{
			pPass->m_EventsToStart.push_back(eventName);
		}
		m_Events.clear();

		m_RenderPasses.push_back(pPass);
		return *m_RenderPasses.back();
	}

	RGTexture* Create(const char* pName, const TextureDesc& desc)
	{
		RGTexture* pResource = Allocate<RGTexture>(pName, (int)m_Resources.size(), desc);
		m_Resources.emplace_back(pResource);
		return pResource;
	}

	RGBuffer* Create(const char* pName, const BufferDesc& desc)
	{
		RGBuffer* pResource = Allocate<RGBuffer>(pName, (int)m_Resources.size(), desc);
		m_Resources.push_back(pResource);
		return pResource;
	}

	RGTexture* Import(Texture* pTexture)
	{
		check(pTexture);
		RGTexture* pResource = Allocate<RGTexture>(pTexture->GetName().c_str(), (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
		m_Resources.push_back(pResource);
		return pResource;
	}

	RGTexture* TryImport(Texture* pTexture)
	{
		return pTexture ? Import(pTexture) : nullptr;
	}

	RGBuffer* Import(Buffer* pBuffer)
	{
		check(pBuffer);
		RGBuffer* pResource = Allocate<RGBuffer>(pBuffer->GetName().c_str(), (int)m_Resources.size(), pBuffer->GetDesc(), pBuffer);
		m_Resources.push_back(pResource);
		return pResource;
	}

	RGBuffer* TryImport(Buffer* pBuffer)
	{
		return pBuffer ? Import(pBuffer) : nullptr;
	}

	void Export(RGTexture* pTexture, RefCountPtr<Texture>* pTarget);
	void Export(RGBuffer* pBuffer, RefCountPtr<Buffer>* pTarget);

	RGTexture* FindTexture(const char* pName) const
	{
		for (RGResource* pResource : m_Resources)
		{
			if (pResource->Type == RGResourceType::Texture && strcmp(pResource->GetName(), pName) == 0)
			{
				return (RGTexture*)pResource;
			}
		}
		return nullptr;
	}

	void PushEvent(const char* pName);
	void PopEvent();

	RGBlackboard Blackboard;

private:
	void ExecutePass(RGPass* pPass, CommandContext& context);
	void PrepareResources(RGPass* pPass, CommandContext& context);
	void DestroyData();

	std::vector<std::string> m_Events;

	RGGraphAllocator m_Allocator;
	SyncPoint m_LastSyncPoint;

	std::vector<RGPass*> m_RenderPasses;
	std::vector<RGResource*> m_Resources;
	RGResourcePool& m_ResourcePool;

	struct ExportedTexture
	{
		RGTexture* pTexture;
		RefCountPtr<Texture>* pTarget;
	};
	std::vector<ExportedTexture> m_ExportTextures;

	struct ExportedBuffer
	{
		RGBuffer* pBuffer;
		RefCountPtr<Buffer>* pTarget;
	};
	std::vector<ExportedBuffer> m_ExportBuffers;
};

class RGGraphScope
{
public:
	RGGraphScope(const char* pName, RGGraph& graph)
		: m_Graph(graph)
	{
		graph.PushEvent(pName);
	}
	~RGGraphScope()
	{
		m_Graph.PopEvent();
	}
private:
	RGGraph& m_Graph;
};

namespace RGUtils
{
	RGPass& AddCopyPass(RGGraph& graph, RGResource* pSource, RGResource* pTarget);
	RGPass& AddResolvePass(RGGraph& graph, RGTexture* pSource, RGTexture* pTarget);
	RGBuffer* CreatePersistent(RGGraph& graph, const char* pName, const BufferDesc& bufferDesc, RefCountPtr<Buffer>* pStorageTarget, bool doExport);
	RGTexture* CreatePersistent(RGGraph& graph, const char* pName, const TextureDesc& textureDesc, RefCountPtr<Texture>* pStorageTarget, bool doExport);
}
