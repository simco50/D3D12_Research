#pragma once
#include "DynamicResourceAllocator.h"
class Graphics;
class GraphicsResource;
class GraphicsBuffer;
class VertexBuffer;
class IndexBuffer;
class Texture2D;
class DynamicDescriptorAllocator;
class RootSignature;
class GraphicsPipelineState;
class ComputePipelineState;

class GraphicsCommandContext;
class ComputeCommandContext;
class CopyCommandContext;

enum class CommandListContext
{
	Graphics,
	Compute
};

class CommandContext
{
public:
	CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator);
	virtual ~CommandContext();

	virtual void Reset();
	virtual uint64 Execute(bool wait);
	virtual uint64 ExecuteAndReset(bool wait);

	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate = false);
	void FlushResourceBarriers();

	DynamicAllocation AllocateUploadMemory(uint32 size);
	void InitializeBuffer(GraphicsBuffer* pResource, const void* pData, uint32 dataSize, uint32 offset = 0);
	void InitializeTexture(Texture2D* pResource, D3D12_SUBRESOURCE_DATA* pSubResourceDatas, int firstSubResource, int subResourceCount);

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	void MarkBegin(const wchar_t* pName);
	void MarkEvent(const wchar_t* pName);
	void MarkEnd();

	void SetName(const char* pName);

protected:
	static const int MAX_QUEUED_BARRIERS = 12;

	std::array<D3D12_RESOURCE_BARRIER, MAX_QUEUED_BARRIERS> m_QueuedBarriers = {};
	int m_NumQueuedBarriers = 0;

	Graphics* m_pGraphics;

	ID3D12GraphicsCommandList* m_pCommandList;
	ID3D12CommandAllocator* m_pAllocator;
	D3D12_COMMAND_LIST_TYPE m_Type;
};

class CopyCommandContext : public CommandContext
{
public:
	CopyCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator);
};

class ComputeCommandContext : public CopyCommandContext
{
public:
	ComputeCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator);

	//Commands
	void Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ);

	virtual void Reset() override;
	virtual uint64 Execute(bool wait) override;
	virtual uint64 ExecuteAndReset(bool wait) override;

	//Bindings
	void SetComputePipelineState(ComputePipelineState* pPipelineState);
	void SetComputeRootSignature(RootSignature* pRootSignature);
	void SetComputeRootConstants(int rootIndex, uint32 count, const void* pConstants);
	void SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32 dataSize);

	void SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count = 1);
	void SetDynamicSampler(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicSamplers(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count = 1);

	void SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type);

protected:
	CommandListContext m_CurrentContext;

	std::unique_ptr<DynamicDescriptorAllocator> m_pShaderResourceDescriptorAllocator;
	std::unique_ptr<DynamicDescriptorAllocator> m_pSamplerDescriptorAllocator;

	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_CurrentDescriptorHeaps = {};

private:
	void BindDescriptorHeaps();
};

class GraphicsCommandContext : public ComputeCommandContext
{
public:
	GraphicsCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator);

	//Commands
	void Draw(int vertexStart, int vertexCount);
	void DrawIndexed(int indexCount, int indexStart, int minVertex = 0);
	void DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex = 0, int instanceStart = 0);
	void ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.0f, 0.0f, 0.0f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);

	//Bindings
	void SetGraphicsPipelineState(GraphicsPipelineState* pPipelineState);
	void SetGraphicsRootSignature(RootSignature* pRootSignature);

	void SetGraphicsRootConstants(int rootIndex, uint32 count, const void* pConstants);
	void SetDynamicConstantBufferView(int rootIndex, void* pData, uint32 dataSize);
	void SetDynamicVertexBuffer(int slot, int elementCount, int elementSize, void* pData);
	void SetDynamicIndexBuffer(int elementCount, void* pData);
	void SetDepthOnlyTarget(D3D12_CPU_DESCRIPTOR_HANDLE dsv);
	void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
	void SetRenderTargets(D3D12_CPU_DESCRIPTOR_HANDLE* pRtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffer(VertexBuffer* pVertexBuffer);
	void SetVertexBuffers(VertexBuffer* pVertexBuffers, int bufferCount);
	void SetIndexBuffer(IndexBuffer* pIndexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);
};