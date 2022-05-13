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
	// Makes the pass invisible to profiling. Useful for adding debug markers
	Invisible = 1 << 3,
	// Makes a pass never be culled when not referenced.
	NeverCull = 1 << 4,
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
		using AllocatedType = std::conditional_t<std::is_pod_v<T>, T, TAllocatedObject<T>>;
		check(m_pCurrentOffset - m_pData + sizeof(AllocatedType) < m_Size);
		void* pData = m_pCurrentOffset;
		m_pCurrentOffset += sizeof(AllocatedType);
		AllocatedType* pAllocation = new (pData) AllocatedType(std::forward<Args&&>(args)...);

		if constexpr (std::is_pod_v<T>)
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
		RGPassCallback(TLambda&& lambda)
			: Lambda(std::forward<TLambda&&>(lambda))
		{}

		virtual void Execute(CommandContext& context, const RGPassResources& resources)
		{
			(Lambda)(context, resources);
		}

		TLambda Lambda;
	};

public:
	friend class RGGraph;
	friend class RGPassResources;

	struct RenderTargetAccess
	{
		RGTexture* pResource = nullptr;
		RenderPassAccess Access;
		RGTexture* pResolveTarget = nullptr;
	};

	struct DepthStencilAccess
	{
		RGTexture* pResource = nullptr;
		RenderPassAccess Access;
		RenderPassAccess StencilAccess;
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
		return *this;
	}

	RGPass& Write(Span<RGResource*> resources);
	RGPass& Read(Span<RGResource*> resources);
	RGPass& RenderTarget(RGTexture* pResource, RenderPassAccess access);
	RGPass& RenderTarget(RGTexture* pResource, RenderTargetLoadAction loadAction, RGTexture* pResolveTarget);
	RGPass& DepthStencil(RGTexture* pResource, RenderPassAccess depthAccess, bool write, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess);

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
	RGGraph(GraphicsDevice* pDevice, RGResourcePool& resourcePool, uint64 allocatorSize = 0xFFFF);
	~RGGraph();

	RGGraph(const RGGraph& other) = delete;
	RGGraph& operator=(const RGGraph& other) = delete;

	void Compile();
	SyncPoint Execute();
	void DumpGraph(const char* pPath) const;

	RGPass& AddCopyPass(const char* pName, RGResource* source, RGResource* target);

	template<typename T, typename... Args>
	T* Allocate(Args&&... args)
	{
		return m_Allocator.Allocate<T>(std::forward<Args&&>(args)...);
	}

	RGPass& AddPass(const char* pName, RGPassFlag flags)
	{
		RGPass* pPass = Allocate<RGPass>(std::ref(*this), m_Allocator, pName, flags, (int)m_RenderPasses.size());
		m_RenderPasses.push_back(pPass);
		return *m_RenderPasses.back();
	}

	RGTexture* CreateTexture(const char* pName, const TextureDesc& desc)
	{
		RGTexture* pResource = Allocate<RGTexture>(pName, (int)m_Resources.size(), desc);
		m_Resources.emplace_back(pResource);
		return pResource;
	}

	RGBuffer* CreateBuffer(const char* pName, const BufferDesc& desc)
	{
		RGBuffer* pResource = Allocate<RGBuffer>(pName, (int)m_Resources.size(), desc);
		m_Resources.push_back(pResource);
		return pResource;
	}

	RGTexture* ImportTexture(const char* pName, Texture* pTexture, Texture* pFallback = nullptr)
	{
		check(pTexture || pFallback);
		pTexture = pTexture ? pTexture : pFallback;
		RGTexture* pResource = Allocate<RGTexture>(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
		m_Resources.push_back(pResource);
		return pResource;
	}

	RGTexture* TryImportTexture(const char* pName, Texture* pTexture)
	{
		return pTexture ? ImportTexture(pName, pTexture) : nullptr;
	}

	RGBuffer* ImportBuffer(const char* pName, Buffer* pBuffer, Buffer* pFallback = nullptr)
	{
		check(pBuffer || pFallback);
		pBuffer = pBuffer ? pBuffer : pFallback;
		RGBuffer* pResource = Allocate<RGBuffer>(pName, (int)m_Resources.size(), pBuffer->GetDesc(), pBuffer);
		m_Resources.push_back(pResource);
		return pResource;
	}

	RGBuffer* TryImportBuffer(const char* pName, Buffer* pBuffer)
	{
		return pBuffer ? ImportBuffer(pName, pBuffer) : nullptr;
	}

	void ExportTexture(RGTexture* pTexture , RefCountPtr<Texture>* pTarget)
	{
		pTexture->IsExported = true;
		m_ExportTextures.push_back({ pTexture, pTarget });
	}

	void ExportBuffer(RGBuffer* pBuffer, RefCountPtr<Buffer>* pTarget)
	{
		pBuffer->IsExported = true;
		m_ExportBuffers.push_back({ pBuffer, pTarget });
	}

	void PushEvent(const char* pName);
	void PopEvent();

	RGBlackboard Blackboard;

private:
	void ExecutePass(RGPass* pPass, CommandContext& context);
	void PrepareResources(RGPass* pPass, CommandContext& context);
	void DestroyData();

	GraphicsDevice* m_pDevice;
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
