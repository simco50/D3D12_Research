#pragma once
#include "Graphics.h"
class ReadbackBuffer;
class CommandContext;

class GraphicsProfiler
{
private:
	struct Block
	{
		Block() : TimerIndex(-1), pParent(nullptr), Name{}
		{}
		Block(const char* pName, int index, Block* pParent) : TimerIndex(index), pParent(pParent)
		{
			strcpy_s(Name, pName);
		}
		int TimerIndex;
		char Name[128];
		Block* pParent = nullptr;
		std::deque<std::unique_ptr<Block>> Children;
	};

public:
	static GraphicsProfiler* Instance();

	void Initialize(Graphics* pGraphics);

	void Begin(const char* pName, CommandContext& context);
	void End(CommandContext& context);

	void BeginReadback(int frameIndex);
	void EndReadBack(int frameIndex);

	float GetTime(int index) const;

private:
	GraphicsProfiler() = default;

	constexpr static int HEAP_SIZE = 512;

	std::array<uint64, Graphics::FRAME_COUNT> m_FenceValues = {};
	uint64* m_pCurrentReadBackData = nullptr;

	Graphics* m_pGraphics;
	double m_SecondsPerTick = 0.0;
	int m_CurrentTimer = 0;
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<ReadbackBuffer> m_pReadBackBuffer;

	Block m_RootBlock;
	Block* m_pCurrentBlock = nullptr;
};