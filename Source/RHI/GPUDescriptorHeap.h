#pragma once
#include "DescriptorHandle.h"
#include "DeviceResource.h"
#include "Core/BitField.h"
#include "RootSignature.h"
#include "CommandQueue.h"

enum class CommandListContext : uint8;


struct DescriptorHeapPage
{
	DescriptorHeapPage(const DescriptorPtr& startHandle, uint32 size)
		: StartPtr(startHandle), Size(size), CurrentOffset(0)
	{}
	DescriptorPtr	StartPtr;
	uint32			Size;
	uint32			CurrentOffset;
	SyncPoint		SyncPoint;
};


class GPUDescriptorHeap : public DeviceObject
{
public:
	struct Stats
	{
		uint32 NumPersistentAllocations;
		uint32 PersistentCapacity;
		uint32 NumScratchAllocations;
		uint32 ScratchCapacity;
	};

	GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 dynamicPageSize, uint32 numDescriptors);
	~GPUDescriptorHeap();

	DescriptorPtr AllocatePersistent();
	void FreePersistent(DescriptorHandle& handle);

	DescriptorHeapPage* AllocateScratchPage();
	void FreeScratchPage(const SyncPoint& syncPoint, DescriptorHeapPage* pPage);
	uint32 GetDynamicPageSize() const { return m_ScratchPageSize; }

	Stats GetStats() const;

	uint32 GetDescriptorSize() const { return m_DescriptorSize; }
	ID3D12DescriptorHeap* GetHeap() const { return m_pHeap.Get(); }
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }
	DescriptorPtr GetStartPtr() const { return m_StartPtr; }

private:
	void CleanupPersistent();
	void CleanupScratch();

	Ref<ID3D12DescriptorHeap> m_pHeap;
	Ref<ID3D12DescriptorHeap> m_pCPUHeap;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_DescriptorSize = 0;
	DescriptorPtr m_StartPtr;

	std::mutex m_ScratchPageAllocationMutex;
	uint32 m_ScratchPageSize;
	uint32 m_NumScratchDescriptors;
	Array<std::unique_ptr<DescriptorHeapPage>> m_ScratchPages;
	std::queue<DescriptorHeapPage*> m_ReleasedScratchPages;
	Array<DescriptorHeapPage*> m_FreeScratchPages;

	FreeList m_PersistentHandles;
	uint32 m_NumPersistentDescriptors;
	std::mutex m_AllocationLock;
	std::queue<std::pair<DescriptorHandle, uint64>> m_PersistentDeletionQueue;
};



class GPUScratchDescriptorAllocator : public DeviceObject
{
public:
	GPUScratchDescriptorAllocator(GPUDescriptorHeap* pGlobalHeap);
	~GPUScratchDescriptorAllocator();

	DescriptorPtr Allocate(uint32 count);

	void SetDescriptors(uint32 rootIndex, uint32 offset, Span<DescriptorHandle> handles);
	void BindStagedDescriptors(CommandContext& context, CommandListContext descriptorTableType);

	void ParseRootSignature(const RootSignature* pRootSignature);
	void ReleaseUsedHeaps(const SyncPoint& syncPoint);

private:
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;

	// Structure holding staged descriptors for a table.
	struct StagedDescriptorTable
	{
		Array<D3D12_CPU_DESCRIPTOR_HANDLE> Descriptors;
		uint32 StartIndex = 0xFFFFFFFF;
		uint32 Capacity = 0;
	};
	StaticArray<StagedDescriptorTable, RootSignature::sMaxNumParameters> m_StagedDescriptors = {};
	BitField<RootSignature::sMaxNumParameters, uint8> m_StaleRootParameters{};

	GPUDescriptorHeap* m_pHeapAllocator;
	DescriptorHeapPage* m_pCurrentHeapPage = nullptr;
	Array<DescriptorHeapPage*> m_ReleasedPages;
};
