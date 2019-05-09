#include "stdafx.h"
#include "GraphicsProfiler.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "GraphicsBuffer.h"
#include "CommandQueue.h"

GraphicsProfiler* GraphicsProfiler::Instance()
{
	static GraphicsProfiler profiler;
	return &profiler;
}

void GraphicsProfiler::Initialize(Graphics* pGraphics)
{
	m_pGraphics = pGraphics;

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

	m_pCurrentBlock = &m_RootBlock;
}

void GraphicsProfiler::Begin(const char* pName, CommandContext& context)
{
	std::unique_ptr<Block> pNewBlock = std::make_unique<Block>(pName, m_CurrentTimer, m_pCurrentBlock);
	context.GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, pNewBlock->TimerIndex * 2);
	m_pCurrentBlock->Children.push_back(std::move(pNewBlock));
	m_pCurrentBlock = m_pCurrentBlock->Children.back().get();
	++m_CurrentTimer;
}

void GraphicsProfiler::End(CommandContext& context)
{	
	context.GetCommandList()->EndQuery(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_pCurrentBlock->TimerIndex * 2 + 1);
	m_pCurrentBlock = m_pCurrentBlock->pParent;
}

void GraphicsProfiler::BeginReadback(int frameIndex)
{
	assert(m_pCurrentReadBackData == nullptr);
	m_pGraphics->WaitForFence(m_FenceValues[frameIndex]);

	m_pCurrentReadBackData = (uint64*)m_pReadBackBuffer->Map(0, 0, m_pReadBackBuffer->GetSize());

	assert(m_pCurrentBlock == &m_RootBlock);
	m_pCurrentBlock = m_RootBlock.Children.front().get();
	int depth = 0;
	std::stringstream stream;
	bool run = true;
	while (run)
	{
		for (int i = 0; i < depth; ++i)
		{
			stream << "\t";
		}
		stream << "[" << m_pCurrentBlock->Name << "] > " << GetTime(m_pCurrentBlock->TimerIndex) << " ms" << std::endl;

		while (m_pCurrentBlock->Children.size() == 0)
		{
			m_pCurrentBlock = m_pCurrentBlock->pParent;
			if (m_pCurrentBlock == nullptr)
			{
				run = false;
				break;
			}
			m_pCurrentBlock->Children.pop_front();
			--depth;
		}
		if (run == false)
		{
			break;
		}
		m_pCurrentBlock = m_pCurrentBlock->Children.front().get();
		depth++;
	}
	m_pCurrentBlock = &m_RootBlock;
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

float GraphicsProfiler::GetTime(int index) const
{
	assert(m_pCurrentReadBackData);
	uint64 start = m_pCurrentReadBackData[index * 2];
	uint64 end = m_pCurrentReadBackData[index * 2 + 1];
	float time = (float)((end - start) * m_SecondsPerTick * 1000.0);
	return time;
}