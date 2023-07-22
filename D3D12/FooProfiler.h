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
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.BeginTime));
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
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTime));
		data.CurrentRegion = region.Parent;
		check(data.Depth > 0);
		--data.Depth;
	}

	void BeginFrame()
	{
		if (!m_Paused)
			++m_FrameIndex;

		SampleHistory& data = GetCurrentData();
		data.CharIndex = 0;
		data.CurrentIndex = 0;

		for (auto& threadData : m_ThreadData)
			threadData.second.Head = 0xFFFFFFFF;
		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksBegin));
	}

	void EndFrame()
	{
		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksEnd));
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
		const char* pName;
		uint32 ThreadID = 0xFFFFFFFF;
		uint64 BeginTime = 0;
		uint64 EndTime = 0;
		uint32 Color = 0xFFFF00FF;
		uint32 Parent = 0xFFFFFFFF;
		uint32 Depth = 0;
		uint32 LineNumber = 0;
		const char* pFilePath = nullptr;
	};

	struct SampleHistory
	{
		uint64 TicksBegin;
		uint64 TicksEnd;
		std::array<SampleRegion, 1024> Regions;
		std::atomic<uint32> CurrentIndex = 0;
		std::atomic<uint32> CharIndex = 0;
		char StringBuffer[1 << 16];
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
