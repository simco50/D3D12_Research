#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Graphics;

enum class BufferUsage
{
	Default			= 0,
	Dynamic			= 1 << 0,
	UnorderedAccess = 1 << 1,
	ShaderResource	= 1 << 2,
};
DEFINE_ENUM_FLAG_OPERATORS(BufferUsage)

class GraphicsBuffer : public GraphicsResource
{
public:
	void Create(Graphics* pGraphics, uint64 size, bool cpuVisible = false);
	void SetData(CommandContext* pContext, void* pData, uint64 dataSize, uint32 offset = 0);

	void* Map(uint32 subResource = 0, uint64 readFrom = 0, uint64 readTo = 0);
	void Unmap(uint32 subResource = 0, uint64 writtenFrom = 0, uint64 writtenTo = 0);

	inline void* GetMappedData() const { return m_pMappedData; }
	inline uint64 GetSize() const { return m_ElementCount * m_ElementStride; }
	inline uint32 GetStride() const { return m_ElementStride; }
	inline uint64 GetElementCount() const { return m_ElementCount; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

protected:
	virtual void CreateViews(ID3D12Device* pDevice) {}

	void CreateInternal(ID3D12Device* pDevice, uint32 elementStride, uint64 elementCount, BufferUsage usage);

	uint32 m_ElementStride = 0;
	uint64 m_ElementCount = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv = {};

private:
	void* m_pMappedData = nullptr;
	BufferUsage m_Usage = BufferUsage::Default;
};

class StructuredBuffer : public GraphicsBuffer
{
public:
	StructuredBuffer(Graphics* pGraphics);
	void Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible = false);
	virtual void CreateViews(ID3D12Device* pDevice) override;

	GraphicsResource* GetCounter() const { return m_pCounter.get(); }

private:
	std::unique_ptr<GraphicsResource> m_pCounter;
};

class ByteAddressBuffer : public GraphicsBuffer
{
public:
	ByteAddressBuffer(Graphics* pGraphics);
	void Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible = false);
	virtual void CreateViews(ID3D12Device* pDevice) override;
};

class VertexBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible = false);
	virtual void CreateViews(ID3D12Device* pDevice) override;

	inline const D3D12_VERTEX_BUFFER_VIEW GetView() const { return m_View; }
private:
	D3D12_VERTEX_BUFFER_VIEW m_View;
};

class IndexBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphics, bool smallIndices, uint64 elementCount, bool cpuVisible = false);
	inline const D3D12_INDEX_BUFFER_VIEW GetView() const { return m_View; }
	virtual void CreateViews(ID3D12Device* pDevice) override;

private:
	bool m_SmallIndices = false;
	D3D12_INDEX_BUFFER_VIEW m_View;
};