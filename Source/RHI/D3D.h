#pragma once
#include "RHI/RHI.h"

#define ENABLE_PIX 1

#if ENABLE_PIX
#define USE_PIX
#include "pix3.h"
#endif

#define VERIFY_HR(hr) D3D::LogHRESULT(hr, nullptr, #hr, __FILE__, __LINE__)
#define VERIFY_HR_EX(hr, device) D3D::LogHRESULT(hr, device, #hr, __FILE__, __LINE__)

template<uint32 Depth>
class Callstack;

namespace D3D
{
String ResourceStateToString(D3D12_RESOURCE_STATES state);

constexpr const char* CommandlistTypeToString(D3D12_COMMAND_LIST_TYPE type)
{
#define STATE_CASE(name)                 \
	case D3D12_COMMAND_LIST_TYPE_##name: \
		return #name
	switch (type)
	{
		STATE_CASE(DIRECT);
		STATE_CASE(COMPUTE);
		STATE_CASE(COPY);
		STATE_CASE(BUNDLE);
		STATE_CASE(VIDEO_DECODE);
		STATE_CASE(VIDEO_ENCODE);
		STATE_CASE(VIDEO_PROCESS);
	default:
		return "";
	}
#undef STATE_CASE
}

void EnqueuePIXCapture(uint32 numFrames = 1);

String GetErrorString(HRESULT errorCode, ID3D12Device* pDevice);

__forceinline bool LogHRESULT(HRESULT hr, ID3D12Device* pDevice, const char* pCode, const char* pFileName, uint32 lineNumber)
{
	if (!SUCCEEDED(hr))
	{
		E_LOG(Error, "%s:%d: %s - %s", pFileName, lineNumber, GetErrorString(hr, pDevice).c_str(), pCode);
		__debugbreak();
		return false;
	}
	return true;
}

static constexpr bool HasWriteResourceState(D3D12_RESOURCE_STATES state)
{
	return EnumHasAnyFlags(state,
						   D3D12_RESOURCE_STATE_STREAM_OUT |
							   D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
							   D3D12_RESOURCE_STATE_RENDER_TARGET |
							   D3D12_RESOURCE_STATE_DEPTH_WRITE |
							   D3D12_RESOURCE_STATE_COPY_DEST |
							   D3D12_RESOURCE_STATE_RESOLVE_DEST |
							   D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
							   D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
							   D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);
};

static constexpr bool CanCombineResourceState(D3D12_RESOURCE_STATES stateA, D3D12_RESOURCE_STATES stateB)
{
	return !HasWriteResourceState(stateA) && !HasWriteResourceState(stateB);
}

static constexpr bool IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandlistType, D3D12_RESOURCE_STATES state)
{
	constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES =
		D3D12_RESOURCE_STATE_COMMON | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

	constexpr int VALID_COPY_QUEUE_RESOURCE_STATES =
		D3D12_RESOURCE_STATE_COMMON | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE;

	if (commandlistType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		return (state & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == state;
	}
	else if (commandlistType == D3D12_COMMAND_LIST_TYPE_COPY)
	{
		return (state & VALID_COPY_QUEUE_RESOURCE_STATES) == state;
	}
	return true;
}

static constexpr bool NeedsTransition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES& after, bool allowCombine)
{
	if (before == after)
		return false;

	// When resolving pending resource barriers, combining resource states is not working
	// This is because the last known resource state of the resource is used to update the resource
	// And so combining after_state during the result will result in the last known resource state not matching up.
	if (!allowCombine)
		return true;

	// Can read from 'write' DSV
	if (before == D3D12_RESOURCE_STATE_DEPTH_WRITE && after == D3D12_RESOURCE_STATE_DEPTH_READ)
		return false;

	if (after == D3D12_RESOURCE_STATE_COMMON)
		return before != D3D12_RESOURCE_STATE_COMMON;

	// Combine already transitioned bits
	if (D3D::CanCombineResourceState(before, after) && !EnumHasAllFlags(before, after))
		after |= before;

	return true;
}

void SetObjectName(ID3D12Object* pObject, const char* pName);

String GetObjectName(ID3D12Object* pObject);

String BarrierToString(const D3D12_RESOURCE_BARRIER& barrier);

constexpr static const DXGI_FORMAT gDXGIFormatMap[] = {
	DXGI_FORMAT_UNKNOWN,
	DXGI_FORMAT_R8_UINT,
	DXGI_FORMAT_R8_SINT,
	DXGI_FORMAT_R8_UNORM,
	DXGI_FORMAT_R8_SNORM,
	DXGI_FORMAT_R8G8_UINT,
	DXGI_FORMAT_R8G8_SINT,
	DXGI_FORMAT_R8G8_UNORM,
	DXGI_FORMAT_R8G8_SNORM,
	DXGI_FORMAT_R16_UINT,
	DXGI_FORMAT_R16_SINT,
	DXGI_FORMAT_R16_UNORM,
	DXGI_FORMAT_R16_SNORM,
	DXGI_FORMAT_R16_FLOAT,
	DXGI_FORMAT_B4G4R4A4_UNORM,
	DXGI_FORMAT_B5G6R5_UNORM,
	DXGI_FORMAT_B5G5R5A1_UNORM,
	DXGI_FORMAT_R8G8B8A8_UINT,
	DXGI_FORMAT_R8G8B8A8_SINT,
	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_R8G8B8A8_SNORM,
	DXGI_FORMAT_B8G8R8A8_UNORM,
	DXGI_FORMAT_R10G10B10A2_UNORM,
	DXGI_FORMAT_R11G11B10_FLOAT,
	DXGI_FORMAT_R16G16_UINT,
	DXGI_FORMAT_R16G16_SINT,
	DXGI_FORMAT_R16G16_UNORM,
	DXGI_FORMAT_R16G16_SNORM,
	DXGI_FORMAT_R16G16_FLOAT,
	DXGI_FORMAT_R32_UINT,
	DXGI_FORMAT_R32_SINT,
	DXGI_FORMAT_R32_FLOAT,
	DXGI_FORMAT_R16G16B16A16_UINT,
	DXGI_FORMAT_R16G16B16A16_SINT,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R16G16B16A16_UNORM,
	DXGI_FORMAT_R16G16B16A16_SNORM,
	DXGI_FORMAT_R32G32_UINT,
	DXGI_FORMAT_R32G32_SINT,
	DXGI_FORMAT_R32G32_FLOAT,
	DXGI_FORMAT_R32G32B32_UINT,
	DXGI_FORMAT_R32G32B32_SINT,
	DXGI_FORMAT_R32G32B32_FLOAT,
	DXGI_FORMAT_R32G32B32A32_UINT,
	DXGI_FORMAT_R32G32B32A32_SINT,
	DXGI_FORMAT_R32G32B32A32_FLOAT,

	DXGI_FORMAT_R9G9B9E5_SHAREDEXP,

	DXGI_FORMAT_BC1_UNORM,
	DXGI_FORMAT_BC2_UNORM,
	DXGI_FORMAT_BC3_UNORM,
	DXGI_FORMAT_BC4_UNORM,
	DXGI_FORMAT_BC4_SNORM,
	DXGI_FORMAT_BC5_UNORM,
	DXGI_FORMAT_BC5_SNORM,
	DXGI_FORMAT_BC6H_UF16,
	DXGI_FORMAT_BC6H_SF16,
	DXGI_FORMAT_BC7_UNORM,

	DXGI_FORMAT_D16_UNORM,
	DXGI_FORMAT_D32_FLOAT,
	DXGI_FORMAT_D24_UNORM_S8_UINT,
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
};

static_assert(ARRAYSIZE(gDXGIFormatMap) == (uint32)ResourceFormat::Num);

constexpr DXGI_FORMAT ConvertFormat(ResourceFormat format)
{
	return gDXGIFormatMap[(uint32)format];
}

constexpr DXGI_FORMAT GetFormatSRGB(DXGI_FORMAT format, bool srgb)
{
	if (!srgb)
		return format;
	switch (format)
	{
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case DXGI_FORMAT_BC1_UNORM:
		return DXGI_FORMAT_BC1_UNORM_SRGB;
	case DXGI_FORMAT_BC2_UNORM:
		return DXGI_FORMAT_BC2_UNORM_SRGB;
	case DXGI_FORMAT_BC3_UNORM:
		return DXGI_FORMAT_BC3_UNORM_SRGB;
	case DXGI_FORMAT_BC7_UNORM:
		return DXGI_FORMAT_BC7_UNORM_SRGB;
	default:
		return format;
	};
}

struct ResourceDescHash
{
	size_t operator()(const D3D12_RESOURCE_DESC& resourceDesc) const
	{
		uint64 hash = gHash(resourceDesc.Dimension);
		hash		= gHashCombine(hash, resourceDesc.Alignment);
		hash		= gHashCombine(hash, resourceDesc.Width);
		hash		= gHashCombine(hash, resourceDesc.Height);
		hash		= gHashCombine(hash, resourceDesc.DepthOrArraySize);
		hash		= gHashCombine(hash, resourceDesc.MipLevels);
		hash		= gHashCombine(hash, resourceDesc.Format);
		hash		= gHashCombine(hash, resourceDesc.SampleDesc.Count);
		hash		= gHashCombine(hash, resourceDesc.SampleDesc.Quality);
		hash		= gHashCombine(hash, resourceDesc.Layout);
		hash		= gHashCombine(hash, resourceDesc.Flags);
		return hash;
	}
};

struct ResourceDescEqual
{
	bool operator()(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) const
	{
		return a.Dimension == b.Dimension &&
			   a.Alignment == b.Alignment &&
			   a.Width == b.Width &&
			   a.Height == b.Height &&
			   a.DepthOrArraySize == b.DepthOrArraySize &&
			   a.MipLevels == b.MipLevels &&
			   a.Format == b.Format &&
			   a.SampleDesc.Count == b.SampleDesc.Count &&
			   a.SampleDesc.Quality == b.SampleDesc.Quality &&
			   a.Layout == b.Layout &&
			   a.Flags == b.Flags;
	}
};

void GetResourceAllocationInfo(ID3D12Device* pDevice, const D3D12_RESOURCE_DESC& resourceDesc, uint64& outSize, uint64& outAlignment);

String GetResourceDescription(ID3D12Resource* pResource);

void SetResourceCallstack(ID3D12Object* pObject);
bool GetResourceCallstack(ID3D12Object* pObject, Callstack<6>& outCallstack);

D3D12_RESOURCE_DESC GetResourceDesc(const BufferDesc& bufferDesc);
D3D12_RESOURCE_DESC GetResourceDesc(const TextureDesc& textureDesc);

} // namespace D3D
