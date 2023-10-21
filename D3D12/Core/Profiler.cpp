
#include "stdafx.h"
#include "Profiler.h"

CPUProfiler gCPUProfiler;
GPUProfiler gGPUProfiler;

//-----------------------------------------------------------------------------
// [SECTION] GPU Profiler
//-----------------------------------------------------------------------------

void GPUProfiler::Initialize(ID3D12Device* pDevice, Span<ID3D12CommandQueue*> queues, uint32 sampleHistory, uint32 frameLatency, uint32 maxNumEvents, uint32 maxNumActiveCommandLists)
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

void GPUProfiler::Shutdown()
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


void GPUProfiler::BeginEvent(ID3D12GraphicsCommandList* pCmd, const char* pName, const char* pFilePath, uint32 lineNumber)
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


void GPUProfiler::EndEvent(ID3D12GraphicsCommandList* pCmd)
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

void GPUProfiler::Tick()
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
				EventFrame::Event& event = sampleData.Events[i];
				event.TicksBegin = pQueries[queryEvent.QueryIndexBegin];
				event.TicksEnd = pQueries[queryEvent.QueryIndexEnd];
			}

			std::vector<EventFrame::Event>& events = sampleData.Events;
			std::sort(events.begin(), events.begin() + sampleData.NumEvents, [](const EventFrame::Event& a, const EventFrame::Event& b)
				{
					if (a.QueueIndex == b.QueueIndex)
					{
						if (a.TicksBegin == b.TicksBegin)
						{
							// If the begin and end time is the same, sort by index to make the sort stable
							if (a.TicksEnd == b.TicksEnd)
								return a.Index < b.Index;

							// An event with zero length is a special case. Assume it comes first
							bool aZero = a.TicksBegin == a.TicksEnd;
							bool bZero = b.TicksBegin == b.TicksEnd;
							if (aZero != bZero)
								return aZero > bZero;

							// If the start time is the same, the one with the longest duration will be first
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

					// While there is a parent and the current event starts after the parent ends, pop it off the stack
					while (stack.GetSize() > 0)
					{
						const EventFrame::Event& parent = events[stack.Top()];
						if (event.TicksBegin >= parent.TicksEnd)
						{
							stack.Pop();
						}
						else
						{
							check(event.TicksEnd <= parent.TicksEnd);
							break;
						}
					}

					// Set the event's depth
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

void GPUProfiler::ExecuteCommandLists(ID3D12CommandQueue* pQueue, Span<ID3D12CommandList*> commandLists)
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


//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------


void CPUProfiler::Initialize(uint32 historySize, uint32 maxSamples)
{
	Shutdown();

	m_pSampleData = new EventFrame[historySize];
	m_HistorySize = historySize;

	for (uint32 i = 0; i < historySize; ++i)
		m_pSampleData[i].Events.resize(maxSamples);
}


void CPUProfiler::Shutdown()
{
	delete[] m_pSampleData;
}


void CPUProfiler::BeginEvent(const char* pName, const char* pFilePath, uint32 lineNumber)
{
	if(m_EventCallback.OnEventBegin)
		m_EventCallback.OnEventBegin(pName, m_EventCallback.pUserData);

	if (m_Paused)
		return;

	EventFrame& data = GetData();
	uint32 newIndex = data.NumEvents.fetch_add(1);
	check(newIndex < data.Events.size());

	TLS& tls = GetTLS();

	EventFrame::Event& newEvent = data.Events[newIndex];
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

	EventFrame::Event& event = GetData().Events[GetTLS().EventStack.Pop()];
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
	EventFrame& frame = GetData();
	std::vector<EventFrame::Event>& events = frame.Events;
	std::sort(events.begin(), events.begin() + frame.NumEvents, [](const EventFrame::Event& a, const EventFrame::Event& b)
		{
			return a.ThreadIndex < b.ThreadIndex;
		});

	uint32 eventStart = 0;
	for (uint32 threadIndex = 0; threadIndex < (uint32)m_ThreadData.size(); ++threadIndex)
	{
		uint32 eventEnd = eventStart;
		while (events[eventEnd].ThreadIndex == threadIndex && eventEnd < frame.NumEvents)
			++eventEnd;

		if(threadIndex < (uint32)frame.EventsPerThread.size())
			frame.EventsPerThread[threadIndex] = Span<const EventFrame::Event>(&events[eventStart], eventEnd - eventStart);
		eventStart = eventEnd;
	}

	++m_FrameIndex;

	EventFrame& newData = GetData();
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
		m_pSampleData[i].EventsPerThread.resize(m_ThreadData.size());
}
