#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"

CommandContext::CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator, D3D12_COMMAND_LIST_TYPE type)
	: m_pGraphics(pGraphics), m_pCommandList(pCommandList), m_pAllocator(pAllocator), m_Type(type)
{

}

CommandContext::~CommandContext()
{

}

void CommandContext::Reset()
{
	m_pAllocator = m_pGraphics->GetCommandQueue(m_Type)->RequestAllocator();
	m_pCommandList->Reset(m_pAllocator, nullptr);
}

uint64 CommandContext::Execute(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64 fenceValue = pQueue->ExecuteCommandList(m_pCommandList);
	pQueue->FreeAllocator(fenceValue, m_pAllocator);
	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}
	m_pGraphics->FreeCommandList(this);
	return fenceValue;
}

void CommandContext::Draw(int vertexStart, int vertexCount)
{
	PrepareDraw();
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void CommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex /*= 0*/)
{
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void CommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex /*= 0*/, int instanceStart /*= 0*/)
{
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color /*= Color(0.15f, 0.15f, 0.15f, 1.0f)*/)
{
	m_pCommandList->ClearRenderTargetView(rtv, &color.x, 0, nullptr);
}

void CommandContext::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags /*= D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL*/, float depth /*= 1.0f*/, unsigned char stencil /*= 0*/)
{
	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void CommandContext::SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE* pRtv)
{
	m_pRenderTarget = pRtv;
}

void CommandContext::SetDepthStencil(D3D12_CPU_DESCRIPTOR_HANDLE* pDsv)
{
	m_pDepthStencilView = pDsv;
}

void CommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void CommandContext::SetVertexBuffer(D3D12_VERTEX_BUFFER_VIEW vertexBufferView)
{
	SetVertexBuffers(&vertexBufferView, 1);
}

void CommandContext::SetVertexBuffers(D3D12_VERTEX_BUFFER_VIEW* pBuffers, int bufferCount)
{
	m_pCommandList->IASetVertexBuffers(0, bufferCount, pBuffers);
}

void CommandContext::SetIndexBuffer(D3D12_INDEX_BUFFER_VIEW indexBufferView)
{
	m_pCommandList->IASetIndexBuffer(&indexBufferView);
}

void CommandContext::SetViewport(const Rect& rect, float minDepth /*= 0.0f*/, float maxDepth /*= 1.0f*/)
{
	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = (float)rect.x;
	viewport.TopLeftY = (float)rect.y;
	viewport.Height = (float)rect.height;
	viewport.Width = (float)rect.width;
	viewport.MinDepth = minDepth;
	viewport.MaxDepth = maxDepth;
	m_pCommandList->RSSetViewports(1, &viewport);
}

void CommandContext::SetScissorRect(const Rect& rect)
{
	D3D12_RECT r;
	r.left = rect.x;
	r.top = rect.x;
	r.right = rect.x + rect.width;
	r.bottom = rect.y + rect.height;
	m_pCommandList->RSSetScissorRects(1, &r);
}

void CommandContext::FlushResourceBarriers()
{
	if (m_NumQueuedBarriers > 0)
	{
		m_pCommandList->ResourceBarrier(m_NumQueuedBarriers, m_QueuedBarriers.data());
		m_NumQueuedBarriers = 0;
	}
}

void CommandContext::InsertResourceBarrier(D3D12_RESOURCE_BARRIER barrier, bool executeImmediate /*= false*/)
{
	if (m_NumQueuedBarriers >= 16)
	{
		FlushResourceBarriers();
	}
	m_QueuedBarriers[m_NumQueuedBarriers] = barrier;
	++m_NumQueuedBarriers;
	if (executeImmediate)
	{
		FlushResourceBarriers();
	}
}

void CommandContext::PrepareDraw()
{
	m_pCommandList->OMSetRenderTargets(1, m_pRenderTarget, false, m_pDepthStencilView);
}
