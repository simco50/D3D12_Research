#pragma once
#include "Graphics/RHI/D3D.h"

#define GPU_PROFILE_SCOPE(name, commandlist)	PROFILE_GPU_SCOPE(name, (commandlist)->GetCommandList())
#define PROFILE_SCOPE(name)						PROFILE_CPU_SCOPE(name)

#ifndef WITH_PROFILING
#define WITH_PROFILING 1
#endif

#if WITH_PROFILING

/*
	General
*/

// Usage:
//		PROFILE_REGISTER_THREAD(const char* pName)
//		PROFILE_REGISTER_THREAD()
#define PROFILE_REGISTER_THREAD(...) gCPUProfiler.RegisterThread(__VA_ARGS__)

/// Usage:
//		PROFILE_FRAME()
#define PROFILE_FRAME() gCPUProfiler.Tick(); gGPUProfiler.Tick()

/*
	CPU Profiling
*/

// Usage:
//		PROFILE_CPU_SCOPE(const char* pName)
//		PROFILE_CPU_SCOPE()
#define PROFILE_CPU_SCOPE(...)							CPUProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__FUNCTION__, __FILE__, (uint16)__LINE__, __VA_ARGS__)

// Usage:
//		PROFILE_CPU_BEGIN(const char* pName)
//		PROFILE_CPU_BEGIN()
#define PROFILE_CPU_BEGIN(...)							gCPUProfiler.PushRegion(__VA_ARGS__)
// Usage:
//		PROFILE_CPU_END()
#define PROFILE_CPU_END()								gCPUProfiler.PopRegion()

/*
	GPU Profiling
*/

// Usage:
//		PROFILE_GPU_SCOPE(const char* pName, ID3D12GraphicsCommandList* pCommandList, uint32 queueIndex)
//		PROFILE_GPU_SCOPE(const char* pName, ID3D12GraphicsCommandList* pCommandList)
#define PROFILE_GPU_SCOPE(...)							GPUProfileScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__FUNCTION__, __FILE__, (uint16)__LINE__, __VA_ARGS__)

#else

#define PROFILE_REGISTER_THREAD(...)
#define PROFILE_FRAME()
#define PROFILE_CPU_SCOPE(...)
#define PROFILE_CPU_BEGIN(...)
#define PROFILE_CPU_END()
#define PROFILE_GPU_SCOPE(...)
#define PROFILE_GPU_BEGIN(...)
#define PROFILE_GPU_END()

#endif

// Simple Linear Allocator
class LinearAllocator
{
public:
	explicit LinearAllocator(uint32 size)
		: m_pData(new char[size]), m_Size(size), m_Offset(0)
	{
	}

	~LinearAllocator()
	{
		delete[] m_pData;
	}

	LinearAllocator(LinearAllocator&) = delete;
	LinearAllocator& operator=(LinearAllocator&) = delete;

	void Reset()
	{
		m_Offset = 0;
	}

	template<typename T, typename... Args>
	T* Allocate(Args... args)
	{
		void* pData = Allocate(sizeof(T));
		T* pValue = new (pData) T(std::forward<Args>(args)...);
		return pValue;
	}

	void* Allocate(uint32 size)
	{
		uint32 offset = m_Offset.fetch_add(size);
		check(offset + size <= m_Size);
		return m_pData + offset;
	}

	const char* String(const char* pStr)
	{
		uint32 len = (uint32)strlen(pStr) + 1;
		char* pData = (char*)Allocate(len);
		strcpy_s(pData, len, pStr);
		return pData;
	}

private:
	char* m_pData;
	uint32 m_Size;
	std::atomic<uint32> m_Offset;
};

template<typename T, uint32 N>
struct FixedStack
{
public:
	T& Pop()
	{
		check(Depth > 0);
		--Depth;
		return StackData[Depth];
	}

	T& Push()
	{
		Depth++;
		check(Depth < ARRAYSIZE(StackData));
		return StackData[Depth - 1];
	}

	T& Top()
	{
		check(Depth > 0);
		return StackData[Depth - 1];
	}

	uint32 GetSize() const { return Depth; }

private:
	uint32 Depth = 0;
	T StackData[N]{};
};

void DrawProfilerHUD();

//-----------------------------------------------------------------------------
// [SECTION] GPU Profiler
//-----------------------------------------------------------------------------

extern class GPUProfiler gGPUProfiler;

struct GPUProfilerCallbacks
{
	using EventBeginFn	= void(*)(const char* /*pName*/, ID3D12GraphicsCommandList* /*CommandList*/, void* /*pUserData*/);
	using EventEndFn	= void(*)(ID3D12GraphicsCommandList* /*CommandList*/, void* /*pUserData*/);

	EventBeginFn OnEventBegin	= nullptr;
	EventEndFn OnEventEnd		= nullptr;
	void* pUserData				= nullptr;
};


class GPUProfiler
{
public:
	void Initialize(ID3D12Device* pDevice, Span<ID3D12CommandQueue*> queues, uint32 sampleHistory, uint32 frameLatency, uint32 maxNumEvents, uint32 maxNumActiveCommandLists)
	{
		m_pResolveQueue = queues[0];
		m_FrameLatency = frameLatency;
		m_NumSampleHistory = sampleHistory;

		m_pSampleData = new EventFrame[sampleHistory];

		for (uint32 i = 0; i < sampleHistory; ++i)
		{
			EventFrame& frame = m_pSampleData[i];
			frame.Events.resize(maxNumEvents);
			frame.EventsPerQueue.resize(queues.GetSize());
		}

		m_CommandListData.Setup(maxNumActiveCommandLists);

		D3D12_QUERY_HEAP_DESC heapDesc{};
		heapDesc.Count = maxNumEvents * 2;
		heapDesc.NodeMask = 0x1;
		heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		pDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_pQueryHeap));

		for (uint16 queueIndex = 0; queueIndex < queues.GetSize(); ++queueIndex)
		{
			ID3D12CommandQueue* pQueue = queues[queueIndex];
			m_QueueIndexMap[pQueue] = (uint32)m_Queues.size();
			QueueInfo& queueInfo = m_Queues.emplace_back();
			uint32 size = ARRAYSIZE(queueInfo.Name);
			pQueue->GetPrivateData(WKPDID_D3DDebugObjectName, &size, queueInfo.Name);
			queueInfo.pQueue = pQueue;
			queueInfo.InitCalibration();
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc = queues[0]->GetDesc();
		m_pQueryFrames = new QueryFrame[frameLatency];
		for (uint32 i = 0; i < frameLatency; ++i)
		{
			QueryFrame& frame = m_pQueryFrames[i];
			frame.Events.resize(maxNumEvents);
			pDevice->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&frame.pCommandAllocator));
		}
		pDevice->CreateCommandList(0x1, queueDesc.Type, m_pQueryFrames[0].pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList));

		D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer((uint64)maxNumEvents * 2 * sizeof(uint64) * frameLatency);
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_pReadbackResource));
		void* pReadbackData = nullptr;
		m_pReadbackResource->Map(0, nullptr, &pReadbackData);
		m_pReadbackData = (uint64*)pReadbackData;

		pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pResolveFence));
		m_ResolveWaitHandle = CreateEventExA(nullptr, "Fence Event", 0, EVENT_ALL_ACCESS);
	}

	void Shutdown()
	{
		delete[] m_pSampleData;

		for (uint32 i = 0; i < m_FrameLatency; ++i)
			m_pQueryFrames[i].pCommandAllocator->Release();
		delete[] m_pQueryFrames;

		m_pQueryHeap->Release();
		m_pCommandList->Release();
		m_pReadbackResource->Release();
		m_pResolveFence->Release();
		CloseHandle(m_ResolveWaitHandle);
	}

	void BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, const char* pFilePath = "", uint32 lineNumber = 0)
	{
		if (m_EventCallback.OnEventBegin)
			m_EventCallback.OnEventBegin(pName, pCmd, m_EventCallback.pUserData);

		if (m_IsPaused)
			return;

		QueryFrame& queryFrame = GetQueryFrame();
		EventFrame& sampleData = GetSampleFrame();
		CommandListData::Data* pCmdData = m_CommandListData.Get(pCmd, true);

		// Allocate an event
		uint32 eventIndex = queryFrame.EventIndex.fetch_add(1);

		// Allocate a query
		uint32 queryIndex = queryFrame.QueryIndex.fetch_add(1);

		// Append a query to the commandlist
		CommandListData::Data::Query& cmdListQuery = pCmdData->Queries.emplace_back();
		cmdListQuery.EventIndex = eventIndex;
		cmdListQuery.IsBegin = true;
		cmdListQuery.QueryIndex = queryIndex;

		// Append an event in the query frame
		QueryFrame::Event& query = queryFrame.Events[eventIndex];
		query.QueryIndexBegin = queryIndex;

		// Append a event in the sample history 
		EventFrame::Event& event = sampleData.Events[eventIndex];
		event.Index = eventIndex;
		event.pName = sampleData.Allocator.String(pName);
		event.pFilePath = pFilePath;
		event.LineNumber = lineNumber;

		pCmd->EndQuery(m_pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
	}

	void EndEvent(ID3D12GraphicsCommandList* pCmd)
	{
		if (m_EventCallback.OnEventEnd)
			m_EventCallback.OnEventEnd(pCmd, m_EventCallback.pUserData);

		if (m_IsPaused)
			return;

		QueryFrame& queryFrame = GetQueryFrame();
		CommandListData::Data* pCmdData = m_CommandListData.Get(pCmd, true);

		// Allocate a query
		uint32 queryIndex = queryFrame.QueryIndex.fetch_add(1);

		// Append a query to the commandlist
		CommandListData::Data::Query& query = pCmdData->Queries.emplace_back();
		query.IsBegin = false;
		query.QueryIndex = queryIndex;

		pCmd->EndQuery(m_pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
	}

	void Tick()
	{
		// If the next frame is not finished resolving, wait for it here so the data can be read from before it's being reset
		QueryFrame& nextFrame = GetQueryFrame(m_FrameIndex + 1);
		if (!IsFenceComplete(nextFrame.FenceValue))
		{
			m_pResolveFence->SetEventOnCompletion(nextFrame.FenceValue, m_ResolveWaitHandle);
			WaitForSingleObject(m_ResolveWaitHandle, INFINITE);
		}

		while (m_FrameToReadback < m_FrameIndex)
		{
			QueryFrame& queryFrame = GetQueryFrame(m_FrameToReadback);
			EventFrame& sampleData = GetSampleFrame(m_FrameToReadback);
			if (IsFenceComplete(queryFrame.FenceValue))
			{
				sampleData.NumEvents = queryFrame.EventIndex;

				uint32 frameBit = m_FrameToReadback % m_FrameLatency;
				uint32 queryStart = frameBit * (uint32)queryFrame.Events.size() * 2;
				uint64* pQueries = m_pReadbackData + queryStart;
				for (uint32 i = 0; i < sampleData.NumEvents; ++i)
				{
					QueryFrame::Event& queryEvent = queryFrame.Events[i];
					EventFrame::Event& sampleRegion = sampleData.Events[i];
					sampleRegion.TicksBegin = pQueries[queryEvent.QueryIndexBegin];
					sampleRegion.TicksEnd = pQueries[queryEvent.QueryIndexEnd];
				}

				std::vector<EventFrame::Event>& events = sampleData.Events;
				std::sort(events.begin(), events.begin() + sampleData.NumEvents, [](const EventFrame::Event& a, const EventFrame::Event& b)
					{
						if (a.QueueIndex == b.QueueIndex)
						{
							if (a.TicksBegin == b.TicksBegin)
							{
								if (a.TicksEnd == b.TicksEnd)
									return a.Index < b.Index;
								return a.TicksEnd > b.TicksEnd;
							}
							return a.TicksBegin < b.TicksBegin;
						}
						return a.QueueIndex < b.QueueIndex;
					});

				uint32 eventStart = 0;
				for (uint32 queueIndex = 0; queueIndex < (uint32)m_Queues.size(); ++queueIndex)
				{
					uint32 eventEnd = eventStart;
					while (events[eventEnd].QueueIndex == queueIndex && eventEnd < sampleData.NumEvents)
						++eventEnd;

					if (eventStart == eventEnd)
						continue;

					sampleData.EventsPerQueue[queueIndex] = Span<const EventFrame::Event>(&events[eventStart], eventEnd - eventStart);

					FixedStack<uint32, 32> stack;
					for (uint32 i = eventStart; i < eventEnd; ++i)
					{
						EventFrame::Event& event = events[i];

						// While there is a parent and the current region starts after the parent ends, pop it off the stack
						while (stack.GetSize() > 0)
						{
							const EventFrame::Event& parent = events[stack.Top()];
							if (event.TicksBegin >= parent.TicksEnd || parent.TicksBegin == parent.TicksEnd)
							{
								stack.Pop();
							}
							else
							{
								check(event.TicksEnd <= parent.TicksEnd);
								break;
							}
						}

						// Set the region's depth
						event.Depth = stack.GetSize();
						stack.Push() = i;
					}

					eventStart = eventEnd;
				}
			}
			++m_FrameToReadback;
		}

		m_IsPaused = m_PauseQueued;
		if (m_IsPaused)
			return;

		m_CommandListData.Reset();

		{
			QueryFrame& queryFrame = GetQueryFrame();
			uint32 frameBit = m_FrameIndex % m_FrameLatency;
			uint32 queryStart = frameBit * (uint32)queryFrame.Events.size() * 2;
			uint32 numQueries = queryFrame.EventIndex * 2;
			m_pCommandList->ResolveQueryData(m_pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, numQueries, m_pReadbackResource, queryStart * sizeof(uint64));
			m_pCommandList->Close();
			ID3D12CommandList* pCmdLists[] = { m_pCommandList };
			m_pResolveQueue->ExecuteCommandLists(1, pCmdLists);
			m_pResolveQueue->Signal(m_pResolveFence, m_FrameIndex + 1);
			queryFrame.FenceValue = m_FrameIndex + 1;
		}

		++m_FrameIndex;

		{
			QueryFrame& queryFrame = GetQueryFrame();
			queryFrame.pCommandAllocator->Reset();
			m_pCommandList->Reset(queryFrame.pCommandAllocator, nullptr);
			queryFrame.EventIndex = 0;
			queryFrame.QueryIndex = 0;

			EventFrame& eventFrame = GetSampleFrame();
			eventFrame.Allocator.Reset();
			eventFrame.NumEvents = 0;
			for (uint32 i = 0; i < (uint32)m_Queues.size(); ++i)
				eventFrame.EventsPerQueue[i] = {};
		}
	}

	void ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists)
	{
		if (m_IsPaused)
			return;

		QueryFrame& queryFrame = GetQueryFrame();
		EventFrame& sampleFrame = GetSampleFrame();

		std::vector<uint32> eventStack;
		for (ID3D12CommandList* pCmd : commandLists)
		{
			CommandListData::Data* pEventData = m_CommandListData.Get(pCmd, false);
			if (pEventData)
			{
				for (CommandListData::Data::Query& query : pEventData->Queries)
				{
					if (query.IsBegin)
					{
						eventStack.push_back(query.EventIndex);
					}
					else
					{
						check(!eventStack.empty(), "Event Begin/End mismatch");
						uint32 eventIndex = eventStack.back();
						eventStack.pop_back();

						QueryFrame::Event& queryEvent = queryFrame.Events[eventIndex];
						EventFrame::Event& sampleEvent = sampleFrame.Events[eventIndex];

						queryEvent.QueryIndexEnd = query.QueryIndex;
						sampleEvent.QueueIndex = m_QueueIndexMap[pQueue];
					}
				}
				pEventData->Queries.clear();
			}
		}
		check(eventStack.empty(), "Forgot to End %d Events", eventStack.size());
	}

	void SetPaused(bool paused) { m_PauseQueued = paused; }

	// Data for a single frame of profiling events. On for each history frame
	struct EventFrame
	{
		EventFrame()
			: Allocator(1 << 14)
		{}

		struct Event
		{
			const char* pName = "";
			const char* pFilePath = "";
			uint64		TicksBegin = 0;
			uint64		TicksEnd = 0;
			uint32		LineNumber : 16;
			uint32		Index : 16;
			uint32		Depth : 8;
			uint32		QueueIndex : 8;
		};

		LinearAllocator Allocator;
		std::vector<Span<const Event>> EventsPerQueue;
		std::vector<Event> Events;
		uint32 NumEvents = 0;
	};

	// Data of a single GPU queue. Allows converting GPU timestamps to CPU timestamps
	class QueueInfo
	{
	public:
		void InitCalibration()
		{
			pQueue->GetClockCalibration(&GPUCalibrationTicks, &CPUCalibrationTicks);
			pQueue->GetTimestampFrequency(&GPUFrequency);
			QueryPerformanceFrequency((LARGE_INTEGER*)&CPUFrequency);
		}

		uint64 GpuToCpuTicks(uint64 gpuTicks) const
		{
			check(gpuTicks >= GPUCalibrationTicks);
			return CPUCalibrationTicks + (gpuTicks - GPUCalibrationTicks) * CPUFrequency / GPUFrequency;
		}

		float TicksToMS(uint64 ticks) const
		{
			return (float)ticks / GPUFrequency * 1000.0f;
		}

		ID3D12CommandQueue* pQueue = nullptr;				// The D3D queue object
		char Name[128];										// Name of the queue

	private:
		uint64 GPUCalibrationTicks = 0;						// The number of GPU ticks when the calibration was done
		uint64 CPUCalibrationTicks = 0;						// The number of CPU ticks when the calibration was done
		uint64 GPUFrequency = 0;							// The GPU tick frequency
		uint64 CPUFrequency = 0;							// The CPU tick frequency
	};

	Span<const QueueInfo> GetQueues() const { return m_Queues; }

	uint32 GetHistorySize() const { return m_NumSampleHistory; }

	Span<const EventFrame::Event> GetSamplesForQueue(const QueueInfo& queue, uint32 frame) const
	{
		uint32 queueIndex = m_QueueIndexMap.at(queue.pQueue);
		const EventFrame& frameData = GetSampleFrame(frame);
		return frameData.EventsPerQueue[queueIndex];
	}

	void SetEventCallback(GPUProfilerCallbacks inCallbacks) { m_EventCallback = inCallbacks; }

private:
	bool IsFenceComplete(uint64 fenceValue)
	{
		if (fenceValue <= m_LastCompletedFence)
			return true;
		m_LastCompletedFence = Math::Max(m_pResolveFence->GetCompletedValue(), m_LastCompletedFence);
		return fenceValue <= m_LastCompletedFence;
	}

	EventFrame* m_pSampleData = nullptr;
	uint32 m_NumSampleHistory = 0;
	const EventFrame& GetSampleFrame(uint32 frameIndex) const { return m_pSampleData[frameIndex % m_NumSampleHistory]; }
	EventFrame& GetSampleFrame(uint32 frameIndex) { return m_pSampleData[frameIndex % m_NumSampleHistory]; }
	EventFrame& GetSampleFrame() { return GetSampleFrame(m_FrameIndex); }

	// Data for a single frame of GPU queries. One for each frame latency
	struct QueryFrame
	{
		struct Event
		{
			uint32 QueryIndexBegin : 16;
			uint32 QueryIndexEnd : 16;
		};

		ID3D12CommandAllocator* pCommandAllocator = nullptr;
		uint64						FenceValue = 0;
		std::atomic<uint32>			EventIndex = 0;
		std::atomic<uint32>			QueryIndex = 0;
		std::vector<Event>			Events;
	};
	QueryFrame& GetQueryFrame(uint32 frameIndex) { return m_pQueryFrames[frameIndex % m_FrameLatency]; }
	QueryFrame& GetQueryFrame() { return GetQueryFrame(m_FrameIndex); }
	QueryFrame* m_pQueryFrames = nullptr;
	uint32 m_FrameLatency = 0;

	// Query data for each commandlist
	class CommandListData
	{
	public:
		struct Data
		{
			struct Query
			{
				uint32 QueryIndex : 16;
				uint32 EventIndex : 15;
				uint32 IsBegin : 1;
			};
			std::vector<Query> Queries;
		};

		void Setup(uint32 maxCommandLists)
		{
			InitializeSRWLock(&m_CommandListMapLock);
			m_CommandListData.resize(maxCommandLists);
		}

		Data* Get(ID3D12CommandList* pCmd, bool createIfNotFound)
		{
			AcquireSRWLockShared(&m_CommandListMapLock);
			auto it = m_CommandListMap.find(pCmd);
			uint32 index = 0xFFFFFFFF;
			if (it != m_CommandListMap.end())
				index = it->second;
			ReleaseSRWLockShared(&m_CommandListMapLock);
			if (createIfNotFound && index == 0xFFFFFFFF)
			{
				AcquireSRWLockExclusive(&m_CommandListMapLock);
				index = (uint32)m_CommandListMap.size();
				m_CommandListMap[pCmd] = index;
				ReleaseSRWLockExclusive(&m_CommandListMapLock);
			}
			if (index == 0xFFFFFFFF)
				return nullptr;
			check(index < m_CommandListData.size());
			return &m_CommandListData[index];
		}

		void Reset()
		{
			for (Data& data : m_CommandListData)
				check(data.Queries.empty(), "The Queries inside the commandlist is not empty. This is because ExecuteCommandLists was not called with this commandlist.");
			m_CommandListMap.clear();
		}

	private:
		SRWLOCK m_CommandListMapLock{};
		std::unordered_map<ID3D12CommandList*, uint32> m_CommandListMap;
		std::vector<Data> m_CommandListData;
	} m_CommandListData{};

	uint32 m_FrameToReadback = 0;
	uint32 m_FrameIndex = 0;

	std::vector<QueueInfo>							m_Queues;
	std::unordered_map<ID3D12CommandQueue*, uint32> m_QueueIndexMap;
	GPUProfilerCallbacks							m_EventCallback;

	ID3D12GraphicsCommandList* m_pCommandList = nullptr;
	ID3D12QueryHeap* m_pQueryHeap = nullptr;
	ID3D12Resource* m_pReadbackResource = nullptr;
	uint64* m_pReadbackData = nullptr;
	ID3D12CommandQueue* m_pResolveQueue = nullptr;
	ID3D12Fence* m_pResolveFence = nullptr;
	HANDLE						m_ResolveWaitHandle = nullptr;
	uint64						m_LastCompletedFence = 0;
	bool						m_IsPaused = false;
	bool						m_PauseQueued = false;
};


// Helper RAII-style structure to push and pop a GPU sample region
struct GPUProfileScope
{
	GPUProfileScope(const char* pFunction, const char* pFilePath, uint16 lineNr, const char* pName, ID3D12GraphicsCommandList* pCmd)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pName, pFilePath, lineNr);
	}

	GPUProfileScope(const char* pFunction, const char* pFilePath, uint16 lineNr, ID3D12GraphicsCommandList* pCmd)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pFunction, pFilePath, lineNr);
	}

	~GPUProfileScope()
	{
		gGPUProfiler.EndEvent(pCmd);
	}

	GPUProfileScope(const GPUProfileScope&) = delete;
	GPUProfileScope& operator=(const GPUProfileScope&) = delete;

private:
	ID3D12GraphicsCommandList* pCmd;
};


//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------

// Global CPU Profiler
extern class CPUProfiler gCPUProfiler;

struct CPUProfilerCallbacks
{
	using EventBeginFn = void(*)(const char* /*pName*/, void* /*pUserData*/);
	using EventEndFn = void(*)(void* /*pUserData*/);

	EventBeginFn OnEventBegin = nullptr;
	EventEndFn OnEventEnd = nullptr;
	void* pUserData = nullptr;
};

// CPU Profiler
// Also responsible for updating GPU profiler
// Also responsible for drawing HUD
class CPUProfiler
{
public:
	void Initialize(uint32 historySize, uint32 maxSamples)
	{
		Shutdown();

		m_pSampleData = new SampleHistory[historySize];
		m_HistorySize = historySize;

		for(uint32 i = 0; i < historySize; ++i)
			m_pSampleData[i].Regions.resize(maxSamples);
	}

	void Shutdown()
	{
		delete[] m_pSampleData;
	}

	// Start and push a region on the current thread
	void PushRegion(const char* pName, const char* pFilePath = nullptr, uint16 lineNumber = 0)
	{
		for (CPUProfilerCallbacks& callback : m_EventCallbacks)
			callback.OnEventBegin(pName, callback.pUserData);

		if (m_Paused)
			return;

		SampleHistory& data = GetData();
		uint32 newIndex = data.NumRegions.fetch_add(1);
		check(newIndex < data.Regions.size());

		TLS& tls = GetTLS();

		SampleRegion& newRegion = data.Regions[newIndex];
		newRegion.Depth = (uint16)tls.RegionStack.GetSize();
		newRegion.ThreadIndex = tls.ThreadIndex;
		newRegion.pName = data.Allocator.String(pName);
		newRegion.pFilePath = pFilePath;
		newRegion.LineNumber = lineNumber;
		QueryPerformanceCounter((LARGE_INTEGER*)(&newRegion.BeginTicks));

		tls.RegionStack.Push() = newIndex;
	}

	// End and pop the last pushed region on the current thread
	void PopRegion()
	{
		for (CPUProfilerCallbacks& callback : m_EventCallbacks)
			callback.OnEventEnd(callback.pUserData);

		if (m_Paused)
			return;

		SampleRegion& region = GetData().Regions[GetTLS().RegionStack.Pop()];
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTicks));
	}

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick()
	{
		m_Paused = m_QueuedPaused;

		if (m_Paused)
			return;

		if (m_FrameIndex)
			PopRegion();

		// Check if all threads have ended all open sample regions
		for (auto& threadData : m_ThreadData)
			check(threadData.pTLS->RegionStack.GetSize() == 0);

		++m_FrameIndex;

		SampleHistory& newData = GetData();
		newData.Allocator.Reset();
		newData.NumRegions = 0;

		PushRegion("CPU Frame");
	}

	// Initialize a thread with an optional name
	void RegisterThread(const char* pName = nullptr)
	{
		TLS& tls = GetTLSUnsafe();
		check(!tls.IsInitialized);
		tls.IsInitialized = true;
		std::scoped_lock lock(m_ThreadDataLock);
		tls.ThreadIndex = (uint32)m_ThreadData.size();
		ThreadData& data = m_ThreadData.emplace_back();

		// If the name is not provided, retrieve it using GetThreadDescription()
		if (pName)
		{
			strcpy_s(data.Name, ARRAYSIZE(data.Name), pName);
		}
		else
		{
			PWSTR pDescription = nullptr;
			VERIFY_HR(::GetThreadDescription(GetCurrentThread(), &pDescription));
			size_t converted = 0;
			check(wcstombs_s(&converted, data.Name, ARRAYSIZE(data.Name), pDescription, ARRAYSIZE(data.Name)) == 0);
		}
		data.ThreadID = GetCurrentThreadId();
		data.pTLS = &tls;
	}

	// Structure representating a single sample region
	struct SampleRegion
	{
		const char* pName		= "";			// Name of the region
		const char* pFilePath	= nullptr;		// File path of file in which this region is recorded
		uint64 BeginTicks		= 0;			// The ticks at the start of this region
		uint64 EndTicks			= 0;			// The ticks at the end of this region
		uint32 ThreadIndex		: 11;			// Thread Index of the thread that recorderd this region
		uint32 LineNumber		: 16;			// Line number of file in which this region is recorded
		uint32 Depth			: 5;			// Depth of the region
	};

	// Struct containing all sampling data of a single frame
	struct SampleHistory
	{
		static constexpr uint32 ALLOCATOR_SIZE = 1 << 14;

		SampleHistory()
			: Allocator(ALLOCATOR_SIZE)
		{}

		std::vector<SampleRegion> Regions;		// All sample regions of the frame
		std::atomic<uint32> NumRegions = 0;		// The number of regions
		LinearAllocator	Allocator;				// Scratch allocator storing all dynamic allocations of the frame
	};

	// Thread-local storage to keep track of current depth and region stack
	struct TLS
	{
		static constexpr int MAX_STACK_DEPTH = 32;

		uint32 ThreadIndex = 0;
		FixedStack<uint32, MAX_STACK_DEPTH> RegionStack;
		bool IsInitialized = false;
	};

	// Structure describing a registered thread
	struct ThreadData
	{
		char Name[128]	{};
		uint32 ThreadID = 0;
		const TLS* pTLS = nullptr;
	};

	// Iterate over all frames
	template<typename Fn>
	void ForEachFrame(Fn&& fn) const
	{
		// Start from the oldest history frame
		uint32 currentIndex = m_FrameIndex < m_HistorySize ? 0 : m_FrameIndex - (uint32)m_HistorySize + 1;
		for (currentIndex; currentIndex < m_FrameIndex; ++currentIndex)
		{
			const SampleHistory& data = m_pSampleData[currentIndex % m_HistorySize];
			fn(currentIndex, Span<const SampleRegion>(data.Regions.data(), data.NumRegions));
		}
	}

	// Get the ticks range of the history
	void GetHistoryRange(uint64& ticksMin, uint64& ticksMax) const
	{
		uint32 oldestFrameIndex = (m_FrameIndex + 1) % m_HistorySize;
		ticksMin = m_pSampleData[oldestFrameIndex].Regions[0].BeginTicks;
		uint32 youngestFrameIndex = (m_FrameIndex + (uint32)m_HistorySize - 1) % m_HistorySize;
		ticksMax = m_pSampleData[youngestFrameIndex].Regions[0].EndTicks;
	}

	void RegisterEventCallbacks(CPUProfilerCallbacks inCallbacks)
	{
		m_EventCallbacks.push_back(inCallbacks);
	}

	Span<const ThreadData> GetThreads() const { return m_ThreadData; }

	void SetPaused(bool paused) { m_QueuedPaused = paused; }
	bool IsPaused() const { return m_Paused; }

private:
	// Retrieve thread-local storage without initialization
	static TLS& GetTLSUnsafe()
	{
		static thread_local TLS tls;
		return tls;
	}

	// Retrieve the thread-local storage
	TLS& GetTLS()
	{
		TLS& tls = GetTLSUnsafe();
		if (!tls.IsInitialized)
			RegisterThread();
		return tls;
	}

	// Return the sample data of the current frame
	SampleHistory& GetData()
	{
		return m_pSampleData[m_FrameIndex % m_HistorySize];
	}

	std::vector<CPUProfilerCallbacks> m_EventCallbacks;

	std::mutex m_ThreadDataLock;							// Mutex for accesing thread data
	std::vector<ThreadData> m_ThreadData;					// Data describing each registered thread

	SampleHistory* m_pSampleData = nullptr;					// Per-frame data
	uint32 m_HistorySize = 0;								// History size
	uint32 m_FrameIndex = 0;								// The current frame index
	bool m_Paused = false;									// The current pause state
	bool m_QueuedPaused = false;							// The queued pause state
};


// Helper RAII-style structure to push and pop a CPU sample region
struct CPUProfileScope
{
	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint16 lineNumber, const char* pName)
	{
		gCPUProfiler.PushRegion(pName, pFilePath, lineNumber);
	}

	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint16 lineNumber)
	{
		gCPUProfiler.PushRegion(pFunctionName, pFilePath, lineNumber);
	}

	~CPUProfileScope()
	{
		gCPUProfiler.PopRegion();
	}

	CPUProfileScope(const CPUProfileScope&) = delete;
	CPUProfileScope& operator=(const CPUProfileScope&) = delete;
};
