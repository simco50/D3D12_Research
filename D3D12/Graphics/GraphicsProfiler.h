#pragma once
class Graphics;
class ReadbackBuffer;
class CommandContext;

class GraphicsProfiler
{
public:
	GraphicsProfiler(Graphics* pGraphics);
	~GraphicsProfiler();

	void Begin(CommandContext& context);
	void End(CommandContext& context);
	void Readback(int frameIndex);

private:
	int GetOffsetForFrame(int frameIndex);

	constexpr static int HEAP_SIZE = 512;

	Graphics* m_pGraphics;
	double m_SecondsPerTick = 0.0;
	int m_CurrentIndex = 0;
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<ReadbackBuffer> m_pReadBackBuffer;
};