#pragma once
#include "RenderGraphDefinitions.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/Fence.h"
#include "Graphics/RHI/CommandContext.h"

#define RG_GRAPH_SCOPE(name, graph) RGGraphScope MACRO_CONCAT(rgScope_,__COUNTER__)(name, graph)

class RGGraph;
class RGPass;

enum class RGResourceType
{
	Texture,
	Buffer,
};

struct RGResource
{
	RGResource(const char* pName, int id, bool isImported, RGResourceType type, void* pResource)
		: m_Name(pName), m_Id(id), m_IsImported(isImported), m_Version(0), m_Type(type), m_pPhysicalResource(pResource), m_References(0)
	{}

	const char* m_Name;
	int m_Id;
	bool m_IsImported;
	int m_Version;
	RGResourceType m_Type;
	void* m_pPhysicalResource;

	//RenderGraph compile-time values
	int m_References;
};

struct RGTexture : public RGResource
{
	RGTexture(const char* pName, int id, const TextureDesc& desc, Texture* pTexture)
		: RGResource(pName, id, pTexture != nullptr, RGResourceType::Texture, pTexture), Desc(desc)
	{}
	TextureDesc Desc;
};

struct RGBuffer : public RGResource
{
	RGBuffer(const char* pName, int id, const BufferDesc& desc, Buffer* pBuffer)
		: RGResource(pName, id, pBuffer != nullptr, RGResourceType::Buffer, pBuffer), Desc(desc)
	{}
	BufferDesc Desc;
};

struct RGNode
{
	explicit RGNode(RGResource* pResource)
		: pResource(pResource), Version(pResource->m_Version)
	{}

	RGResource* pResource;
	int Version;
	//RenderGraph compile-time values
	RGPass* pWriter = nullptr;
	int Reads = 0;
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
	T* Get(RGResourceHandle handle) const
	{
		return static_cast<T*>(GetResource(handle));
	}

private:
	void* GetResource(RGResourceHandle handle) const;

	RGGraph& m_Graph;
	RGPass& m_Pass;
};

class RGPass
{
public:
	struct RenderTargetAccess
	{
		RGResourceHandle Resource;
		RenderPassAccess Access;
	};

	struct DepthStencilAccess
	{
		RGResourceHandle Resource;
		RenderPassAccess Access;
		RenderPassAccess StencilAccess;
		bool Write;
	};

	RGPass(RGGraph& graph, const char* pName, int id)
		: Graph(graph), pName(pName), ID(id)
	{}

	template<typename ExecuteFn>
	RGPass& Bind(ExecuteFn&& callback)
	{
		RG_STATIC_ASSERT(sizeof(ExecuteFn) < 4096, "The Execute callback exceeds the maximum size");
		checkf(!ExecuteCallback.IsBound(), "Pass is already bound! This may be unintentional");
		ExecuteCallback.BindLambda(std::move(callback));
		return *this;
	}

	RGPass& Write(Span<RGResourceHandle*> resources);
	RGPass& Read(Span<RGResourceHandle> resources);
	RGPass& RenderTarget(RGResourceHandle& resource, RenderPassAccess access);
	RGPass& DepthStencil(RGResourceHandle& resource, RenderPassAccess depthAccess, bool write = true, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess);
	RGPass& NeverCull();

	bool ReadsFrom(RGResourceHandle handle) const
	{
		return std::find(Reads.begin(), Reads.end(), handle) != Reads.end();
	}

	bool WritesTo(RGResourceHandle handle) const
	{
		return std::find(Writes.begin(), Writes.end(), handle) != Writes.end();
	}

	DECLARE_DELEGATE(ExecutePassDelegate, CommandContext& /*context*/, const RGPassResources& /*resources*/);
	const char* pName;
	ExecutePassDelegate ExecuteCallback;

	RGGraph& Graph;
	std::vector<RGResourceHandle> Reads;
	std::vector<RGResourceHandle> Writes;
	std::vector<RenderTargetAccess> RenderTargets;
	DepthStencilAccess DepthStencilTarget;

	bool ShouldNeverCull = false;
	int ID;

	//RenderGraph compile-time values
	int References = 0;
};

class RGGraph
{
	class Allocator
	{
	public:
		Allocator(uint64 size)
			: m_Size(size), m_pData(new char[size]), m_pCurrentOffset(m_pData)
		{}
		~Allocator()
		{
			delete[] m_pData;
		}
		template<typename T, typename ...Args>
		T* Allocate(Args... args)
		{
			check(m_pCurrentOffset - m_pData + sizeof(T) < m_Size);
			void* pData = m_pCurrentOffset;
			m_pCurrentOffset += sizeof(T);
			return new (pData) T(std::forward<Args>(args)...);
		}

		template<typename T>
		void Release(T* pPtr)
		{
			pPtr->~T();
		}

	private:
		uint64 m_Size;
		char* m_pData;
		char* m_pCurrentOffset;
	};

public:
	explicit RGGraph(GraphicsDevice* pDevice, uint64 allocatorSize = 0xFFFF);
	~RGGraph();

	RGGraph(const RGGraph& other) = delete;
	RGGraph& operator=(const RGGraph& other) = delete;

	void Compile();
	SyncPoint Execute();
	void Present(RGResourceHandle resource);
	void DumpGraph(const char* pPath) const;
	RGResourceHandle MoveResource(RGResourceHandle From, RGResourceHandle To);

	RGPass& AddPass(const char* pName)
	{
		RGPass* pPass = m_Allocator.Allocate<RGPass>(std::ref(*this), pName, (int)m_RenderPasses.size());
		m_RenderPasses.push_back(pPass);
		return *m_RenderPasses.back();
	}

	RGResourceHandle CreateTexture(const char* pName, const TextureDesc& desc)
	{
		RGResource* pResource = m_Allocator.Allocate<RGTexture>(pName, (int)m_Resources.size(), desc, nullptr);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	RGResourceHandle CreateBuffer(const char* pName, const BufferDesc& desc)
	{
		RGResource* pResource = m_Allocator.Allocate<RGBuffer>(pName, (int)m_Resources.size(), desc, nullptr);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	RGResourceHandle ImportTexture(const char* pName, Texture* pTexture)
	{
		check(pTexture);
		RGResource* pResource = m_Allocator.Allocate<RGTexture>(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	RGResourceHandle ImportBuffer(const char* pName, Buffer* pBuffer)
	{
		check(pBuffer);
		BufferDesc desc{};
		RGResource* pResource = m_Allocator.Allocate<RGBuffer>(pName, (int)m_Resources.size(), desc, pBuffer);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	bool IsValidHandle(RGResourceHandle handle) const
	{
		return handle.IsValid() && handle.Index < (int)m_ResourceNodes.size();
	}

	RGResourceHandle CreateResourceNode(RGResource* pResource)
	{
		RGNode node(pResource);
		m_ResourceNodes.push_back(node);
		return RGResourceHandle((int)m_ResourceNodes.size() - 1);
	}

	const RGNode& GetResourceNode(RGResourceHandle handle) const
	{
		RG_ASSERT(IsValidHandle(handle), "Invalid handle");
		return m_ResourceNodes[handle.Index];
	}

	RGResource* GetResource(RGResourceHandle handle) const
	{
		const RGNode& node = GetResourceNode(handle);
		return node.pResource;
	}

	void PushEvent(const char* pName);
	void PopEvent();

private:
	void ExecutePass(RGPass* pPass, CommandContext& context);
	void PrepareResources(RGPass* pPass);
	void ReleaseResources(RGPass* pPass);
	void DestroyData();

	void ProcessEvents(CommandContext& context, uint32 passIndex, bool begin);

	struct RGResourceAlias
	{
		RGResourceHandle From;
		RGResourceHandle To;
	};

	GraphicsDevice* m_pDevice;
	Allocator m_Allocator;
	SyncPoint m_LastSyncPoint;
	bool m_ImmediateMode = false;

	struct ProfileEvent
	{
		const char* pName;
		bool Begin;
		uint32 PassIndex;
	};

	uint32 m_CurrentEvent = 0;
	std::vector<ProfileEvent> m_Events;
	int m_EventStackSize = 0;

	std::vector<RGResourceAlias> m_Aliases;
	std::vector<RGPass*> m_RenderPasses;
	std::vector<RGResource*> m_Resources;
	std::vector<RGNode> m_ResourceNodes;
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
