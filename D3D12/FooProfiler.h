#pragma once

// FOO_SCOPE() or FOO_SCOPE(name) or FOO_SCOPE(name, color) or FOO_SCOPE(color)
#define FOO_SCOPE(...) FooProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define FOO_REGISTER_THREAD(...) gProfiler.RegisterThread(__VA_ARGS__)
#define FOO_FRAME() gProfiler.Tick();

extern class FooProfiler gProfiler;

class FooProfiler
{
public:
	static constexpr int REGION_HISTORY = 4;
	static constexpr int MAX_DEPTH = 32;
	static constexpr int STRING_BUFFER_SIZE = 1 << 16;
	static constexpr int MAX_NUM_REGIONS = 1024;

	FooProfiler() = default;

	void BeginRegion(const char* pName, const Color& color)
	{
		SampleHistory& data = GetCurrentData();
		uint32 newIndex = data.CurrentIndex.fetch_add(1);
		check(newIndex < data.Regions.size());

		TLS& tls = GetTLS();
		check(tls.Depth < ARRAYSIZE(tls.RegionStack));

		SampleRegion& newRegion = data.Regions[newIndex];
		newRegion.Depth = tls.Depth;
		newRegion.ThreadIndex = tls.ThreadIndex;
		newRegion.pName = StoreString(pName);
		newRegion.Color = Math::Pack_RGBA8_UNORM(color);
		QueryPerformanceCounter((LARGE_INTEGER*)(&newRegion.BeginTicks));

		tls.RegionStack[tls.Depth] = newIndex;
		tls.Depth++;
	}

	void BeginRegion(const char* pName)
	{
		// Add a region and inherit the color
		TLS& tls = GetTLS();
		check(tls.Depth < ARRAYSIZE(tls.RegionStack));
		Color color(0.7f, 0.7f, 0.7f, 1.0f);
		if(tls.Depth > 0)
		{
			const SampleHistory& data = GetCurrentData();
			color = data.Regions[tls.RegionStack[tls.Depth - 1]].Color;
		}
		BeginRegion(pName, color);
	}

	void SetFileInfo(const char* pFilePath, uint32 lineNumber)
	{
		SampleHistory& data = GetCurrentData();
		TLS& tls = GetTLS();

		SampleRegion& region = data.Regions[tls.RegionStack[tls.Depth - 1]];
		region.pFilePath = pFilePath;
		region.LineNumber = lineNumber;
	}

	void EndRegion()
	{
		SampleHistory& data = GetCurrentData();
		TLS& tls = GetTLS();

		check(tls.Depth > 0);
		--tls.Depth;
		SampleRegion& region = data.Regions[tls.RegionStack[tls.Depth]];
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTicks));
	}

	void Tick()
	{
		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksEnd));

		for (auto& threadData : m_ThreadData)
			check(threadData.pTLS->Depth == 0);

		if (!m_Paused)
			++m_FrameIndex;

		SampleHistory& data = GetCurrentData();
		data.CharIndex = 0;
		data.CurrentIndex = 0;

		QueryPerformanceCounter((LARGE_INTEGER*)(&GetCurrentData().TicksBegin));
	}

	void RegisterThread(const char* pName = nullptr)
	{
		TLS& tls = GetTLSUnsafe();
		check(!tls.IsInitialized);
		tls.IsInitialized = true;
		std::scoped_lock lock(m_ThreadDataLock);
		tls.ThreadIndex = (uint32)m_ThreadData.size();
		ThreadData& data = m_ThreadData.emplace_back();
		if (pName)
		{
			data.Name = pName;
		}
		else
		{
			PWSTR pDescription = nullptr;
			GetThreadDescription(GetCurrentThread(), &pDescription);
			data.Name = UNICODE_TO_MULTIBYTE(pDescription);
		}
		data.ThreadID = GetCurrentThreadId();
		data.pTLS = &tls;
	}

	void DrawHUD();

private:
	const char* StoreString(const char* pText)
	{
		SampleHistory& data = GetCurrentData();
		uint32 len = (uint32)strlen(pText) + 1;
		uint32 offset = data.CharIndex.fetch_add(len);
		check(offset + len <= ARRAYSIZE(data.StringBuffer));
		strcpy_s(data.StringBuffer + offset, len, pText);
		return &data.StringBuffer[offset];
	}

	struct TLS
	{
		uint32 ThreadIndex = 0;
		uint32 Depth = 0;
		uint32 RegionStack[MAX_DEPTH];
		bool IsInitialized = false;
	};

	TLS& GetTLSUnsafe()
	{
		static thread_local TLS tls;
		return tls;
	}

	TLS& GetTLS()
	{
		TLS& tls = GetTLSUnsafe();
		if (!tls.IsInitialized)
			RegisterThread();
		return tls;
	}

	struct SampleRegion
	{
		const char* pName;									//< Name of the region
		uint32 ThreadIndex = 0xFFFFFFFF;					//< Thread Index of the thread that recorderd this region
		uint64 BeginTicks = 0;								//< The ticks at the start of this region
		uint64 EndTicks = 0;								//< The ticks at the end of this region
		uint32 Color = 0xFFFF00FF;							//< Color of region
		uint32 Depth = 0;									//< Depth of the region
		uint32 LineNumber = 0;								//< Line number of file in which this region is recorded
		const char* pFilePath = nullptr;					//< File path of file in which this region is recorded
	};

	struct SampleHistory
	{
		uint64 TicksBegin;									//< The start ticks of the frame on the main thread
		uint64 TicksEnd;									//< The end ticks of the frame on the main thread
		std::array<SampleRegion, MAX_NUM_REGIONS> Regions;	//< All sample regions of the frame
		std::atomic<uint32> CurrentIndex = 0;				//< The index to the next free sample region
		std::atomic<uint32> CharIndex = 0;					//< The index to the next free char buffer
		char StringBuffer[STRING_BUFFER_SIZE];				//< Blob to store dynamic strings for the frame
	};

	SampleHistory& GetCurrentData()
	{
		return m_SampleHistory[m_FrameIndex % m_SampleHistory.size()];
	}

	template<typename Fn>
	void ForEachHistory(Fn&& fn) const
	{
		uint32 currentIndex = (m_FrameIndex + 1) % (uint32)m_SampleHistory.size();
		while (currentIndex != m_FrameIndex % m_SampleHistory.size() && currentIndex < m_FrameIndex)
		{
			fn(m_SampleHistory[currentIndex]);
			currentIndex = (currentIndex + 1) % (uint32)m_SampleHistory.size();
		}
	}

	const SampleHistory& GetHistory() const
	{
		uint32 currentIndex = (m_FrameIndex + 1) % (uint32)m_SampleHistory.size();
		return m_SampleHistory[currentIndex];
	}

	struct ThreadData
	{
		std::string Name = "";
		uint32 ThreadID = 0;
		const TLS* pTLS = nullptr;
	};

	std::mutex m_ThreadDataLock;
	std::vector<ThreadData> m_ThreadData;

	bool m_Paused = false;
	uint32 m_FrameIndex = 0;
	std::array<SampleHistory, REGION_HISTORY> m_SampleHistory;
};

struct FooProfileScope
{
	// Name + Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName, const Color& color)
	{
		gProfiler.BeginRegion(pName ? pName : pFunctionName, color);
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	// Just Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const Color& color)
	{
		gProfiler.BeginRegion(pFunctionName, color);
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	// Just Name
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName)
	{
		gProfiler.BeginRegion(pName);
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	// No Name or Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber)
	{
		gProfiler.BeginRegion(pFunctionName);
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	~FooProfileScope()
	{
		gProfiler.EndRegion();
	}
};
