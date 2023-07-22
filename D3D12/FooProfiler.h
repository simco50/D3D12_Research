#pragma once

#define FOO_SCOPE(...) FooProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__VA_ARGS__, __FILE__, __LINE__)

extern class FooProfiler gProfiler;

class FooProfiler
{
public:
	FooProfiler()
	{
		m_SampleRegions.resize(1024);
		m_ThreadData.reserve(128);
	}

	void BeginRegion(const char* pName, const Color& color = Colors::White)
	{
		uint32 regionIndex = AllocateRegion();
		SampleRegion& region = m_SampleRegions[regionIndex];
		region.pName = pName;
		region.Color = Math::Pack_RGBA8_UNORM(color);
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.BeginTime));
	}

	void SetFileInfo(const char* pFilePath, uint32 lineNumber)
	{
		ThreadData& data = GetThreadData();
		check(data.CurrentRegion != 0xFFFFFFFF);
		SampleRegion& region = m_SampleRegions[data.CurrentRegion];
		region.pFilePath = pFilePath;
		region.LineNumber = lineNumber;
	}

	void EndRegion()
	{
		ThreadData& data = GetThreadData();
		check(data.CurrentRegion != 0xFFFFFFFF);
		SampleRegion& region = m_SampleRegions[data.CurrentRegion];
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTime));
		data.CurrentRegion = region.Parent;
		check(data.Depth > 0);
		--data.Depth;
	}

	void BeginFrame()
	{
		m_SampleRegionIndex = 0;
		m_ThreadData.clear();
		QueryPerformanceCounter((LARGE_INTEGER*)(&m_SampleRegions[GetThreadData().Head].BeginTime));
	}

	void EndFrame()
	{
		QueryPerformanceCounter((LARGE_INTEGER*)(&m_SampleRegions[GetThreadData().Head].EndTime));
	}

	void DrawTimings();

private:

	struct ThreadData
	{
		uint32 Head = 0;
		uint32 CurrentRegion = 0;
		uint32 Depth = 0;
		uint32 ThreadID = 0;
		uint32 LastIndex = 0;
	};

	ThreadData& GetThreadData()
	{
		std::lock_guard lock(m_ThreadIndexLock);
		if (m_ThreadData.find(Thread::GetCurrentId()) == m_ThreadData.end())
		{
			ThreadData& data = m_ThreadData[Thread::GetCurrentId()];
			data.Head = m_SampleRegionIndex.fetch_add(1);
			data.CurrentRegion = data.Head;
			data.ThreadID = Thread::GetCurrentId();
			data.LastIndex = data.Head;
			m_SampleRegions[data.Head].pName = "HEAD";
			return data;
		}

		return m_ThreadData[Thread::GetCurrentId()];
	}

	struct SampleRegion
	{
		const char* pName;
		const char* pFilePath = nullptr;
		uint64 BeginTime = 0;
		uint64 EndTime = 0;
		uint32 Color = 0xFFFF00FF;
		uint32 Parent = 0xFFFFFFFF;
		uint32 Depth = 0;
		uint32 LineNumber = 0;
		uint32 Next = 0xFFFFFFFF;
	};

	uint32 GetFirstRegion(uint32 threadId) const
	{
		const ThreadData& data = m_ThreadData.at(threadId);
		return m_SampleRegions[data.Head].Next;
	}

	uint32 AllocateRegion()
	{
		ThreadData& threadData = GetThreadData();

		uint32 newIndex = m_SampleRegionIndex.fetch_add(1);
		check(newIndex < m_SampleRegions.size());

		m_SampleRegions[threadData.LastIndex].Next = newIndex;
		threadData.LastIndex = newIndex;

		SampleRegion& newRegion = m_SampleRegions[newIndex];
		newRegion.Parent = threadData.CurrentRegion;
		newRegion.Depth = threadData.Depth;
		threadData.CurrentRegion = newIndex;
		threadData.Depth++;
		return newIndex;
	}

	std::mutex m_ThreadIndexLock;
	std::unordered_map<uint32, ThreadData> m_ThreadData;

	std::atomic<uint32> m_SampleRegionIndex = 0;
	std::vector<SampleRegion> m_SampleRegions;
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
