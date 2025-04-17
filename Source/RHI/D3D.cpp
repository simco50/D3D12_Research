#include "stdafx.h"
#include "D3D.h"
#include "RHI/RHI.h"
#include "Core/Paths.h"
#include "Core/Utils.h"
#include "Core/Callstack.h"
#include "Core/Mutex.h"
#include "Core/Callstack.h"
#include "RHI/Texture.h"
#include "RHI/Buffer.h"

namespace D3D
{
String ResourceStateToString(D3D12_RESOURCE_STATES state)
{
	if (state == 0)
		return "COMMON";
	if (state == -1)
		return "UNKNOWN";

	char outString[1024];
	outString[0]   = '\0';
	char* pCurrent = outString;
	int	  i		   = 0;
	auto  AddText  = [&](const char* pText) {
		  if (i++ > 0)
			  *pCurrent++ = '/';
		  strcpy_s(pCurrent, 1024 - (pCurrent - outString), pText);
		  size_t len = strlen(pText);
		  pCurrent += len;
	};

#define STATE_CASE(name)                                                      \
	if ((state & D3D12_RESOURCE_STATE_##name) == D3D12_RESOURCE_STATE_##name) \
	{                                                                         \
		AddText(#name);                                                       \
		state &= ~D3D12_RESOURCE_STATE_##name;                                \
	}

	STATE_CASE(GENERIC_READ);
	STATE_CASE(VERTEX_AND_CONSTANT_BUFFER);
	STATE_CASE(INDEX_BUFFER);
	STATE_CASE(RENDER_TARGET);
	STATE_CASE(UNORDERED_ACCESS);
	STATE_CASE(DEPTH_WRITE);
	STATE_CASE(DEPTH_READ);
	STATE_CASE(ALL_SHADER_RESOURCE);
	STATE_CASE(NON_PIXEL_SHADER_RESOURCE);
	STATE_CASE(PIXEL_SHADER_RESOURCE);
	STATE_CASE(STREAM_OUT);
	STATE_CASE(INDIRECT_ARGUMENT);
	STATE_CASE(COPY_DEST);
	STATE_CASE(COPY_SOURCE);
	STATE_CASE(RESOLVE_DEST);
	STATE_CASE(RESOLVE_SOURCE);
	STATE_CASE(RAYTRACING_ACCELERATION_STRUCTURE);
	STATE_CASE(SHADING_RATE_SOURCE);
	STATE_CASE(VIDEO_DECODE_READ);
	STATE_CASE(VIDEO_DECODE_WRITE);
	STATE_CASE(VIDEO_PROCESS_READ);
	STATE_CASE(VIDEO_PROCESS_WRITE);
	STATE_CASE(VIDEO_ENCODE_READ);
	STATE_CASE(VIDEO_ENCODE_WRITE);
#undef STATE_CASE
	return outString;
}

void EnqueuePIXCapture(uint32 numFrames)
{
#ifdef USE_PIX
	HWND window = GetActiveWindow();
	if (SUCCEEDED(PIXSetTargetWindow(window)))
	{
		SYSTEMTIME time;
		GetSystemTime(&time);
		Paths::CreateDirectoryTree(Paths::SavedDir());
		String filePath = Sprintf("%ssGPU_Capture_%s.wpix", Paths::SavedDir().c_str(), Utils::GetTimeString().c_str());
		if (SUCCEEDED(PIXGpuCaptureNextFrames(MULTIBYTE_TO_UNICODE(filePath.c_str()), numFrames)))
		{
			E_LOG(Info, "Captured %d frames to '%s'", numFrames, filePath.c_str());
		}
	}
#endif
}

String GetErrorString(HRESULT errorCode, ID3D12Device* pDevice)
{
	String str;
	char*  errorMsg;
	if (FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errorMsg, 0, nullptr) != 0)
	{
		str += errorMsg;
		LocalFree(errorMsg);
	}
	if (errorCode == DXGI_ERROR_DEVICE_REMOVED && pDevice)
	{
		Ref<ID3D12InfoQueue> pInfo;
		pDevice->QueryInterface(pInfo.GetAddressOf());
		if (pInfo)
		{
			str += "Validation Layer: \n";
			for (uint64 i = 0; i < pInfo->GetNumStoredMessages(); ++i)
			{
				size_t messageLength = 0;
				pInfo->GetMessageA(0, nullptr, &messageLength);
				D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
				pInfo->GetMessageA(0, pMessage, &messageLength);
				str += pMessage->pDescription;
				str += "\n";
				free(pMessage);
			}
		}

		HRESULT removedReason = pDevice->GetDeviceRemovedReason();
		str += "\nDRED: " + GetErrorString(removedReason, nullptr);
	}
	return str;
}

void SetObjectName(ID3D12Object* pObject, const char* pName)
{
	if (pObject)
		VERIFY_HR_EX(pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (uint32)strlen(pName) + 1, pName), nullptr);
}

String GetObjectName(ID3D12Object* pObject)
{
	String out;
	if (pObject)
	{
		uint32 size = 0;
		if (SUCCEEDED(pObject->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr)))
		{
			out.resize(size);
			VERIFY_HR(pObject->GetPrivateData(WKPDID_D3DDebugObjectName, &size, &out[0]));
		}
	}
	return out;
}

String BarrierToString(const D3D12_RESOURCE_BARRIER& barrier)
{
	if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
	{
		return Sprintf("Transition | Resource: '%s' (%x) | Before %s | After %s",
					   D3D::GetObjectName(barrier.Transition.pResource),
					   barrier.Transition.pResource,
					   D3D::ResourceStateToString(barrier.Transition.StateBefore),
					   D3D::ResourceStateToString(barrier.Transition.StateAfter));
	}
	if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
	{
		return Sprintf("UAV | Resource: '%s' (%x)",
					   D3D::GetObjectName(barrier.UAV.pResource),
					   barrier.UAV.pResource);
	}
	if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
	{
		return Sprintf("Aliasing | Before: '%s' (%x) | After: '%s' (%x)",
					   D3D::GetObjectName(barrier.Aliasing.pResourceBefore), barrier.Aliasing.pResourceBefore,
					   D3D::GetObjectName(barrier.Aliasing.pResourceAfter), barrier.Aliasing.pResourceAfter);
	}
	return "[Invalid]";
}

void GetResourceAllocationInfo(ID3D12Device* pDevice, const D3D12_RESOURCE_DESC& resourceDesc, uint64& outSize, uint64& outAlignment)
{
	if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		outSize		 = resourceDesc.Width;
		outAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		return;
	}

	struct AllocationInfo
	{
		uint64 Alignment;
		uint64 Size;
	};
	static HashMap<D3D12_RESOURCE_DESC, AllocationInfo, ResourceDescHash, ResourceDescEqual> lut;
	static RWMutex																			 lock;

	{
		ScopedReadLock scope(lock);
		auto		   it = lut.find(resourceDesc);
		if (it != lut.end())
		{
			outSize		 = it->second.Size;
			outAlignment = it->second.Alignment;
			return;
		}
	}

	D3D12_RESOURCE_ALLOCATION_INFO allocInfo = pDevice->GetResourceAllocationInfo(0, 1, &resourceDesc);
	AllocationInfo				   info{
						.Alignment = allocInfo.Alignment,
						.Size	   = allocInfo.SizeInBytes,
	};

	{
		ScopedWriteLock scope(lock);
		lut[resourceDesc] = info;
	}

	outSize		 = info.Size;
	outAlignment = info.Alignment;
}

String GetResourceDescription(ID3D12Resource* pResource)
{
	if (!pResource)
		return "nullptr";

	D3D12_RESOURCE_DESC resourceDesc = pResource->GetDesc();
	Ref<ID3D12Device>	pDevice;
	pResource->GetDevice(IID_PPV_ARGS(pDevice.GetAddressOf()));
	D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = pDevice->GetResourceAllocationInfo(1, 1, &resourceDesc);

	if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		return Sprintf("[Buffer] '%s' | %s | Alignment: %s",
					   D3D::GetObjectName(pResource),
					   Math::PrettyPrintDataSize(allocationInfo.SizeInBytes),
					   Math::PrettyPrintDataSize(allocationInfo.Alignment));
	}
	else if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			 resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			 resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
	{
		const char* pType = "";
		switch (resourceDesc.Dimension)
		{
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			pType = "Texture1D";
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			pType = "Texture2D";
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			pType = "Texture3D";
			break;
		default:
			gUnreachable();
		}

		// Find ResourceFormat from DXGI_FORMAT
		ResourceFormat format = ResourceFormat::Unknown;
		for (int i = 0; i < ARRAYSIZE(gDXGIFormatMap); ++i)
		{
			if (resourceDesc.Format == gDXGIFormatMap[i])
			{
				format = (ResourceFormat)i;
				break;
			}
		}
		const FormatInfo& info = RHI::GetFormatInfo(format);

		return Sprintf("[%s] '%s' | %s | %dx%dx%d | %s | Alignment: %s",
					   pType,
					   D3D::GetObjectName(pResource),
					   info.pName,
					   resourceDesc.Width,
					   resourceDesc.Height,
					   resourceDesc.DepthOrArraySize,
					   Math::PrettyPrintDataSize(allocationInfo.SizeInBytes),
					   Math::PrettyPrintDataSize(allocationInfo.Alignment));
	}
	return "Unknown";
}

static constexpr GUID ResourceCallstackGUID = { 0xe8241f90, 0xff0a, 0x4dd4, { 0xaa, 0xf5, 0xb4, 0x53, 0xe1, 0x91, 0x96, 0x5e } };

void SetResourceCallstack(ID3D12Object* pObject)
{
	Callstack<6> callstack;
	callstack.Trace(1);
	pObject->SetPrivateData(ResourceCallstackGUID, sizeof(callstack), &callstack);
}

bool GetResourceCallstack(ID3D12Object* pObject, Callstack<6>& outCallstack)
{
	uint32 size = sizeof(outCallstack);
	if (SUCCEEDED(pObject->GetPrivateData(ResourceCallstackGUID, &size, &outCallstack)))
		return true;
	return false;
}


D3D12_RESOURCE_DESC GetResourceDesc(const BufferDesc& bufferDesc)
{
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferDesc.Size, D3D12_RESOURCE_FLAG_NONE);
	if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::UnorderedAccess))
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::AccelerationStructure))
		desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
	return desc;
}


D3D12_RESOURCE_DESC GetResourceDesc(const TextureDesc& textureDesc)
{
	DXGI_FORMAT format = D3D::ConvertFormat(textureDesc.Format);

	D3D12_RESOURCE_DESC desc{};
	switch (textureDesc.Type)
	{
	case TextureType::Texture1D:
	case TextureType::Texture1DArray:
		desc = CD3DX12_RESOURCE_DESC::Tex1D(format, textureDesc.Width, (uint16)textureDesc.ArraySize, (uint16)textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	case TextureType::Texture2D:
	case TextureType::Texture2DArray:
		desc = CD3DX12_RESOURCE_DESC::Tex2D(format, textureDesc.Width, textureDesc.Height, (uint16)textureDesc.ArraySize, (uint16)textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	case TextureType::TextureCube:
	case TextureType::TextureCubeArray:
		desc = CD3DX12_RESOURCE_DESC::Tex2D(format, textureDesc.Width, textureDesc.Height, (uint16)textureDesc.ArraySize * 6, (uint16)textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	case TextureType::Texture3D:
		desc = CD3DX12_RESOURCE_DESC::Tex3D(format, textureDesc.Width, textureDesc.Height, (uint16)textureDesc.Depth, (uint16)textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
		break;
	default:
		gUnreachable();
		break;
	}

	if (EnumHasAnyFlags(textureDesc.Flags, TextureFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if (EnumHasAnyFlags(textureDesc.Flags, TextureFlag::RenderTarget))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}
	if (EnumHasAnyFlags(textureDesc.Flags, TextureFlag::DepthStencil))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (!EnumHasAnyFlags(textureDesc.Flags, TextureFlag::ShaderResource))
		{
			// I think this can be a significant optimization on some devices because then the depth buffer can never be (de)compressed
			desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
	}
	return desc;
}

} // namespace D3D
