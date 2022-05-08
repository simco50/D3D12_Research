#pragma once
#include "RenderGraphDefinitions.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/Buffer.h"
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

enum class RGResourceType
{
	Texture,
	Buffer,
};
template<typename T> struct RGResourceTypeTraits { };
template<> struct RGResourceTypeTraits<Texture> { constexpr static RGResourceType Type = RGResourceType::Texture; };
template<> struct RGResourceTypeTraits<Buffer> { constexpr static RGResourceType Type = RGResourceType::Buffer; };

struct RGResource
{
	RGResource(const char* pName, int id, const TextureDesc& desc, Texture* pResource = nullptr)
		: Name(pName), Id(id), IsImported(!!pResource), Type(RGResourceType::Texture), pResourceReference(pResource), TextureDesc(desc), pResource(pResource)
	{}

	RGResource(const char* pName, int id, const BufferDesc& desc, Buffer* pResource = nullptr)
		: Name(pName), Id(id), IsImported(!!pResource), Type(RGResourceType::Buffer), pResourceReference(pResource), BufferDesc(desc), pResource(pResource)
	{}

	void SetResource(RefCountPtr<GraphicsResource> resource)
	{
		pResourceReference = resource;
		pResource = resource;
	}

	void Release()
	{
		pResourceReference = nullptr;
		// pResource keeps a raw reference to use during execution
	}

	const char* Name;
	int Id;
	bool IsImported;
	bool IsExported = false;
	RGResourceType Type;
	RefCountPtr<GraphicsResource> pResourceReference;
	GraphicsResource* pResource = nullptr;

	template<typename T>
	T* GetRHI() const
	{
		checkf(Type == RGResourceTypeTraits<T>::Type, "Provided type does not match resource type");
		check(pResource);
		return static_cast<T*>(pResource);
	}

	union
	{
		TextureDesc TextureDesc;
		BufferDesc BufferDesc;
	};

	RGPass* pFirstAccess = nullptr;
	RGPass* pLastAccess = nullptr;
};

class RGPassResources
{
public:
	RGPassResources(RGGraph& graph, RGPass& pass)
		: m_Graph(graph), m_Pass(pass)
	{}

	RGPassResources(const RGPassResources& other) = delete;
	RGPassResources& operator=(const RGPassResources& other) = delete;

	template<typename T>
	T* Get(RGHandle<T> handle) const
	{
		return GetResource(handle)->GetRHI<T>();
	}

	GraphicsResource* Get(RGHandleT handle) const
	{
		return GetResource(handle)->pResource;
	}

	RenderPassInfo GetRenderPassInfo() const;

private:
	const RGResource* GetResource(RGHandleT handle) const;

	RGGraph& m_Graph;
	RGPass& m_Pass;
};

class RGPass
{
public:
	friend class RGGraph;
	friend class RGPassResources;

	struct RenderTargetAccess
	{
		RGHandle<Texture> Resource;
		RenderPassAccess Access;
	};

	struct DepthStencilAccess
	{
		RGHandle<Texture> Resource;
		RenderPassAccess Access;
		RenderPassAccess StencilAccess;
		bool Write;
	};

private:
	RGPass(RGGraph& graph, const char* pName, RGPassFlag flags, uint32 id)
		: Graph(graph), Flags(flags), ID(id)
	{
		strcpy_s(Name, pName);
	}

	RGPass(const RGPass& rhs) = delete;
	RGPass& operator=(const RGPass& rhs) = delete;

public:
	template<typename ExecuteFn>
	RGPass& Bind(ExecuteFn&& callback)
	{
		static_assert(sizeof(ExecuteFn) < 4096, "The Execute callback exceeds the maximum size");
		checkf(!ExecuteCallback.IsBound(), "Pass is already bound! This may be unintentional");
		ExecuteCallback.BindLambda(std::move(callback));
		return *this;
	}

	RGPass& Write(Span<RGHandleT*> resources);
	RGPass& Read(Span<RGHandleT> resources);
	RGPass& ReadWrite(Span<RGHandleT*> resources);
	RGPass& RenderTarget(RGHandle<Texture>& resource, RenderPassAccess access);
	RGPass& DepthStencil(RGHandle<Texture>& resource, RenderPassAccess depthAccess, bool write, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess);

private:
	struct RGAccess
	{
		D3D12_RESOURCE_STATES Access;
		RGHandleT Resource;
	};

	DECLARE_DELEGATE(ExecutePassDelegate, CommandContext& /*context*/, const RGPassResources& /*resources*/);
	char Name[128];
	RGGraph& Graph;
	uint32 ID;
	RGPassFlag Flags;
	bool IsCulled = true;

	std::vector<RGAccess> Accesses;
	std::vector<RGPass*> PassDependencies;
	std::vector<RenderTargetAccess> RenderTargets;
	DepthStencilAccess DepthStencilTarget;
	ExecutePassDelegate ExecuteCallback;
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
	class Allocator
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

		Allocator(uint64 size)
			: m_Size(size), m_pData(new char[size]), m_pCurrentOffset(m_pData)
		{}

		~Allocator()
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

public:
	RGGraph(GraphicsDevice* pDevice, RGResourcePool& resourcePool, uint64 allocatorSize = 0xFFFF);
	~RGGraph();

	RGGraph(const RGGraph& other) = delete;
	RGGraph& operator=(const RGGraph& other) = delete;

	void Compile();
	SyncPoint Execute();
	void DumpGraph(const char* pPath) const;
	RGHandleT MoveResource(RGHandleT From, RGHandleT To);

	RGPass& AddCopyPass(const char* pName, RGHandleT source, RGHandleT& target);

	template<typename T, typename... Args>
	T* Allocate(Args&&... args)
	{
		return m_Allocator.Allocate<T>(std::forward<Args&&>(args)...);
	}

	RGPass& AddPass(const char* pName, RGPassFlag flags)
	{
		RGPass* pPass = Allocate<RGPass>(std::ref(*this), pName, flags, (int)m_RenderPasses.size());
		m_RenderPasses.push_back(pPass);
		return *m_RenderPasses.back();
	}

	RGHandle<Texture> CreateTexture(const char* pName, const TextureDesc& desc)
	{
		RGResource* pResource = Allocate<RGResource>(pName, (int)m_Resources.size(), desc);
		m_Resources.push_back(pResource);
		return RGHandle<Texture>((uint32)m_Resources.size() - 1);
	}

	RGHandle<Buffer> CreateBuffer(const char* pName, const BufferDesc& desc)
	{
		RGResource* pResource = Allocate<RGResource>(pName, (int)m_Resources.size(), desc);
		m_Resources.push_back(pResource);
		return RGHandle<Buffer>((uint32)m_Resources.size() - 1);
	}

	RGHandle<Texture> ImportTexture(const char* pName, Texture* pTexture, Texture* pFallback = nullptr)
	{
		check(pTexture || pFallback);
		pTexture = pTexture ? pTexture : pFallback;
		RGResource* pResource = Allocate<RGResource>(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
		m_Resources.push_back(pResource);
		return RGHandle<Texture>((uint32)m_Resources.size() - 1);
	}

	RGHandle<Texture> TryImportTexture(const char* pName, Texture* pTexture)
	{
		return pTexture ? ImportTexture(pName, pTexture) : RGHandle<Texture>();
	}

	RGHandle<Buffer> ImportBuffer(const char* pName, Buffer* pBuffer, Buffer* pFallback = nullptr)
	{
		check(pBuffer || pFallback);
		pBuffer = pBuffer ? pBuffer : pFallback;
		RGResource* pResource = Allocate<RGResource>(pName, (int)m_Resources.size(), pBuffer->GetDesc(), pBuffer);
		m_Resources.push_back(pResource);
		return RGHandle<Buffer>((uint32)m_Resources.size() - 1);
	}

	RGHandle<Buffer> TryImportBuffer(const char* pName, Buffer* pBuffer)
	{
		return pBuffer ? ImportBuffer(pName, pBuffer) : RGHandle<Buffer>();
	}

	void ExportTexture(RGHandle<Texture> handle, RefCountPtr<Texture>* pTarget)
	{
		GetResource(handle)->IsExported = true;
		m_ExportTextures.push_back({ handle, pTarget });
	}

	void ExportBuffer(RGHandle<Buffer> handle, RefCountPtr<Buffer>* pTarget)
	{
		GetResource(handle)->IsExported = true;
		m_ExportBuffers.push_back({ handle, pTarget });
	}

	bool IsValidHandle(RGHandleT handle) const
	{
		return handle.IsValid() && handle.Index < (int)m_Resources.size();
	}

	RGResource* GetResource(RGHandleT handle) const
	{
		return m_Resources[handle.Index];
	}

	const TextureDesc& GetDesc(RGHandleT handle) const
	{
		RGResource* pResource = GetResource(handle);
		check(pResource->Type == RGResourceType::Texture);
		return pResource->TextureDesc;
	}

	void PushEvent(const char* pName);
	void PopEvent();

	RGBlackboard Blackboard;

private:
	void ExecutePass(RGPass* pPass, CommandContext& context);
	void PrepareResources(RGPass* pPass, CommandContext& context);
	void DestroyData();

	struct RGResourceAlias
	{
		RGHandleT From;
		RGHandleT To;
	};

	GraphicsDevice* m_pDevice;
	Allocator m_Allocator;
	SyncPoint m_LastSyncPoint;

	std::vector<RGResourceAlias> m_Aliases;
	std::vector<RGPass*> m_RenderPasses;
	std::vector<RGResource*> m_Resources;
	RGResourcePool& m_ResourcePool;

	template<typename T>
	struct ExportedResource
	{
		RGHandle<T> Handle;
		RefCountPtr<T>* pTarget;
	};
	std::vector<ExportedResource<Texture>> m_ExportTextures;
	std::vector<ExportedResource<Buffer>> m_ExportBuffers;
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
