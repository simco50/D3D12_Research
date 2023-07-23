#pragma once

#define FOO_SCOPE(...) FooProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__VA_ARGS__, __FILE__, __LINE__)

extern class FooProfiler gProfiler;

class FooProfiler
{
public:
	FooProfiler()
	{
		m_ThreadData.reserve(128);
	}

	void BeginRegion(const char* pName, const Color& color = Colors::White)
	{
		uint32 regionIndex = AllocateRegion();
		SampleHistory& data = GetCurrentData();
		SampleRegion& region = data.Regions[regionIndex];
		region.pName = StoreString(pName);
		region.Color = Math::Pack_RGBA8_UNORM(color);
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.BeginTicks));
	}

	void SetFileInfo(const char* pFilePath, uint32 lineNumber)
	{
		ThreadData& data = GetThreadData();
		check(data.CurrentRegion != 0xFFFFFFFF);
		SampleRegion& region = GetCurrentData().Regions[data.CurrentRegion];
		region.pFilePath = pFilePath;
		region.LineNumber = lineNumber;
	}

	void EndRegion()
	{
		ThreadData& data = GetThreadData();
		check(data.CurrentRegion != 0xFFFFFFFF);
		SampleRegion& region = GetCurrentData().Regions[data.CurrentRegion];
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTicks));
		data.CurrentRegion = region.Parent;
		check(data.Depth > 0);
		--data.Depth;
	}

	void Tick()
	{
		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksEnd));

		if (!m_Paused)
			++m_FrameIndex;

		SampleHistory& data = GetCurrentData();
		data.CharIndex = 0;
		data.CurrentIndex = 0;

		for (auto& threadData : m_ThreadData)
			threadData.second.Head = 0xFFFFFFFF;
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
		uint32 Head = 0xFFFFFFFF;
		uint32 CurrentRegion = 0xFFFFFFFF;
		uint32 Depth = 0;
		uint32 ThreadID = 0;
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
		const char* pName;						// Name of the region
		uint32 ThreadID = 0xFFFFFFFF;			// Thread ID of the thread that recorderd this region
		uint64 BeginTicks = 0;					// The ticks at the start of this region
		uint64 EndTicks = 0;					// The ticks at the end of this region
		uint32 Color = 0xFFFF00FF;				// Color of region
		uint32 Parent = 0xFFFFFFFF;				// Parent of region
		uint32 Depth = 0;						// Depth of the region
		uint32 LineNumber = 0;					// Line number of file in which this region is recorded
		const char* pFilePath = nullptr;		// File path of file in which this region is recorded
	};

	struct SampleHistory
	{
		uint64 TicksBegin;						// The start ticks of the frame on the main thread
		uint64 TicksEnd;						// The end ticks of the frame on the main thread
		std::array<SampleRegion, 1024> Regions;	// All sample regions of the frame
		std::atomic<uint32> CurrentIndex = 0;	// The index to the next free sample region
		std::atomic<uint32> CharIndex = 0;		// The index to the next free char buffer
		char StringBuffer[1 << 16];				// Blob to store dynamic strings for the frame
	};

	uint32 AllocateRegion()
	{
		ThreadData& threadData = GetThreadData();
		SampleHistory& data = GetCurrentData();

		uint32 newIndex = data.CurrentIndex.fetch_add(1);
		check(newIndex < data.Regions.size());

		SampleRegion& newRegion = data.Regions[newIndex];
		newRegion.Parent = threadData.CurrentRegion;
		newRegion.Depth = threadData.Depth;
		newRegion.ThreadID = Thread::GetCurrentId();
		threadData.CurrentRegion = newIndex;
		threadData.Head = threadData.Head == 0xFFFFFFFF ? newIndex : threadData.Head;
		threadData.Depth++;
		return newIndex;
	}

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
