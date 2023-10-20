#pragma once
#include "Graphics/RHI/D3D.h"

#define GPU_PROFILE_SCOPE(commandlist, ...)		PROFILE_GPU_SCOPE((commandlist).GetCommandList(), __VA_ARGS__)
#define PROFILE_SCOPE(...)						PROFILE_CPU_SCOPE(__VA_ARGS__)

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

/// Usage:
///		PROFILE_EXECUTE_COMMANDLISTS(ID3D12CommandQueue* pQueue, Span<ID3D12CommandLists*> commandLists)
#define PROFILE_EXECUTE_COMMANDLISTS(queue, cmdlists)	gGPUProfiler.ExecuteCommandLists(queue, cmdlists)

/*
	CPU Profiling
*/

// Usage:
//		PROFILE_CPU_SCOPE(const char* pName)
//		PROFILE_CPU_SCOPE()
#define PROFILE_CPU_SCOPE(...)							CPUProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

// Usage:
//		PROFILE_CPU_BEGIN(const char* pName)
//		PROFILE_CPU_BEGIN()
#define PROFILE_CPU_BEGIN(...)							gCPUProfiler.BeginEvent(__VA_ARGS__)
// Usage:
//		PROFILE_CPU_END()
#define PROFILE_CPU_END()								gCPUProfiler.EndEvent()

/*
	GPU Profiling
*/

// Usage:
//		PROFILE_GPU_SCOPE(ID3D12GraphicsCommandList* pCommandList, const char* pName)
//		PROFILE_GPU_SCOPE(ID3D12GraphicsCommandList* pCommandList)
#define PROFILE_GPU_SCOPE(...)							GPUProfileScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

// Usage:
//		PROFILE_GPU_BEGIN(const char* pName, ID3D12GraphicsCommandList* pCommandList)
#define PROFILE_GPU_BEGIN(name, cmdlist)				gGPUProfiler.BeginEvent(name, cmdlist, __FILE__, __LINE__)

// Usage:
//		PROFILE_GPU_END(ID3D12GraphicsCommandList* pCommandList)
#define PROFILE_GPU_END(cmdlist)						gGPUProfiler.EndEvent(cmdlist)


#else

#define PROFILE_REGISTER_THREAD(...)
#define PROFILE_FRAME()
#define PROFILE_EXECUTE_COMMANDLISTS(...)

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

	EventBeginFn	OnEventBegin	= nullptr;
	EventEndFn		OnEventEnd		= nullptr;
	void*			pUserData		= nullptr;
};


class GPUProfiler
{
public:
	void Initialize(ID3D12Device* pDevice, Span<ID3D12CommandQueue*> queues, uint32 sampleHistory, uint32 frameLatency, uint32 maxNumEvents, uint32 maxNumActiveCommandLists);
	void Shutdown();

	// Allocate and record a GPU event on the commandlist
	void BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, const char* pFilePath = "", uint32 lineNumber = 0);

	// Record a GPU event end on the commandlist
	void EndEvent(ID3D12GraphicsCommandList* pCmd);

	// Resolve and tick frame
	void Tick();

	// Notify profiler that these commandlists are executed on a particular queue
	void ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists);

	void SetPaused(bool paused) { m_PauseQueued = paused; }

	// Data for a single frame of profiling events. On for each history frame
	struct EventFrame
	{
		EventFrame()
			: Allocator(1 << 14)
		{}

		struct Event
		{
			const char* pName		= "";
			const char* pFilePath	= "";
			uint64		TicksBegin	= 0;
			uint64		TicksEnd	= 0;
			uint32		LineNumber	: 16;
			uint32		Index		: 16;
			uint32		Depth		: 8;
			uint32		QueueIndex	: 8;
			uint32		padding		: 16;
		};
		static_assert(sizeof(Event) == sizeof(uint32) * 10);

		LinearAllocator					Allocator;
		std::vector<Span<const Event>>	EventsPerQueue;
		std::vector<Event>				Events;
		uint32 NumEvents				= 0;
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

		ID3D12CommandQueue* pQueue = nullptr;	// The D3D queue object
		char Name[128];							// Name of the queue

	private:
		uint64 GPUCalibrationTicks	= 0;		// The number of GPU ticks when the calibration was done
		uint64 CPUCalibrationTicks	= 0;		// The number of CPU ticks when the calibration was done
		uint64 GPUFrequency			= 0;		// The GPU tick frequency
		uint64 CPUFrequency			= 0;		// The CPU tick frequency
	};

	Span<const QueueInfo> GetQueues() const { return m_Queues; }

	uint32 GetHistorySize() const { return m_NumSampleHistory; }

	Span<const EventFrame::Event> GetEventsForQueue(const QueueInfo& queue, uint32 frame) const
	{
		uint32 queueIndex = m_QueueIndexMap.at(queue.pQueue);
		const EventFrame& frameData = GetSampleFrame(frame);
		return frameData.EventsPerQueue[queueIndex];
	}

	void SetEventCallback(const GPUProfilerCallbacks& inCallbacks) { m_EventCallback = inCallbacks; }

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
			uint32 QueryIndexBegin	: 16;
			uint32 QueryIndexEnd	: 16;
		};
		static_assert(sizeof(Event) == sizeof(uint32));

		ID3D12CommandAllocator*		pCommandAllocator = nullptr;
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
				uint32 QueryIndex	: 16;
				uint32 EventIndex	: 15;
				uint32 IsBegin		: 1;
			};
			static_assert(sizeof(Query) == sizeof(uint32));
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
		SRWLOCK											m_CommandListMapLock{};
		std::unordered_map<ID3D12CommandList*, uint32>	m_CommandListMap;
		std::vector<Data>								m_CommandListData;
	} m_CommandListData{};

	uint32						m_FrameToReadback = 0;
	uint32						m_FrameIndex	 = 0;

	std::vector<QueueInfo>								m_Queues;
	std::unordered_map<ID3D12CommandQueue*, uint32>		m_QueueIndexMap;
	GPUProfilerCallbacks								m_EventCallback;

	ID3D12GraphicsCommandList*	m_pCommandList			= nullptr;
	ID3D12QueryHeap*			m_pQueryHeap			= nullptr;
	ID3D12Resource*				m_pReadbackResource		= nullptr;
	uint64*						m_pReadbackData			= nullptr;
	ID3D12CommandQueue*			m_pResolveQueue			= nullptr;
	ID3D12Fence*				m_pResolveFence			= nullptr;
	HANDLE						m_ResolveWaitHandle		= nullptr;
	uint64						m_LastCompletedFence	= 0;
	bool						m_IsPaused				= false;
	bool						m_PauseQueued			= false;
};


// Helper RAII-style structure to push and pop a GPU sample event
struct GPUProfileScope
{
	GPUProfileScope(const char* pFunction, const char* pFilePath, uint32 lineNr, ID3D12GraphicsCommandList* pCmd, const char* pName)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pName, pFilePath, lineNr);
	}

	GPUProfileScope(const char* pFunction, const char* pFilePath, uint32 lineNr, ID3D12GraphicsCommandList* pCmd)
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

	EventBeginFn	OnEventBegin	= nullptr;
	EventEndFn		OnEventEnd		= nullptr;
	void*			pUserData		= nullptr;
};

// CPU Profiler
// Also responsible for updating GPU profiler
// Also responsible for drawing HUD
class CPUProfiler
{
public:
	void Initialize(uint32 historySize, uint32 maxSamples);
	void Shutdown();

	// Start and push an event on the current thread
	void BeginEvent(const char* pName, const char* pFilePath = nullptr, uint32 lineNumber = 0);

	// End and pop the last pushed event on the current thread
	void EndEvent();

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick();

	// Initialize a thread with an optional name
	void RegisterThread(const char* pName = nullptr);

	// Struct containing all sampling data of a single frame
	struct EventFrame
	{
		static constexpr uint32 ALLOCATOR_SIZE = 1 << 14;

		EventFrame()
			: Allocator(ALLOCATOR_SIZE)
		{}

		// Structure representating a single event
		struct Event
		{
			const char* pName		= "";		// Name of the event
			const char* pFilePath	= nullptr;	// File path of file in which this event is recorded
			uint64		TicksBegin	= 0;		// The ticks at the start of this event
			uint64		TicksEnd	= 0;		// The ticks at the end of this event
			uint32		ThreadIndex	: 11;		// Thread Index of the thread that recorderd this event
			uint32		LineNumber	: 16;		// Line number of file in which this event is recorded
			uint32		Depth		: 5;		// Depth of the event
		};

		std::vector<Span<const Event>>	EventsPerThread;	// Events per thread of the frame
		std::vector<Event>				Events;				// All events of the frame
		std::atomic<uint32>				NumEvents = 0;		// The number of events
		LinearAllocator					Allocator;			// Scratch allocator storing all dynamic allocations of the frame
	};

	// Thread-local storage to keep track of current depth and event stack
	struct TLS
	{
		static constexpr int MAX_STACK_DEPTH = 32;

		FixedStack<uint32, MAX_STACK_DEPTH> EventStack;
		uint32								ThreadIndex		= 0;
		bool								IsInitialized	= false;
	};

	// Structure describing a registered thread
	struct ThreadData
	{
		char		Name[128]	{};
		uint32		ThreadID	= 0;
		const TLS*	pTLS		= nullptr;
		uint32		Index		= 0;
	};

	URange GetFrameRange() const
	{
		uint32 begin = m_FrameIndex < m_HistorySize ? 0 : m_FrameIndex - (uint32)m_HistorySize + 1;
		uint32 end = m_FrameIndex;
		return URange(begin, end);
	}

	Span<const EventFrame::Event> GetEventsForThread(const ThreadData& thread, uint32 frame) const
	{
		const EventFrame& data = m_pSampleData[frame % m_HistorySize];
		if (thread.Index < data.EventsPerThread.size())
			return data.EventsPerThread[thread.Index];
		return {};
	}

	// Get the ticks range of the history
	void GetHistoryRange(uint64& ticksMin, uint64& ticksMax) const
	{
		uint32 oldestFrameIndex = (m_FrameIndex + 1) % m_HistorySize;
		ticksMin = m_pSampleData[oldestFrameIndex].Events[0].TicksBegin;
		uint32 youngestFrameIndex = (m_FrameIndex + (uint32)m_HistorySize - 1) % m_HistorySize;
		ticksMax = m_pSampleData[youngestFrameIndex].Events[0].TicksEnd;
	}

	void SetEventCallback(const CPUProfilerCallbacks& inCallbacks)
	{
		m_EventCallback = inCallbacks;
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
	EventFrame& GetData()
	{
		return m_pSampleData[m_FrameIndex % m_HistorySize];
	}

	CPUProfilerCallbacks m_EventCallback;

	std::mutex				m_ThreadDataLock;				// Mutex for accesing thread data
	std::vector<ThreadData> m_ThreadData;					// Data describing each registered thread

	EventFrame*				m_pSampleData		= nullptr;	// Per-frame data
	uint32					m_HistorySize		= 0;		// History size
	uint32					m_FrameIndex		= 0;		// The current frame index
	bool					m_Paused			= false;	// The current pause state
	bool					m_QueuedPaused		= false;	// The queued pause state
};


// Helper RAII-style structure to push and pop a CPU sample region
struct CPUProfileScope
{
	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName)
	{
		gCPUProfiler.BeginEvent(pName, pFilePath, lineNumber);
	}

	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber)
	{
		gCPUProfiler.BeginEvent(pFunctionName, pFilePath, lineNumber);
	}

	~CPUProfileScope()
	{
		gCPUProfiler.EndEvent();
	}

	CPUProfileScope(const CPUProfileScope&) = delete;
	CPUProfileScope& operator=(const CPUProfileScope&) = delete;
};
