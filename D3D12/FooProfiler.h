#pragma once

#define FOO_SCOPE(...) FooProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__VA_ARGS__, __FILE__, __LINE__)

extern class FooProfiler gProfiler;

class FooProfiler
{
public:
	static constexpr int MAX_DEPTH = 32;
	static constexpr int STRING_BUFFER_SIZE = 1 < 16;
	static constexpr int MAX_NUM_REGIONS = 1024;


	FooProfiler()
	{
		m_ThreadData.reserve(128);
	}

	void BeginRegion(const char* pName, const Color& color = Colors::White)
	{
		SampleHistory& data = GetCurrentData();
		uint32 newIndex = data.CurrentIndex.fetch_add(1);
		check(newIndex < data.Regions.size());

		ThreadData& threadData = GetThreadData();
		check(threadData.StackDepth < ARRAYSIZE(threadData.RegionStack));

		threadData.RegionStack[threadData.StackDepth] = newIndex;
		threadData.StackDepth++;

		SampleRegion& newRegion = data.Regions[newIndex];
		newRegion.StackDepth = threadData.StackDepth;
		newRegion.ThreadID = Thread::GetCurrentId();
		newRegion.pName = StoreString(pName);
		newRegion.Color = Math::Pack_RGBA8_UNORM(color);
		QueryPerformanceCounter((LARGE_INTEGER*)(&newRegion.BeginTicks));
	}

	void SetFileInfo(const char* pFilePath, uint32 lineNumber)
	{
		SampleHistory& data = GetCurrentData();
		ThreadData& threadData = GetThreadData();

		SampleRegion& region = data.Regions[threadData.RegionStack[threadData.StackDepth]];
		region.pFilePath = pFilePath;
		region.LineNumber = lineNumber;
	}

	void EndRegion()
	{
		SampleHistory& data = GetCurrentData();
		ThreadData& threadData = GetThreadData();

		check(threadData.StackDepth > 0);
		--threadData.StackDepth;
		SampleRegion& region = data.Regions[threadData.RegionStack[threadData.StackDepth]];
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTicks));
	}

	void Tick()
	{
		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksEnd));

		for (auto& threadData : m_ThreadData)
			check(threadData.second.StackDepth == 0);

		if (!m_Paused)
			++m_FrameIndex;

		SampleHistory& data = GetCurrentData();
		data.CharIndex = 0;
		data.CurrentIndex = 0;

		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksBegin));
	}

	void DrawTimings();

private:
	const char* StoreString(const char* pText)
	{
		SampleHistory& data = GetCurrentData();
		uint32 len = (uint32)strlen(pText) + 1;
		uint32 offset = data.CharIndex.fetch_add(len);
		strcpy_s(&data.StringBuffer[offset], len, pText);
		return &data.StringBuffer[offset];
	}

	struct ThreadData
	{
		uint32 ThreadID = 0;
		uint32 StackDepth = 0;
		uint32 RegionStack[MAX_DEPTH];
	};

	ThreadData& GetThreadData()
	{
		std::lock_guard lock(m_ThreadIndexLock);
		ThreadData& data = m_ThreadData[Thread::GetCurrentId()];
		data.ThreadID = Thread::GetCurrentId();
		return data;
	}

	struct SampleRegion
	{
		const char* pName;									//< Name of the region
		uint32 ThreadID = 0xFFFFFFFF;						//< Thread ID of the thread that recorderd this region
		uint64 BeginTicks = 0;								//< The ticks at the start of this region
		uint64 EndTicks = 0;								//< The ticks at the end of this region
		uint32 Color = 0xFFFF00FF;							//< Color of region
		uint32 StackDepth = 0;								//< StackDepth of the region
		uint32 LineNumber = 0;								//< Line number of file in which this region is recorded
		const char* pFilePath = nullptr;					//< File path of file in which this region is recorded
	};

	struct SampleHistory
	{
		uint64 TicksBegin;									//< The start ticks of the frame on the main thread
		uint64 TicksEnd;									//< The end ticks of the frame on the main thread
		std::array<SampleRegion, MAX_NUM_REASONS> Regions;	//< All sample regions of the frame
		std::atomic<uint32> CurrentIndex = 0;				//< The index to the next free sample region
		std::atomic<uint32> CharIndex = 0;					//< The index to the next free char buffer
		char StringBuffer[STRING_BUFFER_SIZE];				//< Blob to store dynamic strings for the frame
	};

	SampleHistory& GetCurrentData()
	{
		return m_SampleHistory[m_FrameIndex % m_SampleHistory.size()];
	}

	SampleHistory& GetHistoryData(int numFrames)
	{
		numFrames = Math::Min(numFrames, (int)m_SampleHistory.size() - 2);
		int index = (m_FrameIndex + (int)m_SampleHistory.size() - 1 - numFrames) % (int)m_SampleHistory.size();
		return m_SampleHistory[index];
	}

	std::mutex m_ThreadIndexLock;
	std::unordered_map<uint32, ThreadData> m_ThreadData;

	bool m_Paused = false;
	uint32 m_FrameIndex = 0;
	std::array<SampleHistory, 4> m_SampleHistory;
};

struct FooProfileScope
{
	FooProfileScope(const char* pName, const char* pFilePath, uint32 lineNumber)
	{
		gProfiler.BeginRegion(pName);
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	FooProfileScope(const char* pName, const Color& color, const char* pFilePath, uint32 lineNumber)
	{
		gProfiler.BeginRegion(pName, color);
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	~FooProfileScope()
	{
		gProfiler.EndRegion();
	}
};
