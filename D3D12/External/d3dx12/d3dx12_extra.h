#ifndef __D3DX12_EXTRA_H__
#define __D3DX12_EXTRA_H__

#include "d3d12.h"

#if defined( __cplusplus )


struct CD3DX12_INPUT_ELEMENT_DESC : public D3D12_INPUT_ELEMENT_DESC
{
	CD3DX12_INPUT_ELEMENT_DESC() = default;
	explicit CD3DX12_INPUT_ELEMENT_DESC(const D3D12_INPUT_ELEMENT_DESC& o) noexcept :
		D3D12_INPUT_ELEMENT_DESC(o)
	{}
	CD3DX12_INPUT_ELEMENT_DESC(
		const char* semanticName, 
		DXGI_FORMAT format, 
		uint32 semanticIndex = 0, 
		uint32 byteOffset = D3D12_APPEND_ALIGNED_ELEMENT, 
		uint32 inputSlot = 0, 
		D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 
		uint32 instanceDataStepRate = 0)
	{
		SemanticName = semanticName;
		SemanticIndex = semanticIndex;
		Format = format;
		InputSlot = inputSlot;
		AlignedByteOffset = byteOffset;
		InputSlotClass = inputSlotClass;
		InstanceDataStepRate = instanceDataStepRate;
	}
};

struct CD3DX12_QUERY_HEAP_DESC : public D3D12_QUERY_HEAP_DESC
{
	CD3DX12_QUERY_HEAP_DESC() = default;
	CD3DX12_QUERY_HEAP_DESC(uint32 count,
		D3D12_QUERY_HEAP_TYPE type,
		uint32 nodeMask = 0)
	{
		Type = type;
		Count = count;
		NodeMask = nodeMask;
	}
};

#endif // defined( __cplusplus )

#endif //__D3DX12_EXTRA_H__
