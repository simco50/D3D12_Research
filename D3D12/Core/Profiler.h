#pragma once
#include "Graphics/RHI/D3D.h"

#define GPU_PROFILE_SCOPE(name, commandlist)	PROFILE_GPU_SCOPE(name, (commandlist)->GetCommandList())
#define GPU_PROFILE_BEGIN(name, commandlist)	PROFILE_GPU_BEGIN(name, (commandlist)->GetCommandList())
#define GPU_PROFILE_END()						PROFILE_GPU_END()
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
#define PROFILE_CPU_SCOPE(...)							CPUProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

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
#define PROFILE_GPU_SCOPE(...)							GPUProfileScope MACRO_CONCAT(gpu_profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

// Usage:
//		PROFILE_GPU_BEGIN(const char* pName, ID3D12GraphicsCommandList* pCommandList, uint32 queueIndex)
//		PROFILE_GPU_BEGIN(const char* pName, ID3D12GraphicsCommandList* pCommandList)
#define PROFILE_GPU_BEGIN(name, cmdlist, ...)			gGPUProfiler.PushRegion(name, cmdlist, __VA_ARGS__);
// Usage:
//		PROFILE_GPU_END()
#define PROFILE_GPU_END()								gGPUProfiler.PopRegion();

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
	void(*OnEventBegin)(const char* /*pName*/, ID3D12GraphicsCommandList* /*CommandList*/, uint16 /*queueIndex*/);
	void(*OnEventEnd)(const char* /*pName*/, ID3D12GraphicsCommandList* /*CommandList*/, uint16 /*queueIndex*/);
};

class GPUProfiler
{
public:
	static constexpr uint32 MAX_NUM_MAIN_REGIONS = 1024;
	static constexpr uint32 MAX_NUM_COPY_QUEUE_REGIONS = 1024;
	static constexpr uint32 MAX_NUM_REGIONS = MAX_NUM_MAIN_REGIONS + MAX_NUM_COPY_QUEUE_REGIONS;
	static constexpr uint32 NUM_GPU_FRAMES = 3;
	static constexpr uint32 HISTORY_SIZE = 5;
	static constexpr uint32 MAX_DEPTH = 32;

	static constexpr uint16 INVALID_QUEUE = 0xFFFF;

private:
	// Query heap providing the mechanism to record timestamp events on the GPU and resolving them for readback
	class GPUTimeQueryHeap
	{
	public:
		// Initialize the query heap using the provided CommandQueue to resolve queries
		void Initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pQueue, uint32 numQueries, uint32 numFrames)
		{
			pResolveQueue = pQueue;
			MaxNumQueries = numQueries;
			NumFrames = numFrames;

			D3D12_COMMAND_LIST_TYPE commandListType = pQueue->GetDesc().Type;
			uint32 numQueryEntries = numQueries * 2;

			{
				// Query heap that fits desired number of queries
				D3D12_QUERY_HEAP_DESC queryHeapDesc{};
				queryHeapDesc.Count = numQueryEntries;
				queryHeapDesc.Type = commandListType == D3D12_COMMAND_LIST_TYPE_COPY ? D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
				queryHeapDesc.NodeMask = 0;
				VERIFY_HR(pDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&pQueryHeap)));
			}
			{
				// Readback resource that fits all frames
				D3D12_RESOURCE_DESC resourceDesc{};
				resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				resourceDesc.Width = numQueryEntries * sizeof(uint64) * numFrames;
				resourceDesc.Height = 1;
				resourceDesc.DepthOrArraySize = 1;
				resourceDesc.MipLevels = 1;
				resourceDesc.SampleDesc.Count = 1;
				resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				D3D12_HEAP_PROPERTIES heapProperties{};
				heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
				VERIFY_HR(pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pReadbackResource)));
				void* pMappedAddress = nullptr;
				pReadbackResource->Map(0, nullptr, &pMappedAddress);
				m_pReadbackData = static_cast<uint64*>(pMappedAddress);
			}
			{
				// Allocate frame data for each frame
				pFrameData = new FrameData[numFrames];

				// Create CommandAllocator for each frame and store readback address
				for (uint32 i = 0; i < numFrames; ++i)
				{
					FrameData& frame = pFrameData[i];
					VERIFY_HR(pDevice->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&frame.pAllocator)));
					frame.QueryStartOffset = numQueryEntries * i;
				}
			}
			{
				// Create CommandList for ResolveQueryData
				VERIFY_HR(pDevice->CreateCommandList(0, commandListType, pFrameData[0].pAllocator, nullptr, IID_PPV_ARGS(&pResolveCommandList)));
			}
			{
				// Create Fence to check readback status
				VERIFY_HR(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));
				FenceEvent = CreateEventExA(nullptr, "Timestamp Query Fence", 0, EVENT_ALL_ACCESS);
			}
		}

		void Shutdown()
		{
			if (pResolveCommandList)
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

		// Start a timestamp query and return the index of the query
		uint32 QueryBegin(ID3D12GraphicsCommandList* pCommandList)
		{
			FrameData& frame = GetData();
			uint32 index = frame.QueryIndex.fetch_add(1);
			check(index < MaxNumQueries);
			pCommandList->EndQuery(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2);
			return index;
		}

		// End a timestamp query of the provided index previously returned in QueryBegin()
		void EndQuery(uint32 index, ID3D12GraphicsCommandList* pCommandList)
		{
			check(index < MaxNumQueries);
			pCommandList->EndQuery(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index * 2 + 1);
		}

		// Queues a ResolveQueryData of the last frame and advances to the next frame
		void Resolve()
		{
			if (!IsInitialized())
				return;

			// Queue a resolve for the current frame
			FrameData& frame = GetData();
			if (frame.QueryIndex > 0)
			{
				uint32 count = frame.QueryIndex * 2;
				pResolveCommandList->ResolveQueryData(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, count, pReadbackResource, frame.QueryStartOffset * sizeof(uint64));
				pResolveCommandList->Close();
				ID3D12CommandList* pResolveCommandLists[] = { pResolveCommandList };
				pResolveQueue->ExecuteCommandLists(1, pResolveCommandLists);
			}
			else
			{
				pResolveCommandList->Close();
			}
			frame.ReadbackQueries = Span<const uint64>(m_pReadbackData + frame.QueryStartOffset, frame.QueryIndex * 2);
			// Increment fence value, signal queue and store FenceValue in frame.
			++FenceValue;
			pResolveQueue->Signal(pFence, FenceValue);
			frame.FenceValue = FenceValue;

			// Advance to next frame and reset
			++FrameIndex;
			FrameData& newFrame = GetData();
			
			// Don't allow the next frame to start until its resolve is finished.
			if (!IsFenceComplete(newFrame.FenceValue))
			{
				check(false, "Resolve() should not have to wait for the resolve of the upcoming frame to finish. Increase NumFrames.");
				pFence->SetEventOnCompletion(newFrame.FenceValue, FenceEvent);
				WaitForSingleObject(FenceEvent, INFINITE);
			}

			newFrame.QueryIndex = 0;
			newFrame.pAllocator->Reset();
			pResolveCommandList->Reset(newFrame.pAllocator, nullptr);
		}

		// Return the view to the resolved queries and returns true if it's valid to read from
		bool GetResolvedQueries(uint32 frameIndex, Span<const uint64>& outData)
		{
			if (!IsInitialized())
				return true;

			const FrameData& data = pFrameData[frameIndex % NumFrames];
			if (!IsFenceComplete(data.FenceValue))
				return false;
			outData = data.ReadbackQueries;
			return true;
		}

		bool IsFenceComplete(uint64 fenceValue)
		{
			if (fenceValue <= LastCompletedFence)
				return true;
			LastCompletedFence = Math::Max(LastCompletedFence, pFence->GetCompletedValue());
			return fenceValue <= LastCompletedFence;
		}

		bool IsInitialized() const { return pFrameData != nullptr; }

	private:
		struct FrameData
		{
			Span<const uint64> ReadbackQueries;					// View to resolved query data
			ID3D12CommandAllocator* pAllocator = nullptr;		// CommandAllocator for this frame
			std::atomic<uint32> QueryIndex = 0;					// Current number of queries
			uint64 FenceValue = 0;								// FenceValue indicating when Resolve is finished
			uint32 QueryStartOffset = 0;						// Offset in readback buffer to where queries start
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
		uint64 LastCompletedFence = 0;
		uint64 FenceValue = 0;
		HANDLE FenceEvent = nullptr;
	};

public:
	// Initialize the GPU profiler using the provided CommandQueues
	void Initialize(ID3D12Device* pDevice, ID3D12CommandQueue** pQueues, uint16 numQueues)
	{
		for (uint16 queueIndex = 0; queueIndex < numQueues; ++queueIndex)
		{
			ID3D12CommandQueue* pQueue = pQueues[queueIndex];
			D3D12_COMMAND_QUEUE_DESC queueDesc = pQueue->GetDesc();
			bool isCopyQueue = queueDesc.Type == D3D12_COMMAND_LIST_TYPE_COPY;

			QueueInfo& queueInfo = m_Queues.emplace_back();
			queueInfo.IsCopyQueue = isCopyQueue;
			uint32 size = ARRAYSIZE(queueInfo.Name);
			pQueue->GetPrivateData(WKPDID_D3DDebugObjectName, &size, queueInfo.Name);
			queueInfo.pQueue = pQueue;
			queueInfo.InitCalibration();

			// If the query heap is not yet initialized and there is an appropriate queue, initialize it
			if (!m_CopyQueryHeap.IsInitialized() && isCopyQueue)
			{
				m_CopyQueryHeap.Initialize(pDevice, pQueue, MAX_NUM_MAIN_REGIONS, NUM_GPU_FRAMES);
				m_ResolveCopyQueueIndex = queueIndex;
			}
			else if (!m_MainQueryHeap.IsInitialized() && !isCopyQueue)
			{
				m_MainQueryHeap.Initialize(pDevice, pQueue, MAX_NUM_MAIN_REGIONS, NUM_GPU_FRAMES);
				m_ResolveMainQueueIndex = queueIndex;
			}
		}
	}

	// Start and push a region on the provided commandlist to be executed on the given queue index
	void PushRegion(const char* pName, ID3D12GraphicsCommandList* pCmd, uint16 queueIndex = 0, const char* pFilePath = nullptr, uint16 lineNr = 0)
	{
		if (m_IsPaused)
			return;

		// #todo: Find a way so that queueIndex doesn't need to be specified. Can only be resolved during ExecuteCommandLists?
		D3D12_COMMAND_LIST_TYPE commandListType = pCmd->GetType();
		bool isCopyCommandlist = commandListType == D3D12_COMMAND_LIST_TYPE_COPY;
		check(isCopyCommandlist == m_Queues[queueIndex].IsCopyQueue);

		SampleHistory& data = m_SampleData[m_FrameIndex % m_SampleData.size()];
		uint32 index = data.CurrentIndex.fetch_add(1);
		check(index < data.Regions.size());
		SampleRegion& region = data.Regions[index];
		region.pName = data.Allocator.String(pName);
		region.QueueIndex = queueIndex;
		region.TimerIndex = GetHeap(isCopyCommandlist).QueryBegin(pCmd);
		region.pFilePath = pFilePath;
		region.LineNumber = lineNr;

		TLS& tls = GetTLS();
		TLS::StackData& stackData = tls.RegionStack.Push();
		stackData.pCommandList = pCmd;
		stackData.RegionIndex = index;

		for (GPUProfilerCallbacks& callback : m_EventCallbacks)
			callback.OnEventBegin(pName, pCmd, queueIndex);
	}

	// End and pop the region on the top of the stack
	void PopRegion()
	{
		if (m_IsPaused)
			return;

		TLS& tls = GetTLS();
		SampleHistory& data = m_SampleData[m_FrameIndex % m_SampleData.size()];
		const TLS::StackData& stackData = tls.RegionStack.Pop();
		const SampleRegion& region = data.Regions[stackData.RegionIndex];
		bool isCopyQueue = m_Queues[region.QueueIndex].IsCopyQueue;
		GetHeap(isCopyQueue).EndQuery(region.TimerIndex, stackData.pCommandList);

		for (GPUProfilerCallbacks& callback : m_EventCallbacks)
			callback.OnEventEnd(region.pName, stackData.pCommandList, region.QueueIndex);
	}

	// Returns the appropriate query heap
	GPUTimeQueryHeap& GetHeap(bool isCopy)
	{
		GPUTimeQueryHeap& heap = isCopy ? m_CopyQueryHeap : m_MainQueryHeap;
		check(heap.IsInitialized());
		return heap;
	}

	void Tick()
	{
		// Apply the pausing.
		// Pausing is deferred because it may happen in the middle of the frame and that would cause remaining regions to not finish.
		m_IsPaused = m_QueuedPause;

		// While the next frame to resolve is not the last one, attempt access the readback data and advance.
		while (m_FrameToResolve < m_FrameIndex)
		{
			Span<const uint64> copyQueries, mainQueries;
			bool copiesValid = m_CopyQueryHeap.GetResolvedQueries(m_FrameToResolve, copyQueries);
			bool mainValid = m_MainQueryHeap.GetResolvedQueries(m_FrameToResolve, mainQueries);
			if (!(copiesValid && mainValid))
				break;

			// Copy the timing data
			SampleHistory& data = m_SampleData[m_FrameToResolve % m_SampleData.size()];
			data.NumRegions = data.CurrentIndex;

			check(copyQueries.GetSize() + mainQueries.GetSize() == data.NumRegions * 2);
			for (uint32 i = 0; i < data.NumRegions; ++i)
			{
				SampleRegion& region = data.Regions[i];
				const QueueInfo& queue = m_Queues[region.QueueIndex];
				const Span<const uint64>& queries = queue.IsCopyQueue ? copyQueries : mainQueries;
				region.BeginTicks = queries[region.TimerIndex * 2 + 0];
				region.EndTicks = queries[region.TimerIndex * 2 + 1];
			}

			// Sort the regions and resolve the stack depth
			std::sort(data.Regions.begin(), data.Regions.begin() + data.NumRegions, [](const SampleRegion& a, const SampleRegion& b) { return a.BeginTicks < b.BeginTicks; });

			using DepthStack = FixedStack<uint32, MAX_DEPTH>;
			std::vector<DepthStack> queueStacks(m_Queues.size());

			for (uint32 i = 0; i < data.NumRegions; ++i)
			{
				SampleRegion& region = data.Regions[i];
				DepthStack& stack = queueStacks[region.QueueIndex];

				// While there is a parent and the current region starts after the parent ends, pop it off the stack
				while (stack.GetSize() > 0)
				{
					const SampleRegion* pParent = &data.Regions[stack.Top()];
					if (region.BeginTicks >= pParent->EndTicks)
					{
						stack.Pop();
					}
					else
					{
						check(region.EndTicks <= pParent->EndTicks);
						break;
					}
				}

				// Set the region's depth
				region.Depth = (uint16)stack.GetSize();
				stack.Push() = i;
			}

			++m_FrameToResolve;
		}

		if (m_IsPaused)
			return;

		// Make sure all last frame's regions have ended
		for (const TLS* pTLS : m_ThreadData)
			check(pTLS->RegionStack.GetSize() == 0);

		// Schedule a resolve for last frame
		m_CopyQueryHeap.Resolve();
		m_MainQueryHeap.Resolve();

		// Advance frame and clear data
		++m_FrameIndex;

		// Check if we're not advancing to a frame that hasn't been resolved yet.
		check(m_FrameIndex % m_SampleData.size() != m_FrameToResolve % m_SampleData.size());

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
		bool IsCopyQueue = false;							// Whether this queue is a copy queue

	private:
		uint64 GPUCalibrationTicks = 0;						// The number of GPU ticks when the calibration was done
		uint64 CPUCalibrationTicks = 0;						// The number of CPU ticks when the calibration was done
		uint64 GPUFrequency = 0;							// The GPU tick frequency
		uint64 CPUFrequency = 0;							// The CPU tick frequency
	};

	// Structure representating a single sample region
	struct SampleRegion
	{
		const char* pName = "";								// Name of the region
		uint64 BeginTicks = 0;								// GPU ticks of start of the region
		uint64 EndTicks = 0;								// GPU ticks of end of the region
		const char* pFilePath = nullptr;					// File path of the sample region
		uint32 TimerIndex = 0xFFFFFFFF;						// The index in the query heap for the timer
		uint16 QueueIndex = INVALID_QUEUE;					// The index of the queue this region is executed on (QueueInfo)
		uint16 Depth = 0;									// Stack depth of the region
		uint16 LineNumber = 0;								// Line number of the sample region
	};

	// Struct containing all sampling data of a single frame
	struct SampleHistory
	{
		SampleHistory()
			: Allocator(1 << 16)
		{}

		std::array<SampleRegion, MAX_NUM_REGIONS> Regions;	// All sample regions of the frame
		uint32 NumRegions = 0;								// The number of resolved regions
		std::atomic<uint32> CurrentIndex = 0;				// The index to the next free sample region
		LinearAllocator Allocator;							// Scratch allocator storing all dynamic allocations of the frame
	};

	Span<const QueueInfo> GetQueueInfo() const { return m_Queues; }

	// Iterate over all sample regions
	template<typename Fn>
	void ForEachFrame(Fn&& fn) const
	{
		// Get the last N history frames before the last resolved frame
		uint32 currentIndex = m_FrameToResolve < m_SampleData.size() ? 0 : m_FrameToResolve - (uint32)m_SampleData.size() + 1;
		for (currentIndex; currentIndex < m_FrameToResolve; ++currentIndex)
		{
			const SampleHistory& data = m_SampleData[currentIndex % m_SampleData.size()];
			fn(currentIndex, data);
		}
	}

	void RegisterEventCallback(GPUProfilerCallbacks inCallbacks)
	{
		m_EventCallbacks.push_back(inCallbacks);
	}

	void SetPaused(bool paused) { m_QueuedPause = paused; }
	bool IsPaused() const { return m_IsPaused; }

private:

	// Return the sample data of the current frame
	SampleHistory& GetData()
	{
		return m_SampleData[m_FrameIndex % m_SampleData.size()];
	}

	// Thread-local storage to keep track of current depth and region stack
	struct TLS
	{
		struct StackData
		{
			uint32 RegionIndex;
			ID3D12GraphicsCommandList* pCommandList;
		};

		FixedStack<StackData, MAX_DEPTH> RegionStack;
		bool IsInitialized = false;
	};

	// Retrieve the thread-local storage
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

	std::vector<GPUProfilerCallbacks> m_EventCallbacks;

	std::mutex m_ThreadDataMutex;
	std::vector<const TLS*> m_ThreadData;

	std::vector<QueueInfo> m_Queues;						// Data of all registered queues
	GPUTimeQueryHeap m_MainQueryHeap;						// The main normal timestamp heap
	uint16 m_ResolveMainQueueIndex = INVALID_QUEUE;			// The index of the queue to resolve normal timestamps with
	GPUTimeQueryHeap m_CopyQueryHeap;						// The copy query timestamp heap
	uint16 m_ResolveCopyQueueIndex = INVALID_QUEUE;			// The index of the queue to resolve copy timestamps with

	std::array<SampleHistory, HISTORY_SIZE> m_SampleData;	// Per-frame sample data
	uint32 m_FrameIndex = 0;								// The current frame index
	uint32 m_FrameToResolve = 0;							// The frame index to resolve next
	bool m_QueuedPause = false;								// The pause state to be queued next Tick
	bool m_IsPaused = false;								// The current pause state
};


// Helper RAII-style structure to push and pop a GPU sample region
struct GPUProfileScope
{
	GPUProfileScope(const char* pFunction, const char* pFilePath, uint16 lineNr, const char* pName, ID3D12GraphicsCommandList* pCmd, uint16 queueIndex = 0)
	{
		gGPUProfiler.PushRegion(pName, pCmd, queueIndex, pFilePath, lineNr);
	}

	GPUProfileScope(const char* pFunction, const char* pFilePath, uint16 lineNr, ID3D12GraphicsCommandList* pCmd, uint16 queueIndex = 0)
	{
		gGPUProfiler.PushRegion(pFunction, pCmd, queueIndex, pFilePath, lineNr);
	}

	~GPUProfileScope()
	{
		gGPUProfiler.PopRegion();
	}

	GPUProfileScope(const GPUProfileScope&) = delete;
	GPUProfileScope& operator=(const GPUProfileScope&) = delete;
};


//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------

// Global CPU Profiler
extern class CPUProfiler gCPUProfiler;

struct CPUProfilerCallbacks
{
	void(*OnEventBegin)(const char* /*pName*/, uint32 /*threadIndex*/);
	void(*OnEventEnd)(const char* /*pName*/, uint32 /*threadIndex*/);
};

// CPU Profiler
// Also responsible for updating GPU profiler
// Also responsible for drawing HUD
class CPUProfiler
{
public:
	static constexpr int REGION_HISTORY = 5;
	static constexpr int MAX_DEPTH = 32;
	static constexpr int MAX_NUM_REGIONS = 1024;

	// Start and push a region on the current thread
	void PushRegion(const char* pName, const char* pFilePath = nullptr, uint16 lineNumber = 0)
	{
		SampleHistory& data = GetData();
		uint32 newIndex = data.CurrentIndex.fetch_add(1);
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

		for (CPUProfilerCallbacks& callback : m_EventCallbacks)
			callback.OnEventBegin(newRegion.pName, newRegion.ThreadIndex);
	}

	// End and pop the last pushed region on the current thread
	void PopRegion()
	{
		SampleHistory& data = GetData();
		TLS& tls = GetTLS();

		SampleRegion& region = data.Regions[tls.RegionStack.Pop()];
		QueryPerformanceCounter((LARGE_INTEGER*)(&region.EndTicks));

		for (CPUProfilerCallbacks& callback : m_EventCallbacks)
			callback.OnEventEnd(region.pName, region.ThreadIndex);
	}

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick()
	{
		m_Paused = m_QueuedPaused;

		if (m_FrameIndex)
			PopRegion();

		for (auto& threadData : m_ThreadData)
			check(threadData.pTLS->RegionStack.GetSize() == 0);

		if (!m_Paused)
			++m_FrameIndex;

		SampleHistory& data = GetData();
		data.CurrentIndex = 0;
		data.Allocator.Reset();

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
		const char* pName = "";								// Name of the region
		uint64 BeginTicks = 0;								// The ticks at the start of this region
		uint64 EndTicks = 0;								// The ticks at the end of this region
		const char* pFilePath = nullptr;					// File path of file in which this region is recorded
		uint32 ThreadIndex = 0xFFFFFFFF;					// Thread Index of the thread that recorderd this region
		uint16 Depth = 0;									// Depth of the region
		uint16 LineNumber = 0;								// Line number of file in which this region is recorded
	};

	// Struct containing all sampling data of a single frame
	struct SampleHistory
	{
		SampleHistory()
			: Allocator(1 << 16)
		{}

		std::array<SampleRegion, MAX_NUM_REGIONS> Regions;	// All sample regions of the frame
		std::atomic<uint32> CurrentIndex = 0;				// The index to the next free sample region
		LinearAllocator	Allocator;							// Scratch allocator storing all dynamic allocations of the frame
	};

	// Thread-local storage to keep track of current depth and region stack
	struct TLS
	{
		uint32 ThreadIndex = 0;
		FixedStack<uint32, MAX_DEPTH> RegionStack;
		bool IsInitialized = false;
	};

	// Structure describing a registered thread
	struct ThreadData
	{
		char Name[128]{};
		uint32 ThreadID = 0;
		const TLS* pTLS = nullptr;
	};

	// Iterate over all frames
	template<typename Fn>
	void ForEachFrame(Fn&& fn) const
	{
		// Start from the oldest history frame
		uint32 currentIndex = m_FrameIndex < m_SampleData.size() ? 0 : m_FrameIndex - (uint32)m_SampleData.size() + 1;
		for (currentIndex; currentIndex < m_FrameIndex; ++currentIndex)
		{
			const SampleHistory& data = m_SampleData[currentIndex % m_SampleData.size()];
			fn(currentIndex, data);
		}
	}

	// Get the ticks range of the history
	void GetHistoryRange(uint64& ticksMin, uint64& ticksMax) const
	{
		uint32 oldestFrameIndex = (m_FrameIndex + 1) % m_SampleData.size();
		ticksMin = m_SampleData[oldestFrameIndex].Regions[0].BeginTicks;
		uint32 youngestFrameIndex = (m_FrameIndex + (uint32)m_SampleData.size() - 1) % m_SampleData.size();
		ticksMax = m_SampleData[youngestFrameIndex].Regions[0].EndTicks;
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
		return m_SampleData[m_FrameIndex % m_SampleData.size()];
	}

	std::vector<CPUProfilerCallbacks> m_EventCallbacks;

	std::mutex m_ThreadDataLock;							// Mutex for accesing thread data
	std::vector<ThreadData> m_ThreadData;					// Data describing each registered thread

	std::array<SampleHistory, REGION_HISTORY> m_SampleData;	// Per-frame data
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
