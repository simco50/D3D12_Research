#include "stdafx.h"
#include "RingBufferAllocator.h"
#include "Device.h"
#include "Buffer.h"
#include "CommandContext.h"

RingBufferAllocator::RingBufferAllocator(GraphicsDevice* pDevice, uint32 size)
	: DeviceObject(pDevice), m_pQueue(pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY)), m_Size(size), m_ConsumeOffset(0), m_ProduceOffset(0)
{
	m_pBuffer = pDevice->CreateBuffer(BufferDesc{ .Size = size, .Flags = BufferFlag::Upload }, "RingBuffer");
}

RingBufferAllocator::~RingBufferAllocator()
{
	Sync();
	m_ConsumeOffset = 0;
	m_ProduceOffset = 0;
}

bool RingBufferAllocator::Allocate(uint32 size, RingBufferAllocation& allocation)
{
	std::lock_guard lock(m_Lock);

	while (!m_RetiredAllocations.empty())
	{
		const RetiredAllocation& retired = m_RetiredAllocations.front();
		if (!retired.Sync.IsComplete())
			break;
		m_ConsumeOffset = retired.Offset + retired.Size;
		m_RetiredAllocations.pop();
	}

	constexpr uint32 InvalidOffset = 0xFFFFFFFF;
	uint32 offset = InvalidOffset;

	if (size > m_Size)
		return false;

	if (m_ProduceOffset >= m_ConsumeOffset)
	{
		if (m_ProduceOffset + size <= m_Size)
		{
			offset = m_ProduceOffset;
			m_ProduceOffset += size;
		}
		else if (size <= m_ConsumeOffset)
		{
			offset = 0;
			m_ProduceOffset = size;
		}
	}
	else if (m_ProduceOffset + size <= m_ConsumeOffset)
	{
		offset = m_ProduceOffset;
		m_ProduceOffset += size;
	}

	if (offset == InvalidOffset)
		return false;

	allocation.pContext = GetParent()->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
	allocation.Offset = offset;
	allocation.Size = size;
	allocation.GpuHandle = m_pBuffer->GetGpuHandle() + offset;
	allocation.pBackingResource = m_pBuffer;
	allocation.pMappedMemory = (char*)m_pBuffer->GetMappedData() + offset;
	return true;
}

void RingBufferAllocator::Free(RingBufferAllocation& allocation)
{
	std::lock_guard lock(m_Lock);

	RetiredAllocation retired;
	retired.Offset = allocation.Offset;
	retired.Size = allocation.Size;
	retired.Sync = allocation.pContext->Execute();
	m_RetiredAllocations.push(retired);

	allocation.pBackingResource = nullptr;
	allocation.pContext = nullptr;
	allocation.pMappedMemory = nullptr;

	m_LastSync = retired.Sync;
}

void RingBufferAllocator::Sync()
{
	if(m_LastSync.IsValid())
		m_LastSync.Wait();
}
