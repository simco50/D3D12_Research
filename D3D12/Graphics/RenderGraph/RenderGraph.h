#pragma once
#include "RenderGraphDefinitions.h"
#include "../Texture.h"
#include "../GraphicsBuffer.h"

class Graphics;
class CommandContext;

class RGResourceAllocator;
class RGGraph;
class RGPassBase;

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
		: m_Name(pName), m_Id(id), m_IsImported(isImported), m_pPhysicalResource(pResource), m_Type(type)
	{}

	const char* m_Name;
	int m_Id;
	bool m_IsImported;
	int m_Version = 0;
	RGResourceType m_Type;
	void* m_pPhysicalResource;

	//RenderGraph compile-time values
	int m_References = 0;
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
	RGPassBase* pWriter = nullptr;
	int Reads = 0;
};

struct RGResourceHandle
{
	explicit RGResourceHandle(int id = InvalidIndex)
		: Index(id)
	{}

	bool operator==(const RGResourceHandle& other) const { return Index == other.Index; }
	bool operator!=(const RGResourceHandle& other) const { return Index != other.Index; }

	constexpr static const int InvalidIndex = -1;

	inline void Invalidate() { Index = InvalidIndex; }
	inline bool IsValid() const { return Index != InvalidIndex; }

	int Index;
};

struct RGResourceHandleMutable : public RGResourceHandle
{
	explicit RGResourceHandleMutable(int id = InvalidIndex)
		: RGResourceHandle(id)
	{}
};

class RGPassBuilder
{
public:
	RGPassBuilder(RGGraph& renderGraph, RGPassBase& pass)
		: m_RenderGraph(renderGraph),
		m_Pass(pass)
	{}

	RGPassBuilder(const RGPassBuilder& other) = delete;
	RGPassBuilder& operator=(const RGPassBuilder& other) = delete;

	RGResourceHandle Read(const RGResourceHandle& resource);
	RGResourceHandleMutable Write(RGResourceHandleMutable& resource);
	RGResourceHandleMutable CreateTexture(const char* pName, const TextureDesc& desc);
	RGResourceHandleMutable CreateBuffer(const char* pName, const BufferDesc& desc);
	void NeverCull();

private:
	RGPassBase& m_Pass;
	RGGraph& m_RenderGraph;
};

class RGPassResources
{
public:
	RGPassResources(RGGraph& graph, RGPassBase& pass)
		: m_Graph(graph), m_Pass(pass)
	{}

	RGPassResources(const RGPassResources& other) = delete;
	RGPassResources& operator=(const RGPassResources& other) = delete;

	template<typename T>
	T* GetResource(RGResourceHandle handle) const
	{
		const RGNode& node = m_Graph.GetResourceNode(handle);
		assert(node.pResource);
		return static_cast<T*>(node.pResource->m_pPhysicalResource);
	}

private:
	RGGraph& m_Graph;
	RGPassBase& m_Pass;
};

class RGPassBase
{
public:
	friend class RGPassBuilder;
	friend class RGGraph;

	RGPassBase(RGGraph& graph, const char* pName, int id)
		: m_Name(pName), m_RenderGraph(graph), m_Id(id)
	{}

	virtual ~RGPassBase() = default;

	virtual void Execute(const RGPassResources& resources, CommandContext& renderContext) = 0;
	const char* GetName() const { return m_Name; }

	bool ReadsFrom(RGResourceHandle handle) const
	{
		return std::find(m_Reads.begin(), m_Reads.end(), handle) != m_Reads.end();
	}

	bool WritesTo(RGResourceHandle handle) const
	{
		return std::find(m_Writes.begin(), m_Writes.end(), handle) != m_Writes.end();
	}

private:
	const char* m_Name;
	std::vector<RGResourceHandle> m_Reads;
	std::vector<RGResourceHandle> m_Writes;
	RGGraph& m_RenderGraph;
	bool m_NeverCull = false;
	int m_Id;

	//RenderGraph compile-time values
	int m_References = 0;
};

template<typename PassData>
class RGPass : public RGPassBase
{
	friend class RGGraph;
public:
	RGPass(RGGraph& graph, const char* pName, int id)
		: RGPassBase(graph, pName, id),
		m_PassData{}
	{}
	inline const PassData& GetData() const { return m_PassData; }
protected:
	PassData m_PassData;
};

template<typename PassData, typename ExecuteCallback>
class RGLambdaPass : public RGPass<PassData>
{
public:
	RGLambdaPass(RGGraph& graph, const char* pName, int id, ExecuteCallback&& executeCallback)
		: RGPass(graph, pName, id),
		m_ExecuteCallback(std::forward<ExecuteCallback>(executeCallback))
	{}
	virtual void Execute(const RGPassResources& resources, CommandContext& renderContext) override
	{
		m_ExecuteCallback(renderContext, resources, m_PassData);
	}
private:
	ExecuteCallback m_ExecuteCallback;
};

class RGGraph
{
public:
	explicit RGGraph(RGResourceAllocator* pAllocator);
	~RGGraph();

	RGGraph(const RGGraph& other) = delete;
	RGGraph& operator=(const RGGraph& other) = delete;

	void Compile();
	int64 Execute(Graphics* pGraphics);
	void Present(RGResourceHandle resource);
	void DumpGraphViz(const char* pPath) const;
	void DumpGraphMermaid(const char* pPath) const;
	RGResourceHandle MoveResource(RGResourceHandle From, RGResourceHandle To);

	template<typename PassData, typename SetupCallback, typename ExecuteCallback>
	RGPass<PassData>& AddCallbackPass(const char* pName, const SetupCallback& setupCallback, ExecuteCallback&& executeCallback)
	{
		RG_STATIC_ASSERT(sizeof(ExecuteCallback) < 1024, "The Execute callback exceeds the maximum size");
		RGPass<PassData>* pPass = new RGLambdaPass<PassData, ExecuteCallback>(*this, pName, (int)m_RenderPasses.size(), std::forward<ExecuteCallback>(executeCallback));
		RGPassBuilder builder(*this, *pPass);
		setupCallback(builder, pPass->m_PassData);
		m_RenderPasses.push_back(pPass);
		return *pPass;
	}

	RGResourceHandleMutable CreateTexture(const char* pName, const TextureDesc& desc)
	{
		RGResource* pResource = new RGTexture(pName, (int)m_Resources.size(), desc, nullptr);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	RGResourceHandleMutable CreateBuffer(const char* pName, const BufferDesc& desc)
	{
		RGResource* pResource = new RGBuffer(pName, (int)m_Resources.size(), desc, nullptr);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	RGResourceHandleMutable ImportTexture(const char* pName, Texture* pTexture)
	{
		assert(pTexture);
		RGResource* pResource = new RGTexture(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	RGResourceHandleMutable ImportBuffer(const char* pName, Buffer* pBuffer)
	{
		assert(pBuffer);
		BufferDesc desc{};
		RGResource* pResource = new RGBuffer(pName, (int)m_Resources.size(), desc, pBuffer);
		m_Resources.push_back(pResource);
		return CreateResourceNode(pResource);
	}

	bool IsValidHandle(RGResourceHandle handle) const
	{
		return handle.IsValid() && handle.Index < (int)m_ResourceNodes.size();
	}

	RGResourceHandleMutable CreateResourceNode(RGResource* pResource)
	{
		RGNode node(pResource);
		m_ResourceNodes.push_back(node);
		return RGResourceHandleMutable((int)m_ResourceNodes.size() - 1);
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

private:
	void ExecutePass(RGPassBase* pPass, CommandContext& context, RGResourceAllocator* pAllocator);
	void PrepareResources(RGPassBase* pPass, RGResourceAllocator* pAllocator);
	void ReleaseResources(RGPassBase* pPass, RGResourceAllocator* pAllocator);
	void DestroyData();

	void ConditionallyCreateResource(RGResource* pResource, RGResourceAllocator* pAllocator);
	void ConditionallyReleaseResource(RGResource* pResource, RGResourceAllocator* pAllocator);

	struct RGResourceAlias
	{
		RGResourceHandle From;
		RGResourceHandle To;
	};
	RGResourceAllocator* m_pAllocator;

	std::vector<RGResourceAlias> m_Aliases;
	std::vector<RGPassBase*> m_RenderPasses;
	std::vector<RGResource*> m_Resources;
	std::vector<RGNode> m_ResourceNodes;
};
