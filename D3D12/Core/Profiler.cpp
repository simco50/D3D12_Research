
#include "stdafx.h"
#include "Profiler.h"

#if WITH_PROFILING

CPUProfiler gCPUProfiler;
GPUProfiler gGPUProfiler;

//-----------------------------------------------------------------------------
// [SECTION] GPU Profiler
//-----------------------------------------------------------------------------

void GPUProfiler::Initialize(
	ID3D12Device*				pDevice,
	Span<ID3D12CommandQueue*>	queues,
	uint32						sampleHistory,
	uint32						frameLatency,
	uint32						maxNumEvents,
	uint32						maxNumCopyEvents,
	uint32						maxNumActiveCommandLists)
{
	m_FrameLatency = frameLatency;
	m_EventHistorySize = sampleHistory;

	m_CommandListData.Setup(maxNumActiveCommandLists);

	for (uint16 queueIndex = 0; queueIndex < queues.GetSize(); ++queueIndex)
	{
		ID3D12CommandQueue* pQueue = queues[queueIndex];
		D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();

		m_QueueIndexMap[pQueue] = (uint32)m_Queues.size();
		QueueInfo& queueInfo = m_Queues.emplace_back();
		uint32 size = ARRAYSIZE(queueInfo.Name);
		pQueue->GetPrivateData(WKPDID_D3DDebugObjectName, &size, queueInfo.Name);
		queueInfo.pQueue = pQueue;
		queueInfo.InitCalibration();

		if (desc.Type == D3D12_COMMAND_LIST_TYPE_COPY && !m_CopyHeap.IsInitialized())
			m_CopyHeap.Initialize(pDevice, pQueue, 2 * maxNumCopyEvents, frameLatency);
		else if (desc.Type != D3D12_COMMAND_LIST_TYPE_COPY && !m_MainHeap.IsInitialized())
			m_MainHeap.Initialize(pDevice, pQueue, 2 * maxNumEvents, frameLatency);
	}

	m_pEventData = new EventData[sampleHistory];
	for (uint32 i = 0; i < sampleHistory; ++i)
	{
		EventData& eventData = m_pEventData[i];
		eventData.Events.resize(maxNumEvents + maxNumCopyEvents);
		eventData.EventsPerQueue.resize(queues.GetSize());
	}

	m_pQueryData = new QueryData[frameLatency];
	for (uint32 i = 0; i < frameLatency; ++i)
	{
		QueryData& queryData = m_pQueryData[i];
		queryData.Ranges.resize(maxNumEvents + maxNumCopyEvents);
	}
}

void GPUProfiler::Shutdown()
{
	delete[] m_pEventData;
	delete[] m_pQueryData;

	m_CopyHeap.Shutdown();
	m_MainHeap.Shutdown();
}


void GPUProfiler::BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, const char* pFilePath, uint32 lineNumber)
{
	if (m_EventCallback.OnEventBegin)
		m_EventCallback.OnEventBegin(pName, pCmd, m_EventCallback.pUserData);

	if (m_IsPaused)
		return;

	QueryData& queryData			= GetQueryData();
	EventData& eventData			= GetSampleFrame();
	CommandListData::Data* pCmdData = m_CommandListData.Get(pCmd, true);

	// Allocate a query range. This stores a begin/end query index pair. (Also event index)
	uint32 eventIndex = m_EventIndex.fetch_add(1);
	check(eventIndex < eventData.Events.size());

	// Record a timestamp query
	uint32 queryIndex = GetHeap(pCmd->GetType()).RecordQuery(pCmd);

	// Assign the query to the commandlist
	CommandListData::Data::Query& cmdListQuery	= pCmdData->Queries.emplace_back();
	cmdListQuery.QueryIndex						= queryIndex;
	cmdListQuery.RangeIndex						= eventIndex;
	cmdListQuery.IsBegin						= true;

	// Allocate a query range in the query frame
	QueryData::QueryRange& range	= queryData.Ranges[eventIndex];
	range.QueryIndexBegin			= queryIndex;
	range.IsCopyQuery				= pCmd->GetType() == D3D12_COMMAND_LIST_TYPE_COPY;

	// Allocate an event in the sample history
	EventData::Event& event			= eventData.Events[eventIndex];
	event.Index						= eventIndex;
	event.pName						= eventData.Allocator.String(pName);
	event.pFilePath					= pFilePath;
	event.LineNumber				= lineNumber;
}


void GPUProfiler::EndEvent(ID3D12GraphicsCommandList* pCmd)
{
	if (m_EventCallback.OnEventEnd)
		m_EventCallback.OnEventEnd(pCmd, m_EventCallback.pUserData);

	if (m_IsPaused)
		return;

	// Record a query in the commandlist
	CommandListData::Data* pCmdData		= m_CommandListData.Get(pCmd, true);
	CommandListData::Data::Query& query = pCmdData->Queries.emplace_back();
	query.QueryIndex					= GetHeap(pCmd->GetType()).RecordQuery(pCmd);
	query.RangeIndex					= 0x7FFF; // Range index is only required for 'Begin' events
	query.IsBegin						= false;
}

void GPUProfiler::Tick()
{
	// If the next frame is not finished resolving, wait for it here so the data can be read from before it's being reset
	m_CopyHeap.WaitFrame(m_FrameIndex);
	m_MainHeap.WaitFrame(m_FrameIndex);

	GetSampleFrame(m_FrameIndex).NumEvents = m_EventIndex;
	m_EventIndex = 0;

	while (m_FrameToReadback < m_FrameIndex)
	{
		QueryData& queryData = GetQueryData(m_FrameToReadback);
		EventData& eventData = GetSampleFrame(m_FrameToReadback);
		if (!m_MainHeap.IsFrameComplete(m_FrameToReadback) || !m_CopyHeap.IsFrameComplete(m_FrameToReadback))
			break;

		uint32 numEvents = eventData.NumEvents;
		Span<const uint64> mainQueries = m_MainHeap.GetQueryData(m_FrameToReadback);
		Span<const uint64> copyQueries = m_CopyHeap.GetQueryData(m_FrameToReadback);

		for (uint32 i = 0; i < numEvents; ++i)
		{
			QueryData::QueryRange& queryRange	= queryData.Ranges[i];
			EventData::Event& event				= eventData.Events[i];
			event.TicksBegin					= queryRange.IsCopyQuery ? copyQueries[queryRange.QueryIndexBegin] : mainQueries[queryRange.QueryIndexBegin];
			event.TicksEnd						= queryRange.IsCopyQuery ? copyQueries[queryRange.QueryIndexEnd] : mainQueries[queryRange.QueryIndexEnd];
		}

		// Sort events by queue
		std::vector<EventData::Event>& events = eventData.Events;
		std::sort(events.begin(), events.begin() + numEvents, [](const EventData::Event& a, const EventData::Event& b)
			{
				return a.QueueIndex < b.QueueIndex;
			});

		URange eventRange(0, 0);
		for (uint32 queueIndex = 0; queueIndex < (uint32)m_Queues.size(); ++queueIndex)
		{
			while (queueIndex < events[eventRange.Begin].QueueIndex)
				eventRange.Begin++;
			eventRange.End = eventRange.Begin;
			while (events[eventRange.End].QueueIndex == queueIndex && eventRange.End < numEvents)
				++eventRange.End;

			eventData.EventsPerQueue[queueIndex] = Span<const EventData::Event>(&events[eventRange.Begin], eventRange.End - eventRange.Begin);
			eventRange.Begin = eventRange.End;
		}

		++m_FrameToReadback;
	}

	m_IsPaused = m_PauseQueued;
	if (m_IsPaused)
		return;

	m_CommandListData.Reset();

	{
		m_MainHeap.Resolve(m_FrameIndex);
		m_CopyHeap.Resolve(m_FrameIndex);
	}

	++m_FrameIndex;

	{
		m_MainHeap.Reset(m_FrameIndex);
		m_CopyHeap.Reset(m_FrameIndex);

		EventData& eventFrame = GetSampleFrame();
		eventFrame.NumEvents = 0;
		eventFrame.Allocator.Reset();
		for (uint32 i = 0; i < (uint32)m_Queues.size(); ++i)
			eventFrame.EventsPerQueue[i] = {};
	}
}

void GPUProfiler::ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists)
{
	if (m_IsPaused)
		return;

	QueryData& queryData = GetQueryData();
	EventData& sampleFrame = GetSampleFrame();

	std::vector<uint32> queryRangeStack;
	for (ID3D12CommandList* pCmd : commandLists)
	{
		CommandListData::Data* pEventData = m_CommandListData.Get(pCmd, false);
		if (pEventData)
		{
			for (CommandListData::Data::Query& query : pEventData->Queries)
			{
				if (query.IsBegin)
				{
					queryRangeStack.push_back(query.RangeIndex);
				}
				else
				{
					check(!queryRangeStack.empty(), "Event Begin/End mismatch");
					check(query.RangeIndex == 0x7FFF);
					uint32 queryRangeIndex = queryRangeStack.back();
					queryRangeStack.pop_back();

					QueryData::QueryRange& queryRange	= queryData.Ranges[queryRangeIndex];
					EventData::Event& sampleEvent		= sampleFrame.Events[queryRangeIndex];

					queryRange.QueryIndexEnd	= query.QueryIndex;
					sampleEvent.QueueIndex		= m_QueueIndexMap[pQueue];
					sampleEvent.Depth			= (uint32)queryRangeStack.size();
				}
			}
			pEventData->Queries.clear();
		}
	}
	check(queryRangeStack.empty(), "Forgot to End %d Events", queryRangeStack.size());
}


//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------


void CPUProfiler::Initialize(uint32 historySize, uint32 maxEvents)
{
	Shutdown();

	m_pEventData = new EventData[historySize];
	m_HistorySize = historySize;

	for (uint32 i = 0; i < historySize; ++i)
		m_pEventData[i].Events.resize(maxEvents);
}


void CPUProfiler::Shutdown()
{
	delete[] m_pEventData;
}


void CPUProfiler::BeginEvent(const char* pName, const char* pFilePath, uint32 lineNumber)
{
	if(m_EventCallback.OnEventBegin)
		m_EventCallback.OnEventBegin(pName, m_EventCallback.pUserData);

	if (m_Paused)
		return;

	EventData& data = GetData();
	uint32 newIndex = data.NumEvents.fetch_add(1);
	check(newIndex < data.Events.size());

	TLS& tls = GetTLS();

	EventData::Event& newEvent = data.Events[newIndex];
	newEvent.Depth = tls.EventStack.GetSize();
	newEvent.ThreadIndex = tls.ThreadIndex;
	newEvent.pName = data.Allocator.String(pName);
	newEvent.pFilePath = pFilePath;
	newEvent.LineNumber = lineNumber;
	QueryPerformanceCounter((LARGE_INTEGER*)(&newEvent.TicksBegin));

	tls.EventStack.Push() = newIndex;
}


// End and pop the last pushed event on the current thread
void CPUProfiler::EndEvent()
{
	if (m_EventCallback.OnEventEnd)
		m_EventCallback.OnEventEnd(m_EventCallback.pUserData);

	if (m_Paused)
		return;

	EventData::Event& event = GetData().Events[GetTLS().EventStack.Pop()];
	QueryPerformanceCounter((LARGE_INTEGER*)(&event.TicksEnd));
}


void CPUProfiler::Tick()
{
	m_Paused = m_QueuedPaused;
	if (m_Paused)
		return;

	if (m_FrameIndex)
		EndEvent();

	// Check if all threads have ended all open sample events
	for (auto& threadData : m_ThreadData)
		check(threadData.pTLS->EventStack.GetSize() == 0);

	// Sort the events by thread and group by thread
	EventData& frame = GetData();
	std::vector<EventData::Event>& events = frame.Events;
	std::sort(events.begin(), events.begin() + frame.NumEvents, [](const EventData::Event& a, const EventData::Event& b)
		{
			return a.ThreadIndex < b.ThreadIndex;
		});

	URange eventRange(0, 0);
	for (uint32 threadIndex = 0; threadIndex < (uint32)m_ThreadData.size(); ++threadIndex)
	{
		while (threadIndex < events[eventRange.Begin].ThreadIndex)
			eventRange.Begin++;
		eventRange.End = eventRange.Begin;
		while (events[eventRange.End].ThreadIndex == threadIndex && eventRange.End < frame.NumEvents)
			++eventRange.End;

		frame.EventsPerThread[threadIndex] = Span<const EventData::Event>(&events[eventRange.Begin], eventRange.End - eventRange.Begin);
		eventRange.Begin = eventRange.End;
	}

	++m_FrameIndex;

	EventData& newData = GetData();
	newData.Allocator.Reset();
	newData.NumEvents = 0;

	BeginEvent("CPU Frame");
}


void CPUProfiler::RegisterThread(const char* pName)
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
	data.Index = (uint32)m_ThreadData.size() - 1;

	for (uint32 i = 0; i < m_HistorySize; ++i)
		m_pEventData[i].EventsPerThread.resize(m_ThreadData.size());
}

#endif
