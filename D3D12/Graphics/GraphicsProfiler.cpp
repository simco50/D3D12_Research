#include "stdafx.h"
#include "GraphicsProfiler.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "GraphicsBuffer.h"
#include "CommandQueue.h"

GraphicsProfiler::GraphicsProfiler(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	D3D12_QUERY_HEAP_DESC desc = {};
	desc.Count = HEAP_SIZE * Graphics::FRAME_COUNT * 2;
	desc.NodeMask = 0;
	desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	HR(pGraphics->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(m_pQueryHeap.GetAddressOf())));

	int bufferSize = HEAP_SIZE * sizeof(uint64) * 2 * Graphics::FRAME_COUNT;
	m_pReadBackBuffer = std::make_unique<ReadbackBuffer>();
	m_pReadBackBuffer->Create(pGraphics, bufferSize);

	uint64 timeStampFrequency;
	pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetCommandQueue()->GetTimestampFrequency(&timeStampFrequency);
	m_SecondsPerTick = 1.0 / timeStampFrequency;
}

GraphicsProfiler::~GraphicsProfiler()
{
}

void GraphicsProfiler::Begin(CommandContext& context)
{
	context.GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_CurrentTimer * 2);
}

void GraphicsProfiler::End(CommandContext& context)
{
	context.GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_CurrentTimer * 2 + 1);
	m_CurrentTimer++;
}

void GraphicsProfiler::BeginReadback(int frameIndex)
{
	assert(m_pCurrentReadBackData == nullptr);
	m_pGraphics->WaitForFence(m_FenceValues[frameIndex]);

	int offset = HEAP_SIZE * frameIndex * 2;
	m_pCurrentReadBackData = (uint64*)m_pReadBackBuffer->Map(0, 0, m_pReadBackBuffer->GetSize()) + offset;
	
	std::cout << GetTime(0) << std::endl;
}

void GraphicsProfiler::EndReadBack(int frameIndex)
{
	m_pReadBackBuffer->Unmap();
	m_pCurrentReadBackData = nullptr;

	int offset = HEAP_SIZE * frameIndex * 2;

	GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, offset, HEAP_SIZE * 2, m_pReadBackBuffer->GetResource(), offset * sizeof(uint64));
	m_FenceValues[frameIndex] = pContext->Execute(false);

	m_CurrentTimer = HEAP_SIZE * frameIndex;
}

double GraphicsProfiler::GetTime(int index) const
{
	assert(m_pCurrentReadBackData);
	uint64 start = m_pCurrentReadBackData[index];
	uint64 end = m_pCurrentReadBackData[index + 1];
	return (end - start) * m_SecondsPerTick * 1000.0;
}
