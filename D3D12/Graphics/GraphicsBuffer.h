#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Graphics;

class GraphicsBuffer : public GraphicsResource
{
public:
	GraphicsBuffer() = default;
	GraphicsBuffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);

	void SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset = 0);

	void* Map(uint32 subResource = 0, uint64 readFrom = 0, uint64 readTo = 0);
	void Unmap(uint32 subResource = 0, uint64 writtenFrom = 0, uint64 writtenTo = 0);

	inline uint64 GetSize() const { return m_ElementCount * m_ElementStride; }
	inline uint32 GetStride() const { return m_ElementStride; }
	inline uint64 GetElementCount() const { return m_ElementCount; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

protected:
	void Create(Graphics* pGraphics, uint64 elementCount, uint32 elementStride, bool cpuVisible);
	virtual void CreateViews(Graphics* pGraphics) {}

	uint32 m_ElementStride = 0;
	uint64 m_ElementCount = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv = {};
};

class ByteAddressBuffer : public GraphicsBuffer
{
public:
	ByteAddressBuffer(Graphics* pGraphics);
	void Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;
};

class StructuredBuffer : public GraphicsBuffer
{
public:
	StructuredBuffer(Graphics* pGraphics);
	void Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;

	ByteAddressBuffer* GetCounter() const { return m_pCounter.get(); }

private:
	std::unique_ptr<ByteAddressBuffer> m_pCounter;
};

class VertexBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint64 elementCount, uint32 elementStride, bool cpuVisible = false);
	virtual void CreateViews(Graphics* pGraphics) override;

	inline const D3D12_VERTEX_BUFFER_VIEW GetView() const { return m_View; }
private:
	D3D12_VERTEX_BUFFER_VIEW m_View;
};

class IndexBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, bool smallIndices, uint32 elementCount, bool cpuVisible = false);
	inline const D3D12_INDEX_BUFFER_VIEW GetView() const { return m_View; }
	virtual void CreateViews(Graphics* pGraphics) override;

private:
	bool m_SmallIndices = false;
	D3D12_INDEX_BUFFER_VIEW m_View;
};

class ReadbackBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint64 size);

private:
	D3D12_INDEX_BUFFER_VIEW m_View;
};