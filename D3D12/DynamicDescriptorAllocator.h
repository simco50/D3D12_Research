#pragma once
#include "DescriptorHandle.h"

class CommandContext;
class RootSignature;
class Graphics;

class DynamicDescriptorAllocator
{
public:
	DynamicDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type);
	~DynamicDescriptorAllocator();

	void SetDescriptors(uint32 rootIndex, uint32 offset, uint32 numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE handles[]);
	void UploadAndBindStagedDescriptors();

	bool HasSpace(int count);
	ID3D12DescriptorHeap* GetHeap();

	void ParseRootSignature(RootSignature* pRootSignature);

	void ReleaseUsedHeaps(uint64 fenceValue);

private:
	uint32 GetRequiredSpace();
	ID3D12DescriptorHeap* RequestNewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type);
	void ReleaseHeap();
	void UnbindAll();

	DescriptorHandle Allocate(int descriptors);

	static const int DESCRIPTORS_PER_HEAP = 1024;
	static const int MAX_DESCRIPTORS_PER_TABLE = 6;

	static std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeaps;
	static std::queue<std::pair<uint64, ID3D12DescriptorHeap*>> m_FreeDescriptors;

	std::vector<ID3D12DescriptorHeap*> m_UsedDescriptorHeaps;

	struct RootDescriptorEntry
	{
		BitField32 AssignedHandlesBitMap {};
		D3D12_CPU_DESCRIPTOR_HANDLE* TableStart = nullptr;
		uint32_t TableSize = 0;
	};
	std::array<RootDescriptorEntry, MAX_DESCRIPTORS_PER_TABLE> m_RootDescriptorTable = {};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, DESCRIPTORS_PER_HEAP> m_HandleCache = {};

	BitField32 m_RootDescriptorMask {};
	BitField32 m_StaleRootParameters {};

	Graphics* m_pGraphics;
	CommandContext* m_pOwner;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	DescriptorHandle m_StartHandle {};
	int m_CurrentOffset = 0;
	ID3D12DescriptorHeap* m_pCurrentHeap = nullptr;
	uint32 m_DescriptorSize = 0;
};