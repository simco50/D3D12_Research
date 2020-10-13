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

class OnlineDescriptorAllocator : public GraphicsObject
{
public:
	OnlineDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type);
	~OnlineDescriptorAllocator();

	DescriptorHandle AllocateTransientDescriptor(int count);

	void SetDescriptors(uint32 rootIndex, uint32 offset, uint32 numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles);
	void UploadAndBindStagedDescriptors(DescriptorTableType descriptorTableType);

	void ParseRootSignature(RootSignature* pRootSignature);

	void ReleaseUsedHeaps(uint64 fenceValue);

private:
	static const int DESCRIPTORS_PER_HEAP = 1024;
	static const int MAX_NUM_ROOT_PARAMETERS = 10;
	static const int MAX_DESCRIPTORS_PER_TABLE = 128;

	static std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeaps;
	static std::array<std::queue<std::pair<uint64, ID3D12DescriptorHeap*>>, 2> m_FreeDescriptors;
	static std::mutex m_HeapAllocationMutex;

	uint32 GetRequiredSpace();
	ID3D12DescriptorHeap* RequestNewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type);
	void ReleaseHeap();
	void UnbindAll();
	DescriptorHandle Allocate(int descriptors);
	bool HasSpace(int count);
	ID3D12DescriptorHeap* GetHeap();

	std::vector<ID3D12DescriptorHeap*> m_UsedDescriptorHeaps;

	struct RootDescriptorEntry
	{
		BitField<MAX_DESCRIPTORS_PER_TABLE> AssignedHandlesBitMap {};
		D3D12_CPU_DESCRIPTOR_HANDLE* TableStart = nullptr;
		uint32 TableSize = 0;
	};
	std::array<RootDescriptorEntry, MAX_NUM_ROOT_PARAMETERS> m_RootDescriptorTable = {};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, DESCRIPTORS_PER_HEAP> m_HandleCache = {};

	BitField32 m_RootDescriptorMask {};
	BitField32 m_StaleRootParameters {};

	DescriptorHandle m_StartHandle{};
	CommandContext* m_pOwner;
	ID3D12DescriptorHeap* m_pCurrentHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_CurrentOffset = 0;
	uint32 m_DescriptorSize = 0;
};