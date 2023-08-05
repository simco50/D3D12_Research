#pragma once
#include "Graphics/RHI/D3D.h"

// Linear Allocator
class LinearAllocator
{
public:
	LinearAllocator(uint32 size)
		: m_pData(new char[size]), m_Size(size), m_Offset(0)
	{
	}

	~LinearAllocator()
	{
		delete[] m_pData;
	}

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


////////////////////
/// GPU Profiler ///
////////////////////


class GPUTimeQueryHeap
{
public:
	void Initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pQueue, uint32 numQueries, uint32 numFrames)
	{
		pResolveQueue = pQueue;
		MaxNumQueries = numQueries;
		NumFrames = numFrames;

		ID3D12Device4* pDevice4 = nullptr;
		VERIFY_HR(pDevice->QueryInterface(&pDevice4));
		D3D12_COMMAND_LIST_TYPE commandListType = pQueue->GetDesc().Type;

		uint32 numQueryEntries = numQueries * 2;

		// Query heap that fits desired number of queries
		D3D12_QUERY_HEAP_DESC queryHeapDesc;
		queryHeapDesc.Count = numQueryEntries;
		queryHeapDesc.Type = commandListType == D3D12_COMMAND_LIST_TYPE_COPY ? D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		queryHeapDesc.NodeMask = 0;
		VERIFY_HR(pDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&pQueryHeap)));

		// Readback resource that fits all frames
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(numQueryEntries * sizeof(uint64) * numFrames);
		D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		VERIFY_HR(pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pReadbackResource)));
		void* pMappedAddress = nullptr;
		pReadbackResource->Map(0, nullptr, &pMappedAddress);
		m_pReadbackData = static_cast<uint64*>(pMappedAddress);

		pFrameData = new FrameData[numFrames];
		// Create CommandAllocator for each frame and store readback address
		for(uint32 i = 0; i < numFrames; ++i)
		{
			FrameData& frame = pFrameData[i];
			VERIFY_HR(pDevice->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&frame.pAllocator)));
			frame.QueryStartOffset = numQueryEntries * i;
		}

		// Create CommandList for ResolveQueryData
		VERIFY_HR(pDevice4->CreateCommandList1(0, commandListType, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&pResolveCommandList)));

		// Create Fence to check readback status
		VERIFY_HR(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));
		FenceEvent = CreateEventExA(nullptr, "Timestamp Query Fence", 0, EVENT_ALL_ACCESS);

		pDevice4->Release();
	}

	void Shutdown()
	{
		if (IsInitialized())
		{
			// Wait for the resolve queue to finish.
			pResolveQueue->Signal(pFence, ~0ull);
			pFence->SetEventOnCompletion(~0ull, FenceEvent);
			WaitForSingleObject(FenceEvent, INFINITE);

			// Destroy resources
			CloseHandle(FenceEvent);
			pQueryHeap->Release();
			pReadbackResource->Release();
			pResolveCommandList->Release();
			pFence->Release();

			for (uint32 i = 0; i < NumFrames; ++i)
				pFrameData[i].pAllocator->Release();
			delete[] pFrameData;
		}
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
		// Queue a resolve for the current frame
		FrameData& frame = GetData();
		if (frame.QueryIndex > 0)
		{
			pResolveCommandList->Reset(frame.pAllocator, nullptr);
			uint32 count = frame.QueryIndex * 2;
			pResolveCommandList->ResolveQueryData(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, count, pReadbackResource, frame.QueryStartOffset * sizeof(uint64));
			pResolveCommandList->Close();
			ID3D12CommandList* pResolveCommandLists[] = { pResolveCommandList };
			pResolveQueue->ExecuteCommandLists(1, pResolveCommandLists);
		}
		frame.ReadbackQueries = Span<uint64>(m_pReadbackData + frame.QueryStartOffset, frame.QueryIndex * 2);
		// Increment fence value, signal queue and store FenceValue in frame.
		++FenceValue;
		pResolveQueue->Signal(pFence, FenceValue);
		frame.FenceValue = FenceValue;

		// Advance to next frame and reset
		++FrameIndex;
		FrameData& newFrame = GetData();
		newFrame.QueryIndex = 0;

		// Don't allow the next frame to start until its resolve is finished.
		if (newFrame.FenceValue > pFence->GetCompletedValue())
		{
			pFence->SetEventOnCompletion(newFrame.FenceValue, FenceEvent);
			WaitForSingleObject(FenceEvent, INFINITE);
		}
	}

	// Return the view to the resolved queries and returns true if it's valid to read from
	bool GetResolvedQueries(uint32 frameIndex, Span<uint64>& outData)
	{
		const FrameData& data = pFrameData[frameIndex % NumFrames];
		if (data.FenceValue > pFence->GetCompletedValue())
			return false;
		outData = data.ReadbackQueries;
		return true;
	}

private:
	struct FrameData
	{
		Span<uint64> ReadbackQueries;						//< View to resolved query data
		ID3D12CommandAllocator* pAllocator;					//< CommandAllocator for this frame
		std::atomic<uint32> QueryIndex = 0;					//< Current number of queries
		uint64 FenceValue = 0;								//< FenceValue indicating when Resolve is finished
		uint32 QueryStartOffset = 0;						//< Offset in readback buffer to where queries start
	};

	FrameData& GetData()
	{
		return pFrameData[FrameIndex % NumFrames];
	}

	uint32 NumFrames = 0;
	FrameData* pFrameData = nullptr;
	ID3D12CommandQueue* pResolveQueue = nullptr;
	ID3D12GraphicsCommandList* pResolveCommandList = nullptr;
	ID3D12QueryHeap* pQueryHeap = nullptr;
	ID3D12Resource* pReadbackResource = nullptr;
	uint32 FrameIndex = 0;
	uint32 MaxNumQueries = 0;
	const uint64* m_pReadbackData = nullptr;

	ID3D12Fence* pFence = nullptr;
	uint64 FenceValue = 0;
	HANDLE FenceEvent = nullptr;
};

extern class GPUProfiler gGPUProfiler;

#define FOO_GPU_SCOPE(...) FooGPUProfileScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__VA_ARGS__)

class GPUProfiler
{
public:
	void Initialize(ID3D12Device* pDevice, Span<ID3D12CommandQueue*> queues)
	{
		for (ID3D12CommandQueue* pQueue : queues)
		{
			D3D12_COMMAND_QUEUE_DESC queueDesc = pQueue->GetDesc();
			bool isCopyQueue = queueDesc.Type == D3D12_COMMAND_LIST_TYPE_COPY;

			QueueInfo& queueInfo = m_Queues.emplace_back();
			queueInfo.IsCopyQueue = isCopyQueue;
			uint32 size = ARRAYSIZE(queueInfo.Name);
			pQueue->GetPrivateData(WKPDID_D3DDebugObjectName, &size, queueInfo.Name);
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
		region.pName = data.Allocator.String(pName);
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
		// While the next frame to resolve is not the last one, attempt access the readback data and advance.
		while (m_FrameToResolve < m_FrameIndex)
		{
			// #todo: One readback may be finished while the other is not. Could maybe cause problems?
			Span<uint64> copyQueries, mainQueries;
			bool copiesValid = !m_CopyQueryHeap.IsInitialized() || m_CopyQueryHeap.GetResolvedQueries(m_FrameToResolve, copyQueries);
			bool mainValid = !m_MainQueryHeap.IsInitialized() || m_MainQueryHeap.GetResolvedQueries(m_FrameToResolve, mainQueries);
			if (!copiesValid || !mainValid)
				break;

			// Copy the timing data
			SampleHistory& data = m_SampleData[m_FrameToResolve % m_SampleData.size()];
			check(copyQueries.GetSize() + mainQueries.GetSize() == data.CurrentIndex * 2);
			for (uint32 i = 0; i < data.CurrentIndex; ++i)
			{
				SampleRegion& region = data.Regions[i];
				const QueueInfo& queue = m_Queues[region.QueueIndex];
				const Span<uint64>& queries = queue.IsCopyQueue ? copyQueries : mainQueries;
				region.BeginTicks = queries[region.TimerIndex * 2 + 0];
				region.EndTicks = queries[region.TimerIndex * 2 + 1];
			}
			data.NumRegions = data.CurrentIndex;

			// Sort the regions and resolve the stack depth
			uint32 Depth = 0;
			std::array<uint32, 64> Stack;
			std::sort(data.Regions.begin(), data.Regions.begin() + data.NumRegions, [](const SampleRegion& a, const SampleRegion& b) { return a.BeginTicks < b.BeginTicks; });
			for (uint32 i = 0; i < data.NumRegions; ++i)
			{
				SampleRegion& region = data.Regions[i];

				// While there is a parent and the current region starts after the parent ends, pop it off the stack
				while (Depth > 0)
				{
					const SampleRegion* pParent = &data.Regions[Stack[Depth - 1]];
					if (region.BeginTicks >= pParent->EndTicks)
					{
						--Depth;
					}
					else
					{
						check(region.EndTicks <= pParent->EndTicks);
						break;
					}
				}

				Stack[Depth] = i;

				// Set the region's depth
				region.Depth = Depth;
				++Depth;
			}

			++m_FrameToResolve;
		}

		if (m_Paused)
			return;

		// Make sure all last frame's regions have ended
		for (const TLS* pTLS : m_ThreadData)
			check(pTLS->RegionDepth == 0);

		// Schedule a resolve for last frame
		if (m_CopyQueryHeap.IsInitialized())
			m_CopyQueryHeap.Resolve();
		if (m_MainQueryHeap.IsInitialized())
			m_MainQueryHeap.Resolve();

		// Advance frame and clear data
		++m_FrameIndex;
		SampleHistory& newFrameData = GetData();
		newFrameData.CurrentIndex = 0;
		newFrameData.NumRegions = 0;
		newFrameData.Allocator.Reset();
	}

	void Shutdown()
	{
		m_MainQueryHeap.Shutdown();
		m_CopyQueryHeap.Shutdown();
	}

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

		char Name[128];
		bool IsCopyQueue;
		ID3D12CommandQueue* pQueue;

	private:
		uint64 GPUCalibrationTicks;
		uint64 CPUCalibrationTicks;
		uint64 GPUFrequency;
		uint64 CPUFrequency;
	};

	struct SampleRegion
	{
		const char* pName = "";			//< Name of the region
		uint32 Depth = 0;				//< Stack depth of the region
		uint64 BeginTicks = 0;			//< GPU ticks of start of the region
		uint64 EndTicks = 0;			//< GPU ticks of end of the region
		uint32 QueueIndex = 0xFFFFFFFF;	//< The index of the queue this region is executed on (QueueInfo)
		uint32 TimerIndex = 0xFFFFFFFF;	//< The index in the query heap for the timer
	};

	struct SampleHistory
	{
		SampleHistory()
			: Allocator(1 << 16)
		{}

		std::array<SampleRegion, 1024> Regions;
		uint32 NumRegions = 0;
		std::atomic<uint32> CurrentIndex = 0;		//< The index to the next free sample region
		LinearAllocator Allocator;
	};

	const Span<QueueInfo> GetQueueInfo() const { return m_Queues; }

	template<typename Fn>
	void ForEachHistory(Fn&& fn)
	{
		uint32 leadingFrames = m_FrameIndex - m_FrameToResolve - 1;
		uint32 currentIndex = m_FrameIndex - Math::Min(m_FrameIndex, (uint32)m_SampleData.size()) - leadingFrames;
		while (currentIndex < m_FrameToResolve)
		{
			fn(currentIndex, m_SampleData[currentIndex % m_SampleData.size()]);
			++currentIndex;
		}
	}

	bool m_Paused = false;

private:
	SampleHistory& GetData() { return m_SampleData[m_FrameIndex % m_SampleData.size()]; }

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
	std::array<SampleHistory, 5> m_SampleData;
	uint32 m_FrameIndex = 0;
	uint32 m_FrameToResolve = 0;
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



////////////////////
/// CPU Profiler ///
////////////////////

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
	static constexpr int REGION_HISTORY = 5;
	static constexpr int MAX_DEPTH = 32;
	static constexpr int STRING_BUFFER_SIZE = 1 << 16;
	static constexpr int MAX_NUM_REGIONS = 1024;

	FooProfiler()
		: m_HistorySize(REGION_HISTORY)
	{
		m_pSampleHistory = new SampleHistory[REGION_HISTORY];
	}

	~FooProfiler()
	{
		delete[] m_pSampleHistory;
	}

	void BeginRegion(const char* pName, uint32 color, const char* pFilePath = nullptr, uint32 lineNumber = 0)
	{
		SampleHistory& data = GetCurrentData();
		uint32 newIndex = data.CurrentIndex.fetch_add(1);
		check(newIndex < data.Regions.size());

		TLS& tls = GetTLS();
		check(tls.Depth < ARRAYSIZE(tls.RegionStack));

		SampleRegion& newRegion = data.Regions[newIndex];
		newRegion.Depth = tls.Depth;
		newRegion.ThreadIndex = tls.ThreadIndex;
		newRegion.pName = data.Allocator.String(pName);
		newRegion.Color = color;
		newRegion.pFilePath = pFilePath;
		newRegion.LineNumber = lineNumber;
		QueryPerformanceCounter((LARGE_INTEGER*)(&newRegion.BeginTicks));

		tls.RegionStack[tls.Depth] = newIndex;
		tls.Depth++;
	}

	void BeginRegion(const char* pName, const char* pFilePath = nullptr, uint32 lineNumber = 0)
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
		BeginRegion(pName, color, pFilePath, lineNumber);
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
		data.CurrentIndex = 0;
		data.Allocator.Reset();

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
		SampleHistory()
			: Allocator(STRING_BUFFER_SIZE)
		{}

		uint64 TicksBegin = 0;								//< The start ticks of the frame on the main thread
		uint64 TicksEnd = 0;								//< The end ticks of the frame on the main thread
		std::array<SampleRegion, MAX_NUM_REGIONS> Regions;	//< All sample regions of the frame
		std::atomic<uint32> CurrentIndex = 0;				//< The index to the next free sample region
		LinearAllocator	Allocator;
	};

	SampleHistory& GetCurrentData()
	{
		return m_pSampleHistory[m_FrameIndex % m_HistorySize];
	}

	template<typename Fn>
	void ForEachHistory(Fn&& fn) const
	{
		uint32 currentIndex = m_FrameIndex - Math::Min(m_FrameIndex, m_HistorySize) + 1;
		while (currentIndex < m_FrameIndex)
		{
			fn(currentIndex, m_pSampleHistory[currentIndex % m_HistorySize]);
			++currentIndex;
		}
	}

	const SampleHistory& GetHistory() const
	{
		uint32 currentIndex = (m_FrameIndex + 1) % m_HistorySize;
		return m_pSampleHistory[currentIndex];
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
	uint32 m_HistorySize = 0;
	SampleHistory* m_pSampleHistory = nullptr;
};

struct FooProfileScope
{
	// Name + Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName, const Color& color)
	{
		gProfiler.BeginRegion(pName ? pName : pFunctionName, Math::Pack_RGBA8_UNORM(color), pFilePath, lineNumber);
	}

	// Just Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const Color& color)
	{
		gProfiler.BeginRegion(pFunctionName, Math::Pack_RGBA8_UNORM(color), pFilePath, lineNumber);
	}

	// Just Name
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName)
	{
		gProfiler.BeginRegion(pName, pFilePath, lineNumber);
	}

	// No Name or Color
	FooProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber)
	{
		gProfiler.BeginRegion(pFunctionName, pFilePath, lineNumber);
	}

	~FooProfileScope()
	{
		gProfiler.EndRegion();
	}
};

