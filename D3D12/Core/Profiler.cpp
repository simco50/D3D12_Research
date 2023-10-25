
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
	// Event indices are 15 bit, so max 2^15 events
	check(maxNumEvents + maxNumCopyEvents < (1 << 15));

	m_FrameLatency = frameLatency;
	m_EventHistorySize = sampleHistory;

	m_CommandListData.Setup(maxNumActiveCommandLists);
	m_QueueEventStack.resize(queues.GetSize());
	for (uint32 queueIndex = 0; queueIndex < queues.GetSize(); ++queueIndex)
	{
		ID3D12CommandQueue* pQueue = queues[queueIndex];
		D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();

		m_QueueIndexMap[pQueue] = (uint32)m_Queues.size();
		QueueInfo& queueInfo = m_Queues.emplace_back();
		uint32 size = ARRAYSIZE(queueInfo.Name);
		if(FAILED(pQueue->GetPrivateData(WKPDID_D3DDebugObjectName, &size, queueInfo.Name)))
		{
			if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
				strcpy_s(queueInfo.Name, "Direct Queue");
			else if (desc.Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
				strcpy_s(queueInfo.Name, "Compute Queue");
			else if (desc.Type == D3D12_COMMAND_LIST_TYPE_COPY)
				strcpy_s(queueInfo.Name, "Copy Queue");
			else
				strcpy_s(queueInfo.Name, "??? Queue");
		}

		queueInfo.pQueue = pQueue;
		queueInfo.Index = queueIndex;
		queueInfo.IsCopyQueue = desc.Type == D3D12_COMMAND_LIST_TYPE_COPY;
		pQueue->GetClockCalibration(&queueInfo.GPUCalibrationTicks, &queueInfo.CPUCalibrationTicks);
		pQueue->GetTimestampFrequency(&queueInfo.GPUFrequency);

		if (desc.Type == D3D12_COMMAND_LIST_TYPE_COPY && !m_CopyHeap.IsInitialized())
			m_CopyHeap.Initialize(pDevice, pQueue, 2 * maxNumCopyEvents, frameLatency);
		else if (desc.Type != D3D12_COMMAND_LIST_TYPE_COPY && !m_MainHeap.IsInitialized())
			m_MainHeap.Initialize(pDevice, pQueue, 2 * maxNumEvents, frameLatency);
	}
	QueryPerformanceFrequency((LARGE_INTEGER*)&m_CPUTickFrequency);

	m_pEventData = new EventData[sampleHistory];
	for (uint32 i = 0; i < sampleHistory; ++i)
	{
		EventData& eventData = m_pEventData[i];
		eventData.Events.resize(maxNumEvents + maxNumCopyEvents);
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

	// Allocate an event in the sample history
	EventData::Event& event			= eventData.Events[eventIndex];
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
	for (ActiveEventStack& stack : m_QueueEventStack)
	{
		check(stack.GetSize() == 0, "Forgot to End %d Events", stack.GetSize());
	}

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
			QueryData::QueryRange&	queryRange	= queryData.Ranges[i];
			EventData::Event&		event		= eventData.Events[i];
			const QueueInfo&		queue		= m_Queues[event.QueueIndex];
			Span<const uint64>		queries		= queue.IsCopyQueue ? copyQueries : mainQueries;

			// Convert to CPU ticks and assign to event
			event.TicksBegin					= ConvertToCPUTicks(queue, queries[queryRange.QueryIndexBegin]);
			event.TicksEnd						= ConvertToCPUTicks(queue, queries[queryRange.QueryIndexEnd]);
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
	}
}

void GPUProfiler::ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists)
{
	if (m_IsPaused)
		return;

	uint32 queueIndex = m_QueueIndexMap.at(pQueue);
	ActiveEventStack& eventStack = m_QueueEventStack[queueIndex];
	QueryData& queryData = GetQueryData();
	EventData& sampleFrame = GetSampleFrame();

	for (ID3D12CommandList* pCmd : commandLists)
	{
		CommandListData::Data* pEventData = m_CommandListData.Get(pCmd, false);
		if (pEventData)
		{
			for (CommandListData::Data::Query& query : pEventData->Queries)
			{
				if (query.IsBegin)
				{
					eventStack.Push() = query.RangeIndex;
					EventData::Event& sampleEvent = sampleFrame.Events[query.RangeIndex];
					sampleEvent.QueueIndex = queueIndex;
				}
				else
				{
					check(eventStack.GetSize() > 0, "Event Begin/End mismatch");
					check(query.RangeIndex == 0x7FFF);
					uint32 queryRangeIndex = eventStack.Pop();

					QueryData::QueryRange& queryRange	= queryData.Ranges[queryRangeIndex];
					EventData::Event& sampleEvent		= sampleFrame.Events[queryRangeIndex];

					queryRange.QueryIndexEnd	= query.QueryIndex;
					sampleEvent.Depth			= eventStack.GetSize();
					check(sampleEvent.QueueIndex == queueIndex, "Begin/EndEvent must be recorded on the same queue");
				}
			}
			pEventData->Queries.clear();
		}
	}
}


void GPUProfiler::QueryHeap::Initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pResolveQueue, uint32 maxNumQueries, uint32 frameLatency)
{
	m_pResolveQueue = pResolveQueue;
	m_FrameLatency = frameLatency;
	m_MaxNumQueries = maxNumQueries;

	D3D12_COMMAND_QUEUE_DESC queueDesc = pResolveQueue->GetDesc();

	D3D12_QUERY_HEAP_DESC heapDesc{};
	heapDesc.Count = maxNumQueries;
	heapDesc.NodeMask = 0x1;
	heapDesc.Type = queueDesc.Type == D3D12_COMMAND_LIST_TYPE_COPY ? D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	pDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_pQueryHeap));

	for (uint32 i = 0; i < frameLatency; ++i)
		pDevice->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&m_CommandAllocators.emplace_back()));
	pDevice->CreateCommandList(0x1, queueDesc.Type, m_CommandAllocators[0], nullptr, IID_PPV_ARGS(&m_pCommandList));

	D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer((uint64)maxNumQueries * sizeof(uint64) * frameLatency);
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_pReadbackResource));
	void* pReadbackData = nullptr;
	m_pReadbackResource->Map(0, nullptr, &pReadbackData);
	m_pReadbackData = (uint64*)pReadbackData;

	pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pResolveFence));
	m_ResolveWaitHandle = CreateEventExA(nullptr, "Fence Event", 0, EVENT_ALL_ACCESS);
}

void GPUProfiler::QueryHeap::Shutdown()
{
	if (!IsInitialized())
		return;

	for (ID3D12CommandAllocator* pAllocator : m_CommandAllocators)
		pAllocator->Release();
	m_pCommandList->Release();
	m_pQueryHeap->Release();
	m_pReadbackResource->Release();
	m_pResolveFence->Release();
	CloseHandle(m_ResolveWaitHandle);
}

uint32 GPUProfiler::QueryHeap::RecordQuery(ID3D12GraphicsCommandList* pCmd)
{
	uint32 index = m_QueryIndex.fetch_add(1);
	pCmd->EndQuery(m_pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index);
	return index;
}

uint32 GPUProfiler::QueryHeap::Resolve(uint32 frameIndex)
{
	if (!IsInitialized())
		return 0;

	uint32 frameBit = frameIndex % m_FrameLatency;
	uint32 queryStart = frameBit * m_MaxNumQueries;
	uint32 numQueries = m_QueryIndex;
	m_pCommandList->ResolveQueryData(m_pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, numQueries, m_pReadbackResource, queryStart * sizeof(uint64));
	m_pCommandList->Close();
	ID3D12CommandList* pCmdLists[] = { m_pCommandList };
	m_pResolveQueue->ExecuteCommandLists(1, pCmdLists);
	m_pResolveQueue->Signal(m_pResolveFence, frameIndex + 1);
	return numQueries;
}

void GPUProfiler::QueryHeap::Reset(uint32 frameIndex)
{
	if (!IsInitialized())
		return;

	m_QueryIndex = 0;
	ID3D12CommandAllocator* pAllocator = m_CommandAllocators[frameIndex % m_FrameLatency];
	pAllocator->Reset();
	m_pCommandList->Reset(pAllocator, nullptr);
}



//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------


void CPUProfiler::Initialize(uint32 historySize, uint32 maxEvents)
{
	// Event indices are 16 bit, so max 2^16 events
	check(maxEvents < (1 << 16));

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
	uint32 newIndex = m_EventIndex.fetch_add(1);
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

	EventData& data = GetData();
	data.NumEvents = m_EventIndex;

	++m_FrameIndex;

	EventData& newData = GetData();
	newData.Allocator.Reset();
	newData.NumEvents = 0;
	m_EventIndex = 0;

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
}

#endif
