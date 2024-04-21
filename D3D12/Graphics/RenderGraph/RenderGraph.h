#pragma once
#include "RenderGraphDefinitions.h"
#include "Graphics/RHI/Fence.h"
#include "Graphics/RHI/CommandContext.h"
#include "Blackboard.h"

#define RG_GRAPH_SCOPE(name, graph) RGGraphScope MACRO_CONCAT(rgScope_,__COUNTER__)(name, graph, __FILE__, __LINE__)

class RGGraph;
class RGPass;

// Flags assigned to a pass that can determine various things
enum class RGPassFlag : uint8
{
	None =		0,
	Raster =	1 << 0,		///< Raster pass
	Compute =	1 << 1,		///< Compute pass
	Copy =		1 << 2,		///< Pass that performs a copy resource operation. Does not play well with Raster/Compute passes
	NeverCull = 1 << 3,		///< Makes a pass never be culled when not referenced.
};
DECLARE_BITMASK_TYPE(RGPassFlag);

class RGPassResources
{
public:
	RGPassResources(const RGPass& pass)
		: m_Pass(pass)
	{}

	RGPassResources(const RGPassResources& other) = delete;
	RGPassResources& operator=(const RGPassResources& other) = delete;

	NO_DISCARD RenderPassInfo GetRenderPassInfo() const;

private:
	const RGPass& m_Pass;
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
	NO_DISCARD T* AllocateObject(Args&&... args)
	{
		using AllocatedType = std::conditional_t<std::is_trivial_v<T>, T, TAllocatedObject<T>>;
		void* pData = Allocate(sizeof(AllocatedType));
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

	NO_DISCARD const char* AllocateString(const char* pStr)
	{
		uint32 len = CString::StrLen(pStr);
		char* pAlloc = (char*)Allocate(len + 1);
		strcpy_s(pAlloc, len + 1, pStr);
		return pAlloc;
	}

	NO_DISCARD void* Allocate(uint64 size)
	{
		check(m_pCurrentOffset - m_pData + size < m_Size);
		void* pData = m_pCurrentOffset;
		m_pCurrentOffset += size;

		// For debugging allocations
#if 0
		E_LOG(Info, "Allocating %s (%s / %s - %.0f%%)",
			Math::PrettyPrintDataSize(size).c_str(),
			Math::PrettyPrintDataSize(GetSize()).c_str(),
			Math::PrettyPrintDataSize(GetCapacity()).c_str(),
			(float)GetSize() / GetCapacity() * 100.0f
		);
#endif

		return pData;
	}

	uint64 GetSize() const { return m_pCurrentOffset - m_pData; }
	uint64 GetCapacity() const { return m_Size; }

private:
	std::vector<AllocatedObject*> m_NonPODAllocations;
	uint64 m_Size;
	char* m_pData;
	char* m_pCurrentOffset;
};

struct RGEvent
{
	const char*		pName		= "";
	const char*		pFilePath	= nullptr;
	uint32			LineNumber	= 0;
};
using RGEventID = RGHandle<RGEvent, uint16>;

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
				(Lambda)(context, resources);
			else
				(Lambda)(context);
		}

		TLambda Lambda;
	};

public:
	friend class RGGraph;
	friend class RGPassResources;

	struct RenderTargetAccess
	{
		RGTexture*				pResource		= nullptr;
		RenderPassColorFlags	Flags			= RenderPassColorFlags::None;
		RGTexture*				pResolveTarget	= nullptr;
	};

	struct DepthStencilAccess
	{
		RGTexture*				pResource		= nullptr;
		RenderPassDepthFlags	Flags			= RenderPassDepthFlags::None;
		bool					Write;
	};

	RGPass(RGGraph& graph, RGGraphAllocator& allocator, const char* pName, RGPassFlag flags, RGPassID id)
		: Graph(graph), Allocator(allocator), pName(pName), ID(id), Flags(flags)
	{
	}

	RGPass(const RGPass& rhs) = delete;
	RGPass& operator=(const RGPass& rhs) = delete;

	template<typename ExecuteFn>
	RGPass& Bind(ExecuteFn&& callback)
	{
		static_assert(sizeof(ExecuteFn) < 1024, "The Execute callback exceeds the maximum size");
		check(!pExecuteCallback, "Pass is already bound! This may be unintentional");
		pExecuteCallback = Allocator.AllocateObject<RGPassCallback<ExecuteFn>>(std::forward<ExecuteFn&&>(callback));
		return *this;
	}

	RGPass& Write(Span<RGResource*> resources);
	RGPass& Read(Span<RGResource*> resources);
	RGPass& RenderTarget(RGTexture* pResource, RenderPassColorFlags flags = RenderPassColorFlags::None, RGTexture* pResolveTarget = nullptr);
	RGPass& DepthStencil(RGTexture* pResource, RenderPassDepthFlags flags = RenderPassDepthFlags::None);

	NO_DISCARD const char* GetName() const { return pName; }

private:
	struct ResourceAccess
	{
		RGResource* pResource;
		D3D12_RESOURCE_STATES Access;
	};

	void AddAccess(RGResource* pResource, D3D12_RESOURCE_STATES state);

	struct ResourceTransition
	{
		RGResource*				pResource;
		D3D12_RESOURCE_STATES	BeforeState;
		D3D12_RESOURCE_STATES	AfterState;
		uint32					SubResource;
	};

	const char*						pName;
	RGGraph&						Graph;
	RGGraphAllocator&				Allocator;
	RGPassID						ID;
	RGPassFlag						Flags;
	bool							IsCulled			= true;

	// Profiling
	std::vector<RGEventID>			EventsToStart;
	std::vector<RGEventID>			CPUEventsToStart;
	uint32							NumEventsToEnd		= 0;
	uint32							NumCPUEventsToEnd	= 0;

	std::vector<RenderTargetAccess> RenderTargets;
	DepthStencilAccess				DepthStencilTarget{};
	IRGPassCallback*				pExecuteCallback = nullptr;

	std::vector<ResourceTransition> Transitions;
	std::vector<ResourceAccess>		Accesses;
	std::vector<RGPassID>			PassDependencies;
};

class RGResourcePool : public DeviceObject
{
public:
	RGResourcePool(GraphicsDevice* pDevice)
		: DeviceObject(pDevice)
	{}

	NO_DISCARD Ref<Texture> Allocate(const char* pName, const TextureDesc& desc);
	NO_DISCARD Ref<Buffer> Allocate(const char* pName, const BufferDesc& desc);
	void Tick();

private:
	template<typename T>
	struct PooledResource
	{
		Ref<T> pResource;
		uint32 LastUsedFrame;
	};
	using PooledTexture = PooledResource<Texture>;
	using PooledBuffer = PooledResource<Buffer>;
	std::vector<PooledTexture> m_TexturePool;
	std::vector<PooledBuffer> m_BufferPool;
	uint32 m_FrameIndex = 0;
};

struct RGGraphOptions
{
	bool ResourceAliasing		= true;
	bool Jobify					= true;
	bool PassCulling			= true;
	bool StateTracking			= true;
	uint32 CommandlistGroupSize = 10;
};

class RGGraph
{
public:
	RGGraph(uint64 allocatorSize = 1024 * 128);
	~RGGraph();

	RGGraph(const RGGraph& other) = delete;
	RGGraph& operator=(const RGGraph& other) = delete;

	void Compile(RGResourcePool& resourcePool, const RGGraphOptions& options);

	void Execute(GraphicsDevice* pDevice);

	template<typename T, typename... Args>
	NO_DISCARD T* Allocate(Args&&... args)
	{
		return m_Allocator.AllocateObject<T>(std::forward<Args&&>(args)...);
	}

	RGPass& AddPass(const char* pName, RGPassFlag flags)
	{
		RGPass* pPass = Allocate<RGPass>(std::ref(*this), m_Allocator, m_Allocator.AllocateString(pName), flags, RGPassID((uint16)m_Passes.size()));

		for (RGEventID eventIndex : m_PendingEvents)
			pPass->EventsToStart.push_back(eventIndex);
		m_PendingEvents.clear();

		m_Passes.push_back(pPass);
		return *m_Passes.back();
	}

	NO_DISCARD RGTexture* Create(const char* pName, const TextureDesc& desc)
	{
		RGTexture* pResource = Allocate<RGTexture>(m_Allocator.AllocateString(pName), RGResourceID((uint16)m_Resources.size()), desc);
		m_Resources.emplace_back(pResource);
		return pResource;
	}

	RGBuffer* Create(const char* pName, const BufferDesc& desc)
	{
		RGBuffer* pResource = Allocate<RGBuffer>(m_Allocator.AllocateString(pName), RGResourceID((uint16)m_Resources.size()), desc);
		m_Resources.push_back(pResource);
		return pResource;
	}

	NO_DISCARD RGTexture* Import(Texture* pTexture)
	{
		check(pTexture);
		RGTexture* pResource = Allocate<RGTexture>(m_Allocator.AllocateString(pTexture->GetName()), RGResourceID((uint16)m_Resources.size()), pTexture->GetDesc(), pTexture);
		m_Resources.push_back(pResource);
		return pResource;
	}

	NO_DISCARD RGTexture* TryImport(Texture* pTexture, Texture* pFallback = nullptr)
	{
		if (pTexture)
			return Import(pTexture);
		return pFallback ? Import(pFallback) : nullptr;
	}

	NO_DISCARD RGBuffer* Import(Buffer* pBuffer)
	{
		check(pBuffer);
		RGBuffer* pResource = Allocate<RGBuffer>(m_Allocator.AllocateString(pBuffer->GetName()), RGResourceID((uint16)m_Resources.size()), pBuffer->GetDesc(), pBuffer);
		m_Resources.push_back(pResource);
		return pResource;
	}

	NO_DISCARD RGBuffer* TryImport(Buffer* pBuffer, Buffer* pFallback = nullptr)
	{
		if (pBuffer)
			return Import(pBuffer);
		return pFallback ? Import(pFallback) : nullptr;
	}

	void Export(RGTexture* pTexture, Ref<Texture>* pTarget, TextureFlag additionalFlags = TextureFlag::None);
	void Export(RGBuffer* pBuffer, Ref<Buffer>* pTarget, BufferFlag additionalFlags = BufferFlag::None);

	NO_DISCARD RGTexture* FindTexture(const char* pName) const
	{
		for (RGResource* pResource : m_Resources)
		{
			if (pResource->GetType() == RGResourceType::Texture && strcmp(pResource->GetName(), pName) == 0)
			{
				return (RGTexture*)pResource;
			}
		}
		return nullptr;
	}

	void DumpDebugGraph(const char* pFilePath) const;
	void DrawResourceTracker(bool& enabled) const;
	void DrawPassView(bool& enabled) const;
	
	void PushEvent(const char* pName, const char* pFilePath = "", uint32 lineNumber = 0);
	void PopEvent();

	RGBlackboard Blackboard;

private:
	RGEventID AddEvent(const char* pName, const char* pFilePath, uint32 lineNumber)
	{
		m_Events.push_back(RGEvent{ m_Allocator.AllocateString(pName), pFilePath, lineNumber });
		return RGEventID((uint16)(m_Events.size() - 1));
	}

	void ExecutePass(const RGPass* pPass, CommandContext& context) const;
	void PrepareResources(const RGPass* pPass, CommandContext& context) const;
	void DestroyData();

	bool								m_IsCompiled		= false;
	std::vector<RGEventID>				m_PendingEvents;
	std::vector<RGEvent>				m_Events;

	RGGraphAllocator					m_Allocator;

	std::vector<Span<const RGPass*>>	m_PassExecuteGroups;
	std::vector<RGPass*>				m_Passes;
	std::vector<RGResource*>			m_Resources;

	struct ExportedTexture
	{
		RGTexture*		pTexture;
		Ref<Texture>*	pTarget;
	};
	std::vector<ExportedTexture>		m_ExportTextures;

	struct ExportedBuffer
	{
		RGBuffer*		pBuffer;
		Ref<Buffer>*	pTarget;
	};
	std::vector<ExportedBuffer>			m_ExportBuffers;
};

class RGGraphScope
{
public:
	RGGraphScope(const char* pName, RGGraph& graph, const char* pFilePath = "", uint32 lineNumber = 0)
		: m_Graph(graph)
	{
		graph.PushEvent(pName, pFilePath, lineNumber);
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
	RGPass&		AddCopyPass(RGGraph& graph, RGResource* pSource, RGResource* pTarget);
	RGPass&		AddResolvePass(RGGraph& graph, RGTexture* pSource, RGTexture* pTarget);
	RGBuffer*	CreatePersistent(RGGraph& graph, const char* pName, const BufferDesc& bufferDesc, Ref<Buffer>* pStorageTarget, bool doExport);
	RGTexture*	CreatePersistent(RGGraph& graph, const char* pName, const TextureDesc& textureDesc, Ref<Texture>* pStorageTarget, bool doExport);
}
