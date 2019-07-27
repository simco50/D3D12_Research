#pragma once
#include "Graphics.h"
class ReadbackBuffer;
class CommandContext;

class CpuTimer
{
public:
	void Begin();
	void End();
	float GetTime(float ticksPerSecond) const;

private:
	int64 m_StartTime;
	int64 m_EndTime;
};

class GpuTimer
{
public:
	GpuTimer();
	void Begin(const char* pName, CommandContext* pContext);
	void End(CommandContext* pContext);
	float GetTime(float ticksPerSecond) const;
	int GetTimerIndex() const { return m_TimerIndex; }
private:
	int m_TimerIndex = 0;
};

class Profiler
{
private:
	struct Block
	{
		Block() : pParent(nullptr), Name{}
		{}
		Block(const char* pName, Block* pParent) 
			: pParent(pParent)
		{
			strcpy_s(Name, pName);
		}
		CpuTimer CpuTimer{};
		GpuTimer GpuTimer{};
		char Name[128];
		Block* pParent = nullptr;
		std::deque<std::unique_ptr<Block>> Children;
	};

public:
	static Profiler* Instance();

	void Initialize(Graphics* pGraphics);

	void Begin(const char* pName, CommandContext* pContext = nullptr);
	void End(CommandContext* pContext = nullptr);

	void BeginReadback(int frameIndex);
	void EndReadBack(int frameIndex);

	int32 GetNextTimerIndex();

	inline const uint64* GetData() const { return m_pCurrentReadBackData; }

private:
	Profiler() = default;

	constexpr static int HEAP_SIZE = 512;

	std::array<uint64, Graphics::FRAME_COUNT> m_FenceValues = {};
	uint64* m_pCurrentReadBackData = nullptr;

	Graphics* m_pGraphics;
	float m_SecondsPerGpuTick = 0.0f;
	float m_SecondsPerCpuTick = 0.0f;
	int m_CurrentTimer = 0;
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<ReadbackBuffer> m_pReadBackBuffer;

	std::unique_ptr<Block> m_pRootBlock;
	Block* m_pCurrentBlock = nullptr;
};