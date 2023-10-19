#pragma once
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/CommandQueue.h"
#include "Core/Profiler.h"

class ProfilerThing
{
public:
	void Initialize(ID3D12Device* pDevice, Span<ID3D12CommandQueue*> queues, uint32 sampleHistory, uint32 frameLatency, uint32 maxNumEvents, uint32 maxNumActiveCommandLists)
	{
		m_pResolveQueue = queues[0];
		m_FrameLatency = frameLatency;
		m_NumSampleHistory = sampleHistory;

		m_pSampleData = new EventFrame[sampleHistory];
		
		for(uint32 i = 0; i < sampleHistory; ++i)
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

						// Set the region's depth
						event.Depth = (uint16)stack.GetSize();
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

	URange GetAvailableFrameRange() const
	{
		uint32 endRange = m_FrameToReadback;
		uint32 startRange = endRange - Math::Min((uint32)m_NumSampleHistory, m_FrameIndex);
		return URange(startRange, endRange);
	}

	Span<const EventFrame::Event> GetSamplesForQueue(const QueueInfo& queue, uint32 frame) const
	{
		uint32 queueIndex = m_QueueIndexMap.at(queue.pQueue);
		const EventFrame& frameData = GetSampleFrame(frame);
		return frameData.EventsPerQueue[queueIndex];
	}

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

extern ProfilerThing gThing;

struct GPUEventScope
{
	GPUEventScope(const char* pFunction, const char* pFilePath, uint32 lineNumber, const char* pName, ID3D12GraphicsCommandList* pCmd)
		: pCmd(pCmd)
	{
		gThing.BeginEvent(pCmd, pName, pFilePath, lineNumber);
	}

	~GPUEventScope()
	{
		gThing.EndEvent(pCmd);
	}

	ID3D12GraphicsCommandList* pCmd;
};

#define GPU_SCOPE(...) GPUEventScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)



inline void ProfilerThingTest(GraphicsDevice* pDevice)
{
	CommandQueue* pDirectQueue = pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	CommandQueue* pComputeQueue = pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
	gThing.Initialize(pDevice->GetDevice(), { pDirectQueue->GetCommandQueue(), pComputeQueue->GetCommandQueue() }, 8, 3, 1024, 32);

	RefCountPtr<Buffer> pSource1 = pDevice->CreateBuffer(BufferDesc::CreateBuffer(64), "Source");
	RefCountPtr<Buffer> pDest1 = pDevice->CreateBuffer(BufferDesc::CreateBuffer(64), "Dest");

	// Test out-of-order event submitting

	for (int i = 0; i < 100; ++i)
	{
		gThing.Tick();

		CommandContext* pCmd1 = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		CommandContext* pCmd2 = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		CommandContext* pCmd3 = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

		if (i > 20)
			gThing.SetPaused(true);

		gThing.EndEvent(pCmd2->GetCommandList());
		gThing.EndEvent(pCmd3->GetCommandList());
		gThing.BeginEvent(pCmd1->GetCommandList(), "A");
		gThing.BeginEvent(pCmd1->GetCommandList(), "B");
		pCmd1->CopyResource(pSource1, pDest1);


		CommandContext* pCmdCompute1 = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		CommandContext* pCmdCompute2 = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);

		gThing.EndEvent(pCmdCompute2->GetCommandList());
		gThing.EndEvent(pCmdCompute2->GetCommandList());
		gThing.BeginEvent(pCmdCompute1->GetCommandList(), "Group Compute");
		gThing.BeginEvent(pCmdCompute1->GetCommandList(), "Compute A");
		pCmdCompute1->CopyResource(pSource1, pDest1);
		gThing.EndEvent(pCmdCompute1->GetCommandList());
		gThing.BeginEvent(pCmdCompute1->GetCommandList(), "Compute B");
		pCmdCompute1->CopyResource(pSource1, pDest1);

		gThing.ExecuteCommandLists(pDirectQueue->GetCommandQueue(), { pCmd1->GetCommandList(), pCmd2->GetCommandList(), pCmd3->GetCommandList() });
		SyncPoint direct = CommandContext::Execute({ pCmd1, pCmd2, pCmd3 });

		pComputeQueue->InsertWait(direct);

		gThing.ExecuteCommandLists(pComputeQueue->GetCommandQueue(), { pCmdCompute1->GetCommandList(), pCmdCompute2->GetCommandList() });
		SyncPoint compute = CommandContext::Execute({ pCmdCompute1, pCmdCompute2 });

		pDirectQueue->InsertWait(compute);
	}

	URange range = gThing.GetAvailableFrameRange();
	Span<const ProfilerThing::QueueInfo> queues = gThing.GetQueues();

	for (uint32 i = range.Begin; i < range.End; ++i)
	{
		E_LOG(Info, "\tFrame %d", i);
		for (const ProfilerThing::QueueInfo& queue : queues)
		{
			E_LOG(Info, "Queue: %s", queue.Name);

			Span<const ProfilerThing::EventFrame::Event> events = gThing.GetSamplesForQueue(queue, i);
			for (const auto& event : events)
			{
				E_LOG(Info, "\t\t%*s", event.Depth * 4 + strlen(event.pName), event.pName);
			}
		}
	}
}
