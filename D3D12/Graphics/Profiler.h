#pragma once
#include "RHI/CommandQueue.h"
class Buffer;
class CommandContext;
class GraphicsDevice;

#define WITH_PROFILING 1

#if WITH_PROFILING
#define GPU_PROFILE_BEGIN(name, cmdlist) Profiler::Get()->Begin(name, cmdlist);
#define GPU_PROFILE_END() Profiler::Get()->End();

#define PROFILE_BEGIN(name) Profiler::Get()->Begin(name, nullptr);
#define PROFILE_END() Profiler::Get()->End();

#define GPU_PROFILE_SCOPE(name, cmdlist) ScopeProfiler MACRO_CONCAT(profiler,__COUNTER__)(name, cmdlist, true)
#define PROFILE_SCOPE(name) ScopeProfiler MACRO_CONCAT(profiler,__COUNTER__)(name, nullptr, true)
#else
#define GPU_PROFILE_BEGIN(name, cmdlist)
#define GPU_PROFILE_END()

#define PROFILE_BEGIN(name)
#define PROFILE_END()

#define GPU_PROFILE_SCOPE(name, cmdlist)
#define GPU_PROFILE_SCOPE_CONDITIONAL(name, cmdlist, condition)
#define PROFILE_SCOPE(name)
#endif

class TimeScope
{
public:
	TimeScope()
	{
		QueryPerformanceFrequency(&m_Frequency);
		QueryPerformanceCounter(&m_StartTime);
	}

	float Stop()
	{
		LARGE_INTEGER endTime;
		QueryPerformanceCounter(&endTime);
		return (float)((double)endTime.QuadPart - m_StartTime.QuadPart) / m_Frequency.QuadPart;
	}

private:
	LARGE_INTEGER m_StartTime, m_Frequency;
};

template<typename T, uint32 SIZE>
class TimeHistory
{
public:
	TimeHistory() = default;
	~TimeHistory() = default;

	void AddTime(T time)
	{
		m_TotalTime -= m_History[m_Entries % SIZE];
		m_TotalTime += time;

		m_History[m_Entries % SIZE] = time;
		++m_Entries;
	}

	T GetAverage() const
	{
		return m_TotalTime / Math::Min<uint32>(m_Entries, SIZE);
	}

	void GetHistory(const T** pData, uint32* count, uint32* dataOffset) const
	{
		*count = Math::Min<uint32>(m_Entries, SIZE);
		*dataOffset = m_Entries % SIZE;
		*pData = m_History.data();
	}

private:
	T m_TotalTime = {};
	uint32 m_Entries = 0;
	std::array<T, SIZE> m_History = {};
};

struct ProfileNode
{
	ProfileNode(const char* pInName, ProfileNode* pParent)
		: pParent(pParent)
	{
		strcpy_s(pName, pInName);
	}

	void StartTimer(CommandContext* pInContext);
	void EndTimer();

	void PopulateTimes(const uint64* pReadbackData, uint64 cpuFrequency, int frameIndex);
	ProfileNode* GetChild(const char* pName, int i = 0);

	uint64 CPUStartTime = 0;
	uint64 CPUEndTime = 0;
	int GPUTimerIndex = -1;
	TimeHistory<float, 128> CpuHistory;
	TimeHistory<float, 128> GpuHistory;
	CommandContext* pContext = nullptr;

	int LastHitFrame = -1;
	char pName[128];
	ProfileNode* pParent = nullptr;
	std::vector<std::unique_ptr<ProfileNode>> Children;
	std::unordered_map<StringHash, ProfileNode*> Map;
};

class Profiler
{
public:
	constexpr static int MAX_GPU_TIME_QUERIES = 512;
	constexpr static int QUERY_PAIR_NUM = 2;
	constexpr static int HEAP_SIZE = MAX_GPU_TIME_QUERIES * QUERY_PAIR_NUM;

	static Profiler* Get();

	void Initialize(GraphicsDevice* pParent);
	void Shutdown();

	void Begin(const char* pName, CommandContext* pContext = nullptr);
	void End();

	void Resolve(CommandContext* pContext);

	int32 GetNextTimerIndex();
	ID3D12QueryHeap* GetQueryHeap() const { return m_pQueryHeap.Get(); }
	ProfileNode* GetRootNode() const { return m_pRootBlock.get(); }
	void DrawImGui();
	uint32 GetFrameIndex() const { return m_FrameIndex; }

private:
	Profiler() = default;
	void DrawImGui(const ProfileNode* pNode);

	uint32 m_FrameIndex = 0;
	uint64 m_CpuTimestampFrequency = 0;

	int m_CurrentTimer = 0;
	int m_CurrentReadbackFrame = 0;
	RefCountPtr<ID3D12QueryHeap> m_pQueryHeap;
	RefCountPtr<Buffer> m_pReadBackBuffer;

	std::unique_ptr<ProfileNode> m_pRootBlock;
	ProfileNode* m_pCurrentBlock = nullptr;
	ProfileNode* m_pPreviousBlock = nullptr;
};

struct ScopeProfiler
{
	ScopeProfiler(const char* pName, CommandContext* pContext, bool enabled)
		: Enabled(enabled)
	{
		if(enabled)
			Profiler::Get()->Begin(pName, pContext);
	}

	~ScopeProfiler()
	{
		if(Enabled)
			Profiler::Get()->End();
	}
	bool Enabled;
};
