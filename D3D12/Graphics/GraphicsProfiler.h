#pragma once
#include "Graphics.h"
class ReadbackBuffer;
class CommandContext;

class GraphicsProfiler
{
public:
	GraphicsProfiler(Graphics* pGraphics);
	~GraphicsProfiler();

	void Begin(CommandContext& context);
	void End(CommandContext& context);

	void BeginReadback(int frameIndex);
	void EndReadBack(int frameIndex);

	double GetTime(int index) const;

private:
	constexpr static int HEAP_SIZE = 512;

	std::array<uint64, Graphics::FRAME_COUNT> m_FenceValues = {};
	uint64* m_pCurrentReadBackData = nullptr;

	Graphics* m_pGraphics;
	double m_SecondsPerTick = 0.0;
	int m_CurrentTimer = 0;
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<ReadbackBuffer> m_pReadBackBuffer;
};