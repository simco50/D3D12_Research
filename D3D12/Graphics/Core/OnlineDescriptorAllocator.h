#pragma once
#include "DescriptorHandle.h"
#include "GraphicsResource.h"
#include "Core/BitField.h"

class CommandContext;
class RootSignature;
class Graphics;

enum class DescriptorTableType
{
	Graphics,
	Compute,
};

class GlobalOnlineDescriptorHeap : public GraphicsObject
{
public:
	struct HeapBlock
	{
		HeapBlock(DescriptorHandle startHandle, uint32 size, uint32 currentOffset)
			: StartHandle(startHandle), Size(size), CurrentOffset(currentOffset), FenceValue(0)
		{}
		DescriptorHandle StartHandle;
		uint32 Size;
		uint32 CurrentOffset;
		uint64 FenceValue;
	};

	static const int BLOCK_SIZE = 2000;
	GlobalOnlineDescriptorHeap(Graphics* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 numDescriptors);

	HeapBlock* AllocateBlock();
	void FreeBlock(uint64 fenceValue, HeapBlock* pBlock);

	ID3D12DescriptorHeap* GetHeap() const { return m_pHeap.Get(); }

private:
	std::mutex m_BlockAllocateMutex;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_NumDescriptors;

	uint32 m_DescriptorSize = 0;
	DescriptorHandle m_StartHandle;

	ComPtr<ID3D12DescriptorHeap> m_pHeap;
	std::vector<std::unique_ptr<HeapBlock>> m_HeapBlocks;
	std::vector<HeapBlock*> m_ReleasedBlocks;
	std::queue<HeapBlock*> m_FreeBlocks;
};

class OnlineDescriptorAllocator : public GraphicsObject
{
public:
	OnlineDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type);
	~OnlineDescriptorAllocator();

	DescriptorHandle Allocate(int count);

	void SetDescriptors(uint32 rootIndex, uint32 offset, uint32 numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles);
	void UploadAndBindStagedDescriptors(DescriptorTableType descriptorTableType);

	void ParseRootSignature(RootSignature* pRootSignature);
	void ReleaseUsedHeaps(uint64 fenceValue);

private:
	static const int MAX_NUM_ROOT_PARAMETERS = 10;
	static const int MAX_DESCRIPTORS_PER_TABLE = 128;

	uint32 GetRequiredSpace();
	void UnbindAll();
	bool EnsureSpace(uint32 count);

	struct RootDescriptorEntry
	{
		BitField<MAX_DESCRIPTORS_PER_TABLE> AssignedHandlesBitMap {};
		D3D12_CPU_DESCRIPTOR_HANDLE* TableStart = nullptr;
		uint32 TableSize = 0;
	};
	std::array<RootDescriptorEntry, MAX_NUM_ROOT_PARAMETERS> m_RootDescriptorTable = {};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_NUM_ROOT_PARAMETERS * MAX_DESCRIPTORS_PER_TABLE> m_HandleCache = {};

	BitField32 m_RootDescriptorMask {};
	BitField32 m_StaleRootParameters {};

	GlobalOnlineDescriptorHeap* m_pHeapAllocator;
	GlobalOnlineDescriptorHeap::HeapBlock* m_pCurrentHeapBlock = nullptr;
	std::vector<GlobalOnlineDescriptorHeap::HeapBlock*> m_ReleasedBlocks;

	CommandContext* m_pOwner;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_DescriptorSize = 0;
};
