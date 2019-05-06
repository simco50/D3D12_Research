#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Graphics;

class GraphicsBuffer : public GraphicsResource
{
public:
	void Create(ID3D12Device* pDevice, uint32 size, bool cpuVisible = false);
	void SetData(CommandContext* pContext, void* pData, uint32 dataSize, uint32 offset = 0);

	inline uint32 GetSize() const { return m_Size; }

protected:
	uint32 m_Size;
};

class StructuredBuffer : public GraphicsBuffer
{
public:
	void Create(Graphics* pGraphic, uint32 elementStride, uint32 elementCount, bool cpuVisible = false);
	
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }
private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Uav;
};