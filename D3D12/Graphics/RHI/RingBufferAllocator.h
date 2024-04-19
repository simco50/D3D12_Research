#pragma once
#include "GraphicsResource.h"
#include "CommandQueue.h"

class SyncPoint;
class CommandContext;

struct RingBufferAllocation
{
	CommandContext* pContext;
	Ref<Buffer> pBackingResource;
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	uint32 Offset = 0;
	uint32 Size = 0;
	void* pMappedMemory = nullptr;
};

class RingBufferAllocator : public DeviceObject
{
public:
	RingBufferAllocator(GraphicsDevice* pDevice, uint32 size);
	~RingBufferAllocator();

	bool Allocate(uint32 size, RingBufferAllocation& allocation);
	void Free(RingBufferAllocation& allocation);
	void Sync();

private:
	struct RetiredAllocation
	{
		SyncPoint Sync;
		uint32 Offset;
		uint32 Size;
	};
	std::queue<RetiredAllocation> m_RetiredAllocations;

	CommandQueue* m_pQueue;
	std::mutex m_Lock;
	uint32 m_Size;
	uint32 m_ConsumeOffset;
	uint32 m_ProduceOffset;

	SyncPoint m_LastSync;
	Ref<Buffer> m_pBuffer;
};
