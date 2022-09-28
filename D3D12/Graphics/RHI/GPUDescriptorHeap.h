#pragma once
#include "DescriptorHandle.h"
#include "GraphicsResource.h"
#include "Core/BitField.h"
#include "RootSignature.h"
#include "CommandQueue.h"

class CommandContext;
enum class CommandListContext;

struct DescriptorHeapBlock
{
	DescriptorHeapBlock(DescriptorHandle startHandle, uint32 size)
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
	GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 dynamicBlockSize, uint32 numDescriptors);
	~GPUDescriptorHeap();

	DescriptorHandle AllocatePersistent();
	void FreePersistent(uint32& heapIndex);

	DescriptorHeapBlock* AllocateDynamicBlock();
	void FreeDynamicBlock(const SyncPoint& syncPoint, DescriptorHeapBlock* pBlock);
	uint32 GetDynamicBlockSize() const { return m_DynamicBlockSize; }

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

	std::mutex m_DynamicBlockAllocateMutex;
	uint32 m_DynamicBlockSize;
	uint32 m_NumDynamicDescriptors;
	std::vector<std::unique_ptr<DescriptorHeapBlock>> m_DynamicBlocks;
	std::queue<DescriptorHeapBlock*> m_ReleasedDynamicBlocks;
	std::vector<DescriptorHeapBlock*> m_FreeDynamicBlocks;

	FreeList<false> m_PersistentHandles;
	uint32 m_NumPersistentDescriptors;
	std::mutex m_AllocationLock;
	std::queue<std::pair<uint32, uint64>> m_PersistentDeletionQueue;
};

class DynamicGPUDescriptorAllocator : public GraphicsObject
{
public:
	DynamicGPUDescriptorAllocator(GPUDescriptorHeap* pGlobalHeap);

	DescriptorHandle Allocate(uint32 count);

	void SetDescriptors(uint32 rootIndex, uint32 offset, const Span< D3D12_CPU_DESCRIPTOR_HANDLE>& handles);
	void BindStagedDescriptors(CommandContext& context, CommandListContext descriptorTableType);

	void ParseRootSignature(const RootSignature* pRootSignature);
	void ReleaseUsedHeaps(const SyncPoint& syncPoint);

private:
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	struct RootDescriptorEntry
	{
		uint32 TableSize = 0;
		DescriptorHandle Descriptor;
	};
	std::array<RootDescriptorEntry, MAX_NUM_ROOT_PARAMETERS> m_RootDescriptorTable = {};

	RootSignatureMask m_RootDescriptorMask {};
	RootSignatureMask m_StaleRootParameters {};

	GPUDescriptorHeap* m_pHeapAllocator;
	DescriptorHeapBlock* m_pCurrentHeapBlock = nullptr;
	std::vector<DescriptorHeapBlock*> m_ReleasedBlocks;
};
