#pragma once
#include "Graphics/RHI/D3D.h"

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
#define PROFILE_GPU_SCOPE(cmdlist, ...)					GPUProfileScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, cmdlist, __VA_ARGS__)

// Usage:
//		PROFILE_GPU_BEGIN(const char* pName, ID3D12GraphicsCommandList* pCommandList)
#define PROFILE_GPU_BEGIN(cmdlist, name)				gGPUProfiler.BeginEvent(name, cmdlist, __FILE__, __LINE__)

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
	void Initialize(
		ID3D12Device*				pDevice,
		Span<ID3D12CommandQueue*>	queues,
		uint32						sampleHistory,
		uint32						frameLatency,
		uint32						maxNumEvents,
		uint32						maxNumCopyEvents,
		uint32						maxNumActiveCommandLists);

	void Shutdown();

	// Allocate and record a GPU event on the commandlist
	void BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, const char* pFilePath = "", uint32 lineNumber = 0);

	// Record a GPU event end on the commandlist
	void EndEvent(ID3D12GraphicsCommandList* pCmd);

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick();

	// Notify profiler that these commandlists are executed on a particular queue
	void ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists);

	void SetPaused(bool paused) { m_PauseQueued = paused; }

	// Data for a single frame of profiling events. On for each history frame
	struct EventData
	{
		EventData()
			: Allocator(1 << 14)
		{}

		struct Event
		{
			const char* pName		= nullptr;	// Name of event
			const char* pFilePath	= nullptr;	// File path of location where event was started
			uint64		TicksBegin	= 0;		// Begin CPU ticks
			uint64		TicksEnd	= 0;		// End CPU ticks
			uint32		LineNumber	: 16;		// Line number of file where event was started
			uint32		Depth		: 8;		// Stack depth of event
			uint32		QueueIndex	: 8;		// Index of QueueInfo
			uint32		padding		: 32;
		};
		static_assert(sizeof(Event) == sizeof(uint32) * 10);

		class Iterator
		{
		public:
			Iterator(Span<const Event> events, uint32 queueIndex)
				: m_Events(events), m_QueueIndex(queueIndex), m_CurrentIndex(0)
			{
				AdvanceToValid();
			}

			void operator++()
			{
				++m_CurrentIndex;
				AdvanceToValid();
			}

			bool IsValid() const { return m_CurrentIndex < m_Events.GetSize(); }
			const Event& Get() const { return m_Events[m_CurrentIndex]; }

		private:
			void AdvanceToValid()
			{
				while (m_CurrentIndex < m_Events.GetSize() && m_Events[m_CurrentIndex].QueueIndex != m_QueueIndex)
					m_CurrentIndex++;
			}

			Span<const Event>	m_Events;
			uint32				m_QueueIndex;
			uint32				m_CurrentIndex;
		};

		Iterator Iterate(uint32 queueIndex) const { return Iterator(Span<const Event>(Events.data(), NumEvents), queueIndex); }

		LinearAllocator					Allocator;			// Scratch allocator for frame
		std::vector<Event>				Events;				// Event storage for frame
		uint32							NumEvents = 0;		// Total number of recorded events
	};

	// Data of a single GPU queue. Allows converting GPU timestamps to CPU timestamps
	struct QueueInfo
	{
		char					Name[128]				{};			// Name of the queue
		ID3D12CommandQueue*		pQueue					= nullptr;	// The D3D queue object
		uint64					GPUCalibrationTicks		= 0;		// The number of GPU ticks when the calibration was done
		uint64					CPUCalibrationTicks		= 0;		// The number of CPU ticks when the calibration was done
		uint64					GPUFrequency			= 0;		// The GPU tick frequency
		bool					IsCopyQueue				= false;	// True if queue is a copy queue
	};

	Span<const QueueInfo> GetQueues() const { return m_Queues; }

	URange GetFrameRange() const
	{
		uint32 end = m_FrameToReadback;
		uint32 begin = m_FrameIndex < m_EventHistorySize ? 0 : m_FrameIndex - (uint32)m_EventHistorySize;
		return URange(begin, end);
	}

	EventData::Iterator IterateEvents(uint32 frame, const QueueInfo& queue) const
	{
		check(frame >= GetFrameRange().Begin && frame < GetFrameRange().End);
		const EventData& eventData = GetSampleFrame(frame);
		uint32 queueIndex = m_QueueIndexMap.at(queue.pQueue);
		return eventData.Iterate(queueIndex);
	}

	Span<const EventData::Event> GetEvents(uint32 frame) const
	{
		check(frame >= GetFrameRange().Begin && frame < GetFrameRange().End);
		const EventData& eventData = GetSampleFrame(frame);
		return Span<const EventData::Event>(eventData.Events.data(), eventData.NumEvents);
	}

	void SetEventCallback(const GPUProfilerCallbacks& inCallbacks) { m_EventCallback = inCallbacks; }

private:
	struct QueryHeap
	{
	public:
		void Initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pResolveQueue, uint32 maxNumQueries, uint32 frameLatency);
		void Shutdown();

		uint32 RecordQuery(ID3D12GraphicsCommandList* pCmd);
		uint32 Resolve(uint32 frameIndex);
		void Reset(uint32 frameIndex);

		Span<const uint64> GetQueryData(uint32 frameIndex) const
		{
			if (!IsInitialized())
				return {};

			uint32 frameBit = frameIndex % m_FrameLatency;
			return Span<const uint64>(m_pReadbackData + frameBit * m_MaxNumQueries, m_MaxNumQueries);
		}

		bool IsFrameComplete(uint64 frameIndex)
		{
			if (!IsInitialized())
				return true;

			uint64 fenceValue = frameIndex;
			if (fenceValue <= m_LastCompletedFence)
				return true;
			m_LastCompletedFence = Math::Max(m_pResolveFence->GetCompletedValue(), m_LastCompletedFence);
			return fenceValue <= m_LastCompletedFence;
		}

		void WaitFrame(uint32 frameIndex)
		{
			if (!IsInitialized())
				return;

			if (!IsFrameComplete(frameIndex))
			{
				m_pResolveFence->SetEventOnCompletion(frameIndex, m_ResolveWaitHandle);
				WaitForSingleObject(m_ResolveWaitHandle, INFINITE);
			}
		}

		bool IsInitialized() const			{ return m_pQueryHeap != nullptr; }
		ID3D12QueryHeap* GetHeap() const	{ return m_pQueryHeap; }

	private:
		std::vector<ID3D12CommandAllocator*>	m_CommandAllocators;				// CommandAlloctors to resolve queries. 1 per frame
		uint32									m_MaxNumQueries			= 0;		// Max number of event queries
		uint32									m_FrameLatency			= 0;		// Number of GPU frame latency
		std::atomic<uint32>						m_QueryIndex			= 0;		// Current index of queries
		ID3D12GraphicsCommandList*				m_pCommandList			= nullptr;	// CommandList to resolve queries
		ID3D12QueryHeap*						m_pQueryHeap			= nullptr;	// Heap containing MaxNumQueries * FrameLatency queries
		ID3D12Resource*							m_pReadbackResource		= nullptr;	// Readback resource storing resolved query dara
		const uint64*							m_pReadbackData			= nullptr;	// Mapped readback resource pointer
		ID3D12CommandQueue*						m_pResolveQueue			= nullptr;	// Queue to resolve queries on
		ID3D12Fence*							m_pResolveFence			= nullptr;	// Fence for tracking when queries are finished resolving
		HANDLE									m_ResolveWaitHandle		= nullptr;	// Handle to allow waiting for resolve to finish
		uint64									m_LastCompletedFence	= 0;		// Last finish fence value
	};

	const EventData& GetSampleFrame(uint32 frameIndex) const { return m_pEventData[frameIndex % m_EventHistorySize]; }
	EventData& GetSampleFrame(uint32 frameIndex) { return m_pEventData[frameIndex % m_EventHistorySize]; }
	EventData& GetSampleFrame() { return GetSampleFrame(m_FrameIndex); }

	// Data for a single frame of GPU queries. One for each frame latency
	struct QueryData
	{
		struct QueryRange
		{
			uint32 QueryIndexBegin	: 16;
			uint32 QueryIndexEnd	: 16;
		};
		static_assert(sizeof(QueryRange) == sizeof(uint32));
		std::vector<QueryRange>	Ranges;
	};
	QueryData& GetQueryData(uint32 frameIndex) { return m_pQueryData[frameIndex % m_FrameLatency]; }
	QueryData& GetQueryData() { return GetQueryData(m_FrameIndex); }

	// Query data for each commandlist
	class CommandListData
	{
	public:
		struct Data
		{
			struct Query
			{
				uint32 QueryIndex	: 16;
				uint32 RangeIndex	: 15;
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
			static constexpr uint32 InvalidIndex = 0xFFFFFFFF;
			AcquireSRWLockShared(&m_CommandListMapLock);
			auto it = m_CommandListMap.find(pCmd);
			uint32 index = InvalidIndex;
			if (it != m_CommandListMap.end())
				index = it->second;
			ReleaseSRWLockShared(&m_CommandListMapLock);
			if (createIfNotFound && index == InvalidIndex)
			{
				AcquireSRWLockExclusive(&m_CommandListMapLock);
				index = (uint32)m_CommandListMap.size();
				m_CommandListMap[pCmd] = index;
				ReleaseSRWLockExclusive(&m_CommandListMapLock);
			}
			if (index == InvalidIndex)
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
	};

	uint64 ConvertToCPUTicks(const QueueInfo& queue, uint64 gpuTicks) const
	{
		check(gpuTicks >= queue.GPUCalibrationTicks);
		return queue.CPUCalibrationTicks + (gpuTicks - queue.GPUCalibrationTicks) * m_CPUTickFrequency / queue.GPUFrequency;
	}

	QueryHeap& GetHeap(D3D12_COMMAND_LIST_TYPE type) { return type == D3D12_COMMAND_LIST_TYPE_COPY ? m_CopyHeap : m_MainHeap; }

	CommandListData				m_CommandListData{};

	EventData*					m_pEventData			= nullptr;		// Data containing all resulting events. 1 per frame history
	uint32						m_EventHistorySize		= 0;			// Number of frames to keep track of
	std::atomic<uint32>			m_EventIndex			= 0;			// Current event index

	QueryData*					m_pQueryData			= nullptr;		// Data containing all intermediate query event data. 1 per frame latency
	uint32						m_FrameLatency			= 0;			// Max number of in-flight GPU frames

	uint32						m_FrameToReadback		= 0;			// Next frame to readback from
	uint32						m_FrameIndex			= 0;			// Current frame index

	QueryHeap					m_MainHeap;
	QueryHeap					m_CopyHeap;
	uint64						m_CPUTickFrequency		= 0;

	static constexpr uint32 MAX_EVENT_DEPTH = 32;
	using ActiveEventStack = FixedStack<uint32, MAX_EVENT_DEPTH>;
	std::vector<ActiveEventStack>						m_QueueEventStack;	// Stack of active events for each command queue
	std::vector<QueueInfo>								m_Queues;			// All registered queues
	std::unordered_map<ID3D12CommandQueue*, uint32>		m_QueueIndexMap;	// Map from command queue to index
	GPUProfilerCallbacks								m_EventCallback;

	bool						m_IsPaused				= false;
	bool						m_PauseQueued			= false;
};


// Helper RAII-style structure to push and pop a GPU sample event
struct GPUProfileScope
{
	GPUProfileScope(const char* pFunction, const char* pFilePath, uint32 lineNumber, ID3D12GraphicsCommandList* pCmd, const char* pName)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pName, pFilePath, lineNumber);
	}

	GPUProfileScope(const char* pFunction, const char* pFilePath, uint32 lineNumber, ID3D12GraphicsCommandList* pCmd)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pFunction, pFilePath, lineNumber);
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
	using EventBeginFn	= void(*)(const char* /*pName*/, void* /*pUserData*/);
	using EventEndFn	= void(*)(void* /*pUserData*/);

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
	void Initialize(uint32 historySize, uint32 maxEvents);
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
	struct EventData
	{
		static constexpr uint32 ALLOCATOR_SIZE = 1 << 14;

		EventData()
			: Allocator(ALLOCATOR_SIZE)
		{}

		// Structure representating a single event
		struct Event
		{
			const char* pName		= "";		// Name of the event
			const char* pFilePath	= nullptr;	// File path of file in which this event is recorded
			uint64		TicksBegin	= 0;		// The ticks at the start of this event
			uint64		TicksEnd	= 0;		// The ticks at the end of this event
			uint32		LineNumber	: 16;		// Line number of file in which this event is recorded
			uint32		ThreadIndex	: 11;		// Thread Index of the thread that recorderd this event
			uint32		Depth		: 5;		// Depth of the event
			uint32		padding		: 32;
		};
		static_assert(sizeof(Event) == 10 * sizeof(uint32));

		class Iterator
		{
		public:
			Iterator(Span<const Event> events, uint32 threadIndex)
				: m_Events(events), m_ThreadIndex(threadIndex), m_CurrentIndex(0)
			{
				AdvanceToValid();
			}

			void operator++()
			{
				++m_CurrentIndex;
				AdvanceToValid();
			}

			bool IsValid() const { return m_CurrentIndex < m_Events.GetSize(); }
			const Event& Get() const { return m_Events[m_CurrentIndex]; }


		private:
			void AdvanceToValid()
			{
				while (m_CurrentIndex < m_Events.GetSize() && m_Events[m_CurrentIndex].ThreadIndex != m_ThreadIndex)
					m_CurrentIndex++;
			}

			Span<const Event>	m_Events;
			uint32				m_ThreadIndex;
			uint32				m_CurrentIndex;
		};

		Iterator Iterate(uint32 threadIndex) const { return Iterator(Span<const Event>(Events.data(), NumEvents), threadIndex); }

		std::vector<Event>				Events;				// All events of the frame
		LinearAllocator					Allocator;			// Scratch allocator storing all dynamic allocations of the frame
		uint32							NumEvents = 0;		// The number of events
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
		uint32		Index		= 0;
		const TLS*	pTLS		= nullptr;
	};

	URange GetFrameRange() const
	{
		uint32 begin = m_FrameIndex - Math::Min(m_FrameIndex, m_HistorySize) + 1;
		uint32 end = m_FrameIndex;
		return URange(begin, end);
	}

	EventData::Iterator IterateEvents(uint32 frame, const ThreadData& thread) const
	{
		check(frame >= GetFrameRange().Begin && frame < GetFrameRange().End);
		const EventData& eventData = GetData(frame);
		return eventData.Iterate(thread.Index);
	}

	Span<const EventData::Event> GetEvents(uint32 frame) const
	{
		check(frame >= GetFrameRange().Begin && frame < GetFrameRange().End);
		const EventData& eventData = GetData(frame);
		return Span<const EventData::Event>(eventData.Events.data(), eventData.NumEvents);
	}

	// Get the ticks range of the history
	void GetHistoryRange(uint64& ticksMin, uint64& ticksMax) const
	{
		URange range = GetFrameRange();
		ticksMin = GetData(range.Begin).Events[0].TicksBegin;
		ticksMax = GetData(range.End).Events[0].TicksEnd;
	}

	Span<const ThreadData> GetThreads() const { return m_ThreadData; }

	void SetEventCallback(const CPUProfilerCallbacks& inCallbacks) { m_EventCallback = inCallbacks;	}
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
	EventData& GetData()								{ return GetData(m_FrameIndex); }
	EventData& GetData(uint32 frameIndex)				{ return m_pEventData[frameIndex % m_HistorySize]; }
	const EventData& GetData(uint32 frameIndex)	const	{ return m_pEventData[frameIndex % m_HistorySize]; }

	CPUProfilerCallbacks m_EventCallback;

	std::mutex				m_ThreadDataLock;				// Mutex for accesing thread data
	std::vector<ThreadData> m_ThreadData;					// Data describing each registered thread

	EventData*				m_pEventData		= nullptr;	// Per-frame data
	uint32					m_HistorySize		= 0;		// History size
	uint32					m_FrameIndex		= 0;		// The current frame index
	std::atomic<uint32>		m_EventIndex		= 0;		// The current event index
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
