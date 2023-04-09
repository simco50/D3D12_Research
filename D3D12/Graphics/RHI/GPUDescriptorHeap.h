#pragma once
#include "DescriptorHandle.h"
#include "GraphicsResource.h"
#include "Core/BitField.h"
#include "RootSignature.h"
#include "CommandQueue.h"

class CommandContext;
enum class CommandListContext;

struct DescriptorHeapPage
{
	DescriptorHeapPage(DescriptorHandle startHandle, uint32 size)
		: StartHandle(startHandle), Size(size), CurrentOffset(0)
	{}
	DescriptorHandle StartHandle;
	uint32 Size;
	uint32 CurrentOffset;
	SyncPoint SyncPoint;
};

class GPUDescriptorHeap : public GraphicsObject
{
public:
	GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 dynamicPageSize, uint32 numDescriptors);
	~GPUDescriptorHeap();

	DescriptorHandle AllocatePersistent();
	void FreePersistent(uint32& heapIndex);

	DescriptorHeapPage* AllocateDynamicPage();
	void FreeDynamicPage(const SyncPoint& syncPoint, DescriptorHeapPage* pPage);
	uint32 GetDynamicPageSize() const { return m_DynamicPageSize; }

	uint32 GetDescriptorSize() const { return m_DescriptorSize; }
	ID3D12DescriptorHeap* GetHeap() const { return m_pHeap.Get(); }
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }
	DescriptorHandle GetStartHandle() const { return m_StartHandle; }

private:
	void CleanupPersistent();
	void CleanupDynamic();

	RefCountPtr<ID3D12DescriptorHeap> m_pHeap;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_DescriptorSize = 0;
	DescriptorHandle m_StartHandle;

	std::mutex m_DynamicPageAllocateMutex;
	uint32 m_DynamicPageSize;
	uint32 m_NumDynamicDescriptors;
	std::vector<std::unique_ptr<DescriptorHeapPage>> m_DynamicPages;
	std::queue<DescriptorHeapPage*> m_ReleasedDynamicPages;
	std::vector<DescriptorHeapPage*> m_FreeDynamicPages;

	FreeList<false> m_PersistentHandles;
	uint32 m_NumPersistentDescriptors;
	std::mutex m_AllocationLock;
	std::queue<std::pair<uint32, uint64>> m_PersistentDeletionQueue;
};

class DynamicGPUDescriptorAllocator : public GraphicsObject
{
public:
	DynamicGPUDescriptorAllocator(GPUDescriptorHeap* pGlobalHeap);
	~DynamicGPUDescriptorAllocator();

	DescriptorHandle Allocate(uint32 count);

	void SetDescriptors(uint32 rootIndex, uint32 offset, const Span< D3D12_CPU_DESCRIPTOR_HANDLE>& handles);
	void BindStagedDescriptors(CommandContext& context, CommandListContext descriptorTableType);

	void ParseRootSignature(const RootSignature* pRootSignature);
	void ReleaseUsedHeaps(const SyncPoint& syncPoint);

private:
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;

	// Structure holding staged descriptors for a table.
	struct StagedDescriptorTable
	{
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> Descriptors;
		uint32 StartIndex = 0xFFFFFFFF;
		uint32 Capacity = 0;
	};
	std::array<StagedDescriptorTable, RootSignature::sMaxNumParameters> m_StagedDescriptors = {};
	BitField<RootSignature::sMaxNumParameters, uint8> m_StaleRootParameters{};

	GPUDescriptorHeap* m_pHeapAllocator;
	DescriptorHeapPage* m_pCurrentHeapPage = nullptr;
	std::vector<DescriptorHeapPage*> m_ReleasedPages;
};
