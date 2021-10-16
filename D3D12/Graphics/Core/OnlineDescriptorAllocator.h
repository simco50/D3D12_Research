#pragma once
#include "DescriptorHandle.h"
#include "GraphicsResource.h"
#include "Core/BitField.h"
#include "RootSignature.h"

class CommandContext;
enum class CommandListContext;

struct DescriptorHeapBlock
{
	DescriptorHeapBlock(DescriptorHandle startHandle, uint32 size, uint32 currentOffset)
		: StartHandle(startHandle), Size(size), CurrentOffset(currentOffset), FenceValue(0)
	{}
	DescriptorHandle StartHandle;
	uint32 Size;
	uint32 CurrentOffset;
	uint64 FenceValue;
};

class GlobalOnlineDescriptorHeap : public GraphicsObject
{
public:
	GlobalOnlineDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 blockSize, uint32 numDescriptors);

	DescriptorHeapBlock* AllocateBlock();
	void FreeBlock(uint64 fenceValue, DescriptorHeapBlock* pBlock);
	uint32 GetDescriptorSize() const { return m_DescriptorSize; }
	ID3D12DescriptorHeap* GetHeap() const { return m_pHeap.Get(); }
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }
	DescriptorHandle GetStartHandle() const { return m_StartHandle; }
	uint32 GetBlockSize() const { return  m_BlockSize; }

private:
	std::mutex m_BlockAllocateMutex;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_NumDescriptors;

	uint32 m_DescriptorSize = 0;
	uint32 m_BlockSize = 0;
	DescriptorHandle m_StartHandle;

	ComPtr<ID3D12DescriptorHeap> m_pHeap;
	std::vector<std::unique_ptr<DescriptorHeapBlock>> m_HeapBlocks;
	std::vector<DescriptorHeapBlock*> m_ReleasedBlocks;
	std::queue<DescriptorHeapBlock*> m_FreeBlocks;
};

class PersistentDescriptorAllocator : public GraphicsObject
{
public:
	PersistentDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap);
	DescriptorHandle Allocate();
	void Free(DescriptorHandle& handle);
	void Free(uint32& heapIndex);

private:
	GlobalOnlineDescriptorHeap* m_pHeapAllocator;
	std::vector<DescriptorHeapBlock*> m_HeapBlocks;
	std::vector<uint32> m_FreeHandles;
	uint32 m_NumAllocated = 0;
	std::mutex m_AllocationLock;
};

class OnlineDescriptorAllocator : public GraphicsObject
{
public:
	OnlineDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap);
	~OnlineDescriptorAllocator();

	DescriptorHandle Allocate(uint32 count);

	void SetDescriptors(uint32 rootIndex, uint32 offset, uint32 numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles);
	void BindStagedDescriptors(ID3D12GraphicsCommandList* pCmdList, CommandListContext descriptorTableType);

	void ParseRootSignature(RootSignature* pRootSignature);
	void ReleaseUsedHeaps(uint64 fenceValue);

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

	GlobalOnlineDescriptorHeap* m_pHeapAllocator;
	DescriptorHeapBlock* m_pCurrentHeapBlock = nullptr;
	std::vector<DescriptorHeapBlock*> m_ReleasedBlocks;
};
