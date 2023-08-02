#pragma once
#include "Graphics/RHI/D3D.h"

extern class GPUProfiler gGPUProfiler;

#define FOO_GPU_SCOPE(...) FooGPUProfileScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__VA_ARGS__)

class GPUTimeQueryHeap
{
public:
	void Initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pQueue, uint32 numQueries, uint32 numFrames)
	{
		pResolveQueue = pQueue;
		MaxNumQueries = numQueries;
		NumFrames = numFrames;

		RefCountPtr<ID3D12Device4> pDevice4;
		VERIFY_HR(pDevice->QueryInterface(pDevice4.GetAddressOf()));
		D3D12_COMMAND_LIST_TYPE commandListType = pQueue->GetDesc().Type;

		uint32 numQueryEntries = numQueries * 2;

		// Query heap that fits desired number of queries
		D3D12_QUERY_HEAP_DESC queryHeapDesc;
		queryHeapDesc.Count = numQueryEntries;
		queryHeapDesc.Type = commandListType == D3D12_COMMAND_LIST_TYPE_COPY ? D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		queryHeapDesc.NodeMask = 0;
		VERIFY_HR(pDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(pQueryHeap.GetAddressOf())));

		// Readback resource that fits all frames
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(numQueryEntries * sizeof(uint64) * numFrames);
		D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		VERIFY_HR(pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(pReadbackResource.GetAddressOf())));
		void* pMappedAddress = nullptr;
		pReadbackResource->Map(0, nullptr, &pMappedAddress);

		pFrameData = new FrameData[numFrames];
		// Create CommandAllocator for each frame and store readback address
		for(uint32 i = 0; i < numFrames; ++i)
		{
			FrameData& frame = pFrameData[i];
			VERIFY_HR(pDevice->CreateCommandAllocator(commandListType, IID_PPV_ARGS(frame.pAllocator.GetAddressOf())));
			frame.QueryStart = numQueryEntries * i;
			frame.ReadbackQueries = Span(static_cast<uint64*>(pMappedAddress) + frame.QueryStart, numQueryEntries);
		}

		// Create CommandList for ResolveQueryData
		VERIFY_HR(pDevice4->CreateCommandList1(0, commandListType, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(pResolveCommandList.GetAddressOf())));

		// Create Fence to check readback status
		VERIFY_HR(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(pFence.GetAddressOf())));
		FenceEvent = CreateEventExA(nullptr, "Timestamp Query Fence", 0, EVENT_ALL_ACCESS);
	}

	void Shutdown()
	{
		// Wait for the resolve queue to finish.
		pResolveQueue->Signal(pFence, ~0ull);
		pFence->SetEventOnCompletion(~0ull, FenceEvent);
		WaitForSingleObject(FenceEvent, INFINITE);

		// Destroy resources
		CloseHandle(FenceEvent);
		pQueryHeap.Reset();
		pReadbackResource.Reset();
		delete[] pFrameData;
		pResolveCommandList.Reset();
		pFence.Reset();
	}

	uint32 QueryBegin(ID3D12GraphicsCommandList* pCommandList)
	{
		FrameData& frame = GetData();
		uint32 index = frame.QueryIndex.fetch_add(1);
		check(index < MaxNumQueries);
		pCommandList->EndQuery(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2);
		return index;
	}

	void EndQuery(uint32 index, ID3D12GraphicsCommandList* pCommandList)
	{
		check(index >= 0 && index < MaxNumQueries);
		pCommandList->EndQuery(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2 + 1);
	}

	bool IsInitialized()
	{
		return pResolveCommandList;
	}

	void Resolve()
	{
		FrameData& frame = GetData();

		// Don't start resolve until the current resolve is complete
		if (frame.FenceValue > pFence->GetCompletedValue())
		{
			pFence->SetEventOnCompletion(frame.FenceValue, FenceEvent);
			WaitForSingleObject(FenceEvent, INFINITE);
		}

		if (frame.QueryIndex > 0)
		{
			pResolveCommandList->Reset(frame.pAllocator, nullptr);
			uint32 count = frame.QueryIndex * 2;
			pResolveCommandList->ResolveQueryData(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, count, pReadbackResource, frame.QueryStart * sizeof(uint64));
			pResolveCommandList->Close();
			ID3D12CommandList* pResolveCommandLists[] = { pResolveCommandList.Get() };
			pResolveQueue->ExecuteCommandLists(1, pResolveCommandLists);
		}
		frame.NumResolvedQueries = frame.QueryIndex;
		frame.FenceValue = FenceValue;
		pResolveQueue->Signal(pFence, FenceValue++);
		++FrameIndex;

		FrameData& newFrame = GetData();
		newFrame.QueryIndex = 0;
	}

	bool GetResolvedQueries(uint32 index, Span<uint64>& outData)
	{
		const FrameData& data = pFrameData[index % NumFrames];
		if (data.FenceValue > pFence->GetCompletedValue())
			return false;
		outData = data.ReadbackQueries.Subspan(0, data.NumResolvedQueries * 2);
		return true;
	}

private:
	struct FrameData
	{
		RefCountPtr<ID3D12CommandAllocator> pAllocator;
		Span<uint64> ReadbackQueries;
		std::atomic<uint32> QueryIndex = 0;
		uint64 FenceValue = 0;
		uint32 QueryStart = 0;
		uint32 NumResolvedQueries = 0;
	};

	FrameData& GetData()
	{
		return pFrameData[FrameIndex % NumFrames];
	}

	uint32 NumFrames = 0;
	FrameData* pFrameData = nullptr;
	ID3D12CommandQueue* pResolveQueue;
	RefCountPtr<ID3D12GraphicsCommandList> pResolveCommandList;
	RefCountPtr<ID3D12QueryHeap> pQueryHeap;
	RefCountPtr<ID3D12Resource> pReadbackResource;
	uint32 FrameIndex = 0;
	uint32 MaxNumQueries;

	RefCountPtr<ID3D12Fence> pFence;
	uint64 FenceValue = 1;
	HANDLE FenceEvent = nullptr;
};

class GPUProfiler
{
public:
	void Initialize(ID3D12Device4* pDevice, Span<ID3D12CommandQueue*> queues)
	{
		for (ID3D12CommandQueue* pQueue : queues)
		{
			D3D12_COMMAND_QUEUE_DESC queueDesc = pQueue->GetDesc();
			bool isCopyQueue = queueDesc.Type == D3D12_COMMAND_LIST_TYPE_COPY;

			QueueInfo& queueInfo = m_Queues.emplace_back();
			queueInfo.IsCopyQueue = isCopyQueue;
			queueInfo.Name = D3D::GetObjectName(pQueue);
			queueInfo.pQueue = pQueue;
			queueInfo.InitCalibration();

			if (!m_CopyQueryHeap.IsInitialized() && isCopyQueue)
				m_CopyQueryHeap.Initialize(pDevice, pQueue, 1024, 4);
			if (!m_MainQueryHeap.IsInitialized() && !isCopyQueue)
				m_MainQueryHeap.Initialize(pDevice, pQueue, 1024, 4);
		}
	}

	void BeginRegion(const char* pName, ID3D12GraphicsCommandList* pCmd, uint32 queueIndex)
	{
		if (m_Paused)
			return;

		bool isCopyQueue = m_Queues[queueIndex].IsCopyQueue;

		SampleHistory& data = m_SampleData[m_FrameIndex % m_SampleData.size()];
		uint32 index = data.CurrentIndex.fetch_add(1);
		check(index < data.Regions.size());
		SampleRegion& region = data.Regions[index];
		region.pName = pName;
		region.QueueIndex = queueIndex;
		region.TimerIndex = GetHeap(isCopyQueue).QueryBegin(pCmd);

		TLS& tls = GetTLS();
		check(tls.RegionDepth >= 0);
		TLS::StackData& stackData = tls.RegionStack[tls.RegionDepth];
		stackData.pCommandList = pCmd;
		stackData.RegionIndex = index;
		++tls.RegionDepth;
		check(tls.RegionDepth < ARRAYSIZE(tls.RegionStack))
	}

	void EndRegion()
	{
		if (m_Paused)
			return;

		TLS& tls = GetTLS();
		check(tls.RegionDepth > 0);
		--tls.RegionDepth;
		SampleHistory& data = m_SampleData[m_FrameIndex % m_SampleData.size()];
		const TLS::StackData& stackData = tls.RegionStack[tls.RegionDepth];
		const SampleRegion& region = data.Regions[stackData.RegionIndex];
		bool isCopyQueue = m_Queues[region.QueueIndex].IsCopyQueue;
		GetHeap(isCopyQueue).EndQuery(region.TimerIndex, stackData.pCommandList);
	}

	GPUTimeQueryHeap& GetHeap(bool isCopy)
	{
		return isCopy ? m_CopyQueryHeap : m_MainQueryHeap;
	}

	void Tick()
	{
		if (m_Paused)
			return;

		for (const TLS* pTLS : m_ThreadData)
			check(pTLS->RegionDepth == 0);

		if (m_CopyQueryHeap.IsInitialized())
			m_CopyQueryHeap.Resolve();
		if (m_MainQueryHeap.IsInitialized())
			m_MainQueryHeap.Resolve();

		++m_FrameIndex;

		while (m_LastResolvedFrame < m_FrameIndex - 1)
		{
			Span<uint64> copyQueries, mainQueries;
			bool copiesValid = !m_CopyQueryHeap.IsInitialized() || m_CopyQueryHeap.GetResolvedQueries(m_LastResolvedFrame, copyQueries);
			bool mainValid = !m_MainQueryHeap.IsInitialized() || m_MainQueryHeap.GetResolvedQueries(m_LastResolvedFrame, mainQueries);
			if (!copiesValid || !mainValid)
				break;
			SampleHistory& data = m_SampleData[m_LastResolvedFrame % m_SampleData.size()];
			check(copyQueries.GetSize() + mainQueries.GetSize() == data.CurrentIndex * 2);
			for (uint32 i = 0; i < data.CurrentIndex; ++i)
			{
				SampleRegion& region = data.Regions[i];
				const QueueInfo& queue = m_Queues[region.QueueIndex];
				const Span<uint64>& queries = queue.IsCopyQueue ? copyQueries : mainQueries;
				region.BeginTicks = queries[region.TimerIndex * 2 + 0];
				region.EndTicks = queries[region.TimerIndex * 2 + 1];
			}
			data.ResolvedRegions = data.CurrentIndex;
			++m_LastResolvedFrame;
		}

		m_SampleData[m_FrameIndex % m_SampleData.size()].CurrentIndex = 0;
		m_SampleData[m_FrameIndex % m_SampleData.size()].ResolvedRegions = 0;
	}

	void Shutdown()
	{
		m_MainQueryHeap.Shutdown();
	}

	struct QueueInfo
	{
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

		std::string Name;
		uint64 GPUCalibrationTicks;
		uint64 CPUCalibrationTicks;
		uint64 GPUFrequency;
		uint64 CPUFrequency;
		bool IsCopyQueue;
		ID3D12CommandQueue* pQueue;
	};

	struct SampleRegion
	{
		const char* pName;
		uint32 Depth = 0; 
		uint64 BeginTicks;
		uint64 EndTicks;
		uint32 QueueIndex;
		uint32 TimerIndex;
	};

	struct SampleHistory
	{
		std::array<SampleRegion, 1024> Regions;
		std::atomic<uint32> CurrentIndex = 0;				//< The index to the next free sample region
		uint32 ResolvedRegions = 0;
		std::atomic<uint32> CharIndex = 0;					//< The index to the next free char buffer
		char StringBuffer[1 << 16];							//< Blob to store dynamic strings for the frame
	};

	const Span<QueueInfo> GetQueueInfo() const { return m_Queues; }

	template<typename Fn>
	void ForEachHistory(Fn&& fn)
	{
		for (SampleHistory& data : m_SampleData)
		{
			if (data.ResolvedRegions > 0)
				fn(data);
		}
	}

	bool m_Paused = false;

private:
	struct TLS
	{
		struct StackData
		{
			uint32 RegionIndex;
			ID3D12GraphicsCommandList* pCommandList;
		};
		StackData RegionStack[64];
		uint32 RegionDepth = 0;
		bool IsInitialized = false;
	};

	TLS& GetTLS()
	{
		static thread_local TLS tls;
		if (!tls.IsInitialized)
		{
			tls.IsInitialized = true;
			std::lock_guard lock(m_ThreadDataMutex);
			m_ThreadData.push_back(&tls);
		}
		return tls;
	}

	std::mutex m_ThreadDataMutex;
	std::vector<const TLS*> m_ThreadData;
	std::vector<QueueInfo> m_Queues;
	GPUTimeQueryHeap m_MainQueryHeap;
	GPUTimeQueryHeap m_CopyQueryHeap;
	std::array<SampleHistory, 10> m_SampleData;
	uint32 m_FrameIndex = 0;
	uint32 m_LastResolvedFrame = 0;
};

struct FooGPUProfileScope
{
	FooGPUProfileScope(const char* pName, ID3D12GraphicsCommandList* pCmd)
	{
		gGPUProfiler.BeginRegion(pName, pCmd, 0);
	}

	~FooGPUProfileScope()
	{
		gGPUProfiler.EndRegion();
	}
};














/* Options:
	FOO_SCOPE(const char* pName, const Color& color)
	FOO_SCOPE(const Color& color)
	FOO_SCOPE(const char* pName)
	FOO_SCOPE()
*/
#define FOO_SCOPE(...) FooProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

/* Options:
	FOO_REGISTER_THREAD(const char* pName)
	FOO_REGISTER_THREAD()
*/
#define FOO_REGISTER_THREAD(...) gProfiler.RegisterThread(__VA_ARGS__)

/* Options:
	FOO_FRAME()
*/
#define FOO_FRAME() gProfiler.Tick();

extern class FooProfiler gProfiler;

class FooProfiler
{
public:
	static constexpr int REGION_HISTORY = 4;
	static constexpr int MAX_DEPTH = 32;
	static constexpr int STRING_BUFFER_SIZE = 1 << 16;
	static constexpr int MAX_NUM_REGIONS = 1024;

	FooProfiler();

	void BeginRegion(const char* pName, uint32 color)
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
		newRegion.Color = color;
		QueryPerformanceCounter((LARGE_INTEGER*)(&newRegion.BeginTicks));

		tls.RegionStack[tls.Depth] = newIndex;
		tls.Depth++;
	}

	void BeginRegion(const char* pName)
	{
		// Add a region and inherit the color
		TLS& tls = GetTLS();
		check(tls.Depth < ARRAYSIZE(tls.RegionStack));
		uint32 color = 0xFFFFFFFF;
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
		gGPUProfiler.Tick();

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
		gProfiler.BeginRegion(pName ? pName : pFunctionName, Math::Pack_RGBA8_UNORM(color));
		gProfiler.SetFileInfo(pFilePath, lineNumber);
	}

	// Just Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const Color& color)
	{
		gProfiler.BeginRegion(pFunctionName, Math::Pack_RGBA8_UNORM(color));
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

