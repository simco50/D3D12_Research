#pragma once
#include "RenderGraphDefinitions.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/Buffer.h"

#define RG_GRAPH_SCOPE(name, graph) RGGraphScope MACRO_CONCAT(rgScope_,__COUNTER__)(name, graph)

class CommandContext;

class RGGraph;
class RGPass;

enum class RGResourceType
{
	None,
	Texture,
	Buffer,
};

class RGResource
{
public:
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

class RGTexture : public RGResource
{
public:
	RGTexture(const char* pName, int id, const TextureDesc& desc, Texture* pTexture)
		: RGResource(pName, id, pTexture != nullptr, RGResourceType::Texture, pTexture), m_Desc(desc)
	{}
	const TextureDesc& GetDesc() const { return m_Desc; }
private:
	TextureDesc m_Desc;
};

class RGBuffer : public RGResource
{
public:
	RGBuffer(const char* pName, int id, const BufferDesc& desc, Buffer* pBuffer)
		: RGResource(pName, id, pBuffer != nullptr, RGResourceType::Buffer, pBuffer), m_Desc(desc)
	{}
	const BufferDesc& GetDesc() const { return m_Desc; }
private:
	BufferDesc m_Desc;
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

	Texture* GetTexture(RGResourceHandle handle) const;

private:
	RGGraph& m_Graph;
	RGPass& m_Pass;
};

class RGPass
{
public:
	friend class RGPassBuilder;
	friend class RGGraph;

	RGPass(RGGraph& graph, const char* pName, int id)
		: m_Name(pName), m_RenderGraph(graph), m_Id(id)
	{}

	void Execute(CommandContext& context, const RGPassResources& resources)
	{
		check(m_ExecuteCallback.IsBound());
		m_ExecuteCallback.Execute(context, resources);
	}

	template<typename ExecuteCallback>
	void SetCallback(ExecuteCallback&& callback)
	{
		RG_STATIC_ASSERT(sizeof(ExecuteCallback) < 4096, "The Execute callback exceeds the maximum size");
		m_ExecuteCallback.BindLambda(std::move(callback));
	}

	bool ReadsFrom(RGResourceHandle handle) const
	{
		return std::find(m_Reads.begin(), m_Reads.end(), handle) != m_Reads.end();
	}

	bool WritesTo(RGResourceHandle handle) const
	{
		return std::find(m_Writes.begin(), m_Writes.end(), handle) != m_Writes.end();
	}

	const char* GetName() const { return m_Name; }

private:
	DECLARE_DELEGATE(ExecutePassDelegate, CommandContext& /*context*/, const RGPassResources& /*resources*/);
	const char* m_Name;
	ExecutePassDelegate m_ExecuteCallback;
	std::vector<RGResourceHandle> m_Reads;
	std::vector<RGResourceHandle> m_Writes;
	RGGraph& m_RenderGraph;
	bool m_NeverCull = false;
	int m_Id;

	//RenderGraph compile-time values
	int m_References = 0;
};

class RGPassBuilder
{
public:
	RGPassBuilder(RGGraph& renderGraph, RGPass& pass)
		: m_Pass(pass), m_RenderGraph(renderGraph)
	{}

	template<typename ExecuteCallback>
	void Bind(ExecuteCallback&& callback)
	{
		m_Pass.SetCallback(std::move(callback));
	}

	RGResourceHandle Read(const RGResourceHandle& resource);
	[[nodiscard]] RGResourceHandle Write(RGResourceHandle& resource);
	RGResourceHandle CreateTexture(const char* pName, const TextureDesc& desc);
	RGResourceHandle CreateBuffer(const char* pName, const BufferDesc& desc);
	void NeverCull();

private:
	RGPass& m_Pass;
	RGGraph& m_RenderGraph;
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
	int64 Execute();
	void Present(RGResourceHandle resource);
	void DumpGraphViz(const char* pPath) const;
	void DumpGraphMermaid(const char* pPath) const;
	RGResourceHandle MoveResource(RGResourceHandle From, RGResourceHandle To);

	RGPassBuilder AddPass(const char* pName)
	{
		RGPass* pPass = m_Allocator.Allocate<RGPass>(std::ref(*this), pName, (int)m_RenderPasses.size());
		AddPass(pPass);
		return RGPassBuilder(*this, *pPass);
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
	RGPass& AddPass(RGPass* pPass);
	void ExecutePass(RGPass* pPass, CommandContext& context);
	void PrepareResources(RGPass* pPass);
	void ReleaseResources(RGPass* pPass);
	void DestroyData();

	void ConditionallyCreateResource(RGResource* pResource);
	void ConditionallyReleaseResource(RGResource* pResource);

	void ProcessEvents(CommandContext& context, uint32 passIndex, bool begin);

	struct RGResourceAlias
	{
		RGResourceHandle From;
		RGResourceHandle To;
	};

	GraphicsDevice* m_pDevice;
	Allocator m_Allocator;
	uint64 m_LastFenceValue = 0;
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
