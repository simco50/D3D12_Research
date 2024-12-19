#pragma once
#include "RHI/D3D.h"

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
#define PROFILE_GPU_BEGIN(cmdlist, name)				gGPUProfiler.BeginEvent(cmdlist, name, __FILE__, __LINE__)

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
		gAssert(Depth > 0);
		--Depth;
		return StackData[Depth];
	}

	T& Push()
	{
		Depth++;
		gAssert(Depth < ARRAYSIZE(StackData));
		return StackData[Depth - 1];
	}

	T& Top()
	{
		gAssert(Depth > 0);
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
		gAssert(offset + size <= m_Size);
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



// Single event
struct ProfilerEvent
{
	const char* pName				= nullptr;	///< Name of event
	const char* pFilePath			= nullptr;	///< File path of location where event was started
	uint32		Color		: 24	= 0xFFFFFF;	///< Color
	uint32		Depth		: 8		= 0;		///< Stack depth of event
	uint32		LineNumber	: 18	= 0;		///< Line number of file where event was started
	uint32		ThreadIndex : 8		= 0;		///< Index of thread this event is started on
	uint32		QueueIndex	: 6		= 0;		///< GPU Queue Index (GPU-specific)

	uint64		TicksBegin			= 0;		///< Begin CPU ticks
	uint64		TicksEnd			= 0;		///< End CPU ticks

	bool	IsValid() const		{ return TicksBegin != 0 && TicksEnd != 0; }
	uint32	GetColor() const	{ return Color | (0xFF << 24); }
};



// Data for a single frame of profiling events
class ProfilerEventData
{
public:
	ProfilerEventData()
		: Allocator(1 << 16)
	{}

	Span<const ProfilerEvent> GetEvents() const						{ return Span<const ProfilerEvent>(Events.data(), NumEvents); }
	Span<const ProfilerEvent> GetEvents(uint32 trackIndex) const	{ return trackIndex < EventOffsetAndCountPerTrack.size() && EventOffsetAndCountPerTrack[trackIndex].Size > 0 ? Span<const ProfilerEvent>(&Events[EventOffsetAndCountPerTrack[trackIndex].Offset], EventOffsetAndCountPerTrack[trackIndex].Size) : Span<const ProfilerEvent>(); }

private:
	friend class CPUProfiler;
	friend class GPUProfiler;

	struct OffsetAndSize
	{
		uint32 Offset;
		uint32 Size;
	};

	LinearAllocator						Allocator;						///< Scratch allocator for frame
	Array<OffsetAndSize>				EventOffsetAndCountPerTrack;	///< Span of events for each track
	Array<ProfilerEvent>				Events;							///< Event storage for frame
	uint32								NumEvents = 0;					///< Total number of recorded events
};


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
	void BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, uint32 color, const char* pFilePath, uint32 lineNumber);

	// Allocate and record a GPU event on the commandlist
	void BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, uint32 color = 0) { BeginEvent(pCmd, pName, color, "", 0); }

	// Record a GPU event end on the commandlist
	void EndEvent(ID3D12GraphicsCommandList* pCmd);

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick();

	// Notify profiler that these commandlists are executed on a particular queue
	void ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists);

	void SetPaused(bool paused) { m_PauseQueued = paused; }

	// Data of a single GPU queue. Allows converting GPU timestamps to CPU timestamps
	struct QueueInfo
	{
		char					Name[128]				{};			///< Name of the queue
		ID3D12CommandQueue*		pQueue					= nullptr;	///< The D3D queue object
		uint64					GPUCalibrationTicks		= 0;		///< The number of GPU ticks when the calibration was done
		uint64					CPUCalibrationTicks		= 0;		///< The number of CPU ticks when the calibration was done
		uint64					GPUFrequency			= 0;		///< The GPU tick frequency
		uint32					Index					= 0;		///< Index of queue
		uint32					QueryHeapIndex			= 0;		///< Query Heap index (Copy vs. Other queues)
	};

	Span<const QueueInfo> GetQueues() const { return m_Queues; }

	URange GetFrameRange() const
	{
		uint32 end = m_FrameToReadback;
		uint32 begin = m_FrameIndex < m_EventHistorySize ? 0 : m_FrameIndex - (uint32)m_EventHistorySize;
		return URange(begin, end);
	}

	const ProfilerEventData& GetEventData(uint32 frameIndex) const
	{
		gBoundCheck(frameIndex, GetFrameRange().Begin, GetFrameRange().End);
		return GetSampleFrame(frameIndex);
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
			return m_ReadbackData.Subspan(frameBit * m_MaxNumQueries, m_MaxNumQueries);
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
		Array<ID3D12CommandAllocator*>			m_CommandAllocators;				///< CommandAlloctors to resolve queries. 1 per frame
		uint32									m_MaxNumQueries			= 0;		///< Max number of event queries
		uint32									m_FrameLatency			= 0;		///< Number of GPU frame latency
		std::atomic<uint32>						m_QueryIndex			= 0;		///< Current index of queries
		ID3D12GraphicsCommandList*				m_pCommandList			= nullptr;	///< CommandList to resolve queries
		ID3D12QueryHeap*						m_pQueryHeap			= nullptr;	///< Heap containing MaxNumQueries * FrameLatency queries
		ID3D12Resource*							m_pReadbackResource		= nullptr;	///< Readback resource storing resolved query dara
		Span<const uint64>						m_ReadbackData			= {};		///< Mapped readback resource pointer
		ID3D12CommandQueue*						m_pResolveQueue			= nullptr;	///< Queue to resolve queries on
		ID3D12Fence*							m_pResolveFence			= nullptr;	///< Fence for tracking when queries are finished resolving
		HANDLE									m_ResolveWaitHandle		= nullptr;	///< Handle to allow waiting for resolve to finish
		uint64									m_LastCompletedFence	= 0;		///< Last finish fence value
	};

	const ProfilerEventData& GetSampleFrame(uint32 frameIndex) const { return m_pEventData[frameIndex % m_EventHistorySize]; }
	ProfilerEventData& GetSampleFrame(uint32 frameIndex) { return m_pEventData[frameIndex % m_EventHistorySize]; }
	ProfilerEventData& GetSampleFrame() { return GetSampleFrame(m_FrameIndex); }

	// Data for a single frame of GPU queries. One for each frame latency
	struct QueryData
	{
		struct QueryPair
		{
			uint32 QueryIndexBegin	: 16 = 0xFFFF;
			uint32 QueryIndexEnd	: 16 = 0xFFFF;

			bool IsValid() const { return QueryIndexBegin != 0xFFFF && QueryIndexEnd != 0xFFFF; }
		};
		static_assert(sizeof(QueryPair) == sizeof(uint32));
		Array<QueryPair>	Pairs;
	};
	QueryData& GetQueryData(uint32 frameIndex) { return m_pQueryData[frameIndex % m_FrameLatency]; }
	QueryData& GetQueryData() { return GetQueryData(m_FrameIndex); }

	// Query data for each commandlist
	class CommandListState
	{
	public:
		struct Query
		{
			uint32 QueryIndex : 16 = InvalidEventFlag;
			uint32 EventIndex : 16 = InvalidEventFlag;

			static constexpr uint32 EndEventFlag		= 0xFFFE;
			static constexpr uint32 InvalidEventFlag	= 0xFFFF;
		};
		static_assert(sizeof(Query) == sizeof(uint32));
		using CommandListQueries = Array<Query>;

		void Setup(uint32 maxCommandLists)
		{
			InitializeSRWLock(&m_CommandListMapLock);
			m_CommandListData.resize(maxCommandLists);
		}

		CommandListQueries* Get(const ID3D12CommandList* pCmd, bool createIfNotFound)
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
				gAssert((uint32)index < m_CommandListData.size());
				m_CommandListMap[pCmd] = index;
				ReleaseSRWLockExclusive(&m_CommandListMapLock);
			}
			return index != InvalidIndex ? &m_CommandListData[index] : nullptr;
		}

		void Reset()
		{
#if ENABLE_ASSERTS
			for (CommandListQueries& queries : m_CommandListData)
				gAssert(queries.empty(), "The Queries inside the commandlist is not empty. This is because ExecuteCommandLists was not called with this commandlist.");
#endif
			m_CommandListMap.clear();
		}

	private:
		SRWLOCK										m_CommandListMapLock{};	///< Lock for accessing hashmap
		HashMap<const ID3D12CommandList*, uint32>	m_CommandListMap;		///< Maps commandlist to index
		Array<CommandListQueries>					m_CommandListData;		///< Contains queries for all commandlists
	};

	uint64 ConvertToCPUTicks(const QueueInfo& queue, uint64 gpuTicks) const
	{
		gAssert(gpuTicks >= queue.GPUCalibrationTicks);
		return queue.CPUCalibrationTicks + (gpuTicks - queue.GPUCalibrationTicks) * m_CPUTickFrequency / queue.GPUFrequency;
	}

	QueryHeap& GetHeap(D3D12_COMMAND_LIST_TYPE type) { return type == D3D12_COMMAND_LIST_TYPE_COPY ? m_QueryHeaps[1] : m_QueryHeaps[0]; }

	bool						m_IsInitialized			= false;
	bool						m_IsPaused				= false;
	bool						m_PauseQueued			= false;

	CommandListState			m_CommandListData{};

	ProfilerEventData*			m_pEventData		= nullptr;		///< Data containing all resulting events. 1 per frame history
	uint32						m_EventHistorySize	= 0;			///< Number of frames to keep track of
	std::atomic<uint32>			m_EventIndex		= 0;			///< Current event index
	QueryData*					m_pQueryData		= nullptr;		///< Data containing all intermediate query event data. 1 per frame latency
	uint32						m_FrameLatency		= 0;			///< Max number of in-flight GPU frames
	uint32						m_FrameToReadback	= 0;			///< Next frame to readback from
	uint32						m_FrameIndex		= 0;			///< Current frame index
	StaticArray<QueryHeap, 2>	m_QueryHeaps;						///< GPU Query Heaps
	uint64						m_CPUTickFrequency = 0;				///< Tick frequency of CPU for QPC

	std::mutex					m_QueryRangeLock;

	static constexpr uint32 MAX_EVENT_DEPTH = 32;
	using ActiveEventStack = FixedStack<CommandListState::Query, MAX_EVENT_DEPTH>;
	Array<ActiveEventStack>					m_QueueEventStack;		///< Stack of active events for each command queue
	Array<QueueInfo>						m_Queues;				///< All registered queues
	HashMap<ID3D12CommandQueue*, uint32>	m_QueueIndexMap;		///< Map from command queue to index
	GPUProfilerCallbacks					m_EventCallback;
};


// Helper RAII-style structure to push and pop a GPU sample event
struct GPUProfileScope
{
	GPUProfileScope(const char* pFunction, const char* pFilePath, uint32 lineNumber, ID3D12GraphicsCommandList* pCmd, const char* pName)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pName, 0, pFilePath, lineNumber);
	}

	GPUProfileScope(const char* pFunction, const char* pFilePath, uint32 lineNumber, ID3D12GraphicsCommandList* pCmd)
		: pCmd(pCmd)
	{
		gGPUProfiler.BeginEvent(pCmd, pFunction, 0, pFilePath, lineNumber);
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
	void Initialize(uint32 historySize);
	void Shutdown();

	// Start and push an event on the current thread
	void BeginEvent(const char* pName, uint32 color, const char* pFilePath, uint32 lineNumber);

	// Start and push an event on the current thread
	void BeginEvent(const char* pName, uint32 color = 0) { BeginEvent(pName, color, "", 0); }

	// End and pop the last pushed event on the current thread
	void EndEvent();

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick();

	// Initialize a thread with an optional name
	void RegisterThread(const char* pName = nullptr);

	// Thread-local storage to keep track of current depth and event stack
	struct TLS
	{
		static constexpr int MAX_STACK_DEPTH = 32;

		FixedStack<uint32, MAX_STACK_DEPTH> EventStack;
		uint32								ThreadIndex		= 0;
		bool								IsInitialized	= false;
		Array<ProfilerEvent>				Events;
	};

	// Structure describing a registered thread
	struct ThreadData
	{
		char		Name[128]	{};
		uint32		ThreadID	= 0;
		uint32		Index		= 0;
		TLS*		pTLS		= nullptr;
	};

	URange GetFrameRange() const
	{
		uint32 begin = m_FrameIndex - Math::Min(m_FrameIndex, m_HistorySize) + 1;
		uint32 end = m_FrameIndex;
		return URange(begin, end);
	}

	const ProfilerEventData& GetEventData(uint32 frameIndex) const
	{
		gBoundCheck(frameIndex, GetFrameRange().Begin, GetFrameRange().End);
		return GetData(frameIndex);
	}

	Span<const ThreadData> GetThreads() const { return m_ThreadData; }

	void SetEventCallback(const CPUProfilerCallbacks& inCallbacks)	{ m_EventCallback = inCallbacks; }
	void SetPaused(bool paused)										{ m_QueuedPaused = paused; }
	bool IsPaused() const											{ return m_Paused; }

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
	ProfilerEventData& GetData()								{ return GetData(m_FrameIndex); }
	ProfilerEventData& GetData(uint32 frameIndex)				{ return m_pEventData[frameIndex % m_HistorySize]; }
	const ProfilerEventData& GetData(uint32 frameIndex)	const	{ return m_pEventData[frameIndex % m_HistorySize]; }

	CPUProfilerCallbacks	m_EventCallback;

	std::mutex				m_ThreadDataLock;				// Mutex for accesing thread data
	Array<ThreadData>		m_ThreadData;					// Data describing each registered thread

	ProfilerEventData*		m_pEventData		= nullptr;	// Per-frame data
	uint32					m_HistorySize		= 0;		// History size
	uint32					m_FrameIndex		= 0;		// The current frame index
	bool					m_Paused			= false;	// The current pause state
	bool					m_QueuedPaused		= false;	// The queued pause state
	bool					m_IsInitialized		= false;
};


// Helper RAII-style structure to push and pop a CPU sample region
struct CPUProfileScope
{
	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName, uint32 color = 0)
	{
		gCPUProfiler.BeginEvent(pName, color, pFilePath, lineNumber);
	}

	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, uint32 color = 0)
	{
		gCPUProfiler.BeginEvent(pFunctionName, color, pFilePath, lineNumber);
	}

	~CPUProfileScope()
	{
		gCPUProfiler.EndEvent();
	}

	CPUProfileScope(const CPUProfileScope&) = delete;
	CPUProfileScope& operator=(const CPUProfileScope&) = delete;
};
