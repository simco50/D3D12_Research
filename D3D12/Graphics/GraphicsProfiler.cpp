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

	int bufferSize = HEAP_SIZE * Graphics::FRAME_COUNT * sizeof(uint64) * 2;
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
	context.GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_CurrentIndex);
	m_CurrentIndex++;
}

void GraphicsProfiler::End(CommandContext& context)
{
	context.GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_CurrentIndex);
	m_CurrentIndex++;
}

void GraphicsProfiler::Readback(int frameIndex)
{
	assert(m_CurrentIndex % 2 == 0);
	int offset = GetOffsetForFrame(frameIndex);
	int elements = m_CurrentIndex % HEAP_SIZE;

	if (elements > 0)
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, offset, elements, m_pReadBackBuffer->GetResource(), 0);
		pContext->Execute(true);

		m_pReadBackBuffer->Map(0, 0, m_pReadBackBuffer->GetSize());
		std::vector<double> times;
		uint64* pData = (uint64*)m_pReadBackBuffer->GetMappedData();
		for (int i = 0; i < elements; i += 2)
		{
			uint64 start = pData[i];
			uint64 end = pData[i + 1];
			double time = (end - start) * m_SecondsPerTick * 1000.0;
			std::cout << time << " ms" << std::endl;
			times.push_back(time);
		}
		m_pReadBackBuffer->Unmap();

	}
	m_CurrentIndex = GetOffsetForFrame(frameIndex + 1);
}

int GraphicsProfiler::GetOffsetForFrame(int frameIndex)
{
	frameIndex = frameIndex % Graphics::FRAME_COUNT;
	return HEAP_SIZE * 2 * frameIndex;
}