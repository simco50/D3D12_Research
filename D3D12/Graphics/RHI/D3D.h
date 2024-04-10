#pragma once

#define ENABLE_PIX 1

#if ENABLE_PIX
#define USE_PIX
#include "pix3.h"
#endif

#include "RHI.h"
#include "Core/Paths.h"
#include "Core/Utils.h"

#define VERIFY_HR(hr) D3D::LogHRESULT(hr, nullptr, #hr, __FILE__, __LINE__)
#define VERIFY_HR_EX(hr, device) D3D::LogHRESULT(hr, device, #hr, __FILE__, __LINE__)

namespace D3D
{
	inline std::string ResourceStateToString(D3D12_RESOURCE_STATES state)
	{
		if (state == 0)
			return "COMMON";
		if (state == -1)
			return "UNKNOWN";

		char outString[1024];
		outString[0] = '\0';
		char* pCurrent = outString;
		int i = 0;
		auto AddText = [&](const char* pText)
		{
			if (i++ > 0)
				*pCurrent++ = '/';
			strcpy_s(pCurrent, 1024 - (pCurrent - outString), pText);
			size_t len = strlen(pText);
			pCurrent += len;
		};

#define STATE_CASE(name) if((state & D3D12_RESOURCE_STATE_##name) == D3D12_RESOURCE_STATE_##name) { AddText(#name); state &= ~D3D12_RESOURCE_STATE_##name; }

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

	constexpr const char* CommandlistTypeToString(D3D12_COMMAND_LIST_TYPE type)
	{
#define STATE_CASE(name) case D3D12_COMMAND_LIST_TYPE_##name: return #name
		switch (type)
		{
			STATE_CASE(DIRECT);
			STATE_CASE(COMPUTE);
			STATE_CASE(COPY);
			STATE_CASE(BUNDLE);
			STATE_CASE(VIDEO_DECODE);
			STATE_CASE(VIDEO_ENCODE);
			STATE_CASE(VIDEO_PROCESS);
			default: return "";
		}
#undef STATE_CASE
	}

	inline void EnqueuePIXCapture(uint32 numFrames = 1)
	{
#ifdef USE_PIX
		HWND window = GetActiveWindow();
		if (SUCCEEDED(PIXSetTargetWindow(window)))
		{
			SYSTEMTIME time;
			GetSystemTime(&time);
			Paths::CreateDirectoryTree(Paths::SavedDir());
			std::string filePath = Sprintf("%ssGPU_Capture_%s.wpix", Paths::SavedDir().c_str(), Utils::GetTimeString().c_str());
			if (SUCCEEDED(PIXGpuCaptureNextFrames(MULTIBYTE_TO_UNICODE(filePath.c_str()), numFrames)))
			{
				E_LOG(Info, "Captured %d frames to '%s'", numFrames, filePath.c_str());
			}
		}
#endif
	}

	inline std::string GetErrorString(HRESULT errorCode, ID3D12Device* pDevice)
	{
		std::string str;
		char* errorMsg;
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

	inline bool LogHRESULT(HRESULT hr, ID3D12Device* pDevice, const char* pCode, const char* pFileName, uint32 lineNumber)
	{
		if (!SUCCEEDED(hr))
		{
			E_LOG(Error, "%s:%d: %s - %s", pFileName, lineNumber, GetErrorString(hr, pDevice).c_str(), pCode);
			__debugbreak();
			return false;
		}
		return true;
	}

	static bool HasWriteResourceState(D3D12_RESOURCE_STATES state)
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
			D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE
		);
	};

	static bool CanCombineResourceState(D3D12_RESOURCE_STATES stateA, D3D12_RESOURCE_STATES stateB)
	{
		return !HasWriteResourceState(stateA) && !HasWriteResourceState(stateB);
	}

	static bool IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandlistType, D3D12_RESOURCE_STATES state)
	{
		constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES =
			D3D12_RESOURCE_STATE_COMMON
			| D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			| D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			| D3D12_RESOURCE_STATE_COPY_DEST
			| D3D12_RESOURCE_STATE_COPY_SOURCE
			| D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

		constexpr int VALID_COPY_QUEUE_RESOURCE_STATES =
			D3D12_RESOURCE_STATE_COMMON
			| D3D12_RESOURCE_STATE_COPY_DEST
			| D3D12_RESOURCE_STATE_COPY_SOURCE;

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

	static bool NeedsTransition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES& after, bool allowCombine)
	{
		if (before == after)
			return false;

		// When resolving pending resource barriers, combining resource states is not working
		// This is because the last known resource state of the resource is used to update the resource
		// And so combining after_state during the result will result in the last known resource state not matching up.
		if (!allowCombine)
			return true;

		//Can read from 'write' DSV
		if (before == D3D12_RESOURCE_STATE_DEPTH_WRITE && after == D3D12_RESOURCE_STATE_DEPTH_READ)
			return false;

		if (after == D3D12_RESOURCE_STATE_COMMON)
			return before != D3D12_RESOURCE_STATE_COMMON;

		//Combine already transitioned bits
		if (D3D::CanCombineResourceState(before, after) && !EnumHasAllFlags(before, after))
			after |= before;

		return true;
	}

	inline void SetObjectName(ID3D12Object* pObject, const char* pName)
	{
		if (pObject)
			VERIFY_HR_EX(pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (uint32)strlen(pName) + 1, pName), nullptr);
	}

	inline std::string GetObjectName(ID3D12Object* pObject)
	{
		std::string out;
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

	inline std::string BarrierToString(const D3D12_RESOURCE_BARRIER& barrier)
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

	constexpr static const DXGI_FORMAT gDXGIFormatMap[] =
	{
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

	static std::string GetResourceDescription(ID3D12Resource* pResource)
	{
		if (!pResource)
			return "nullptr";

		D3D12_RESOURCE_DESC resourceDesc = pResource->GetDesc();
		Ref<ID3D12Device> pDevice;
		pResource->GetDevice(IID_PPV_ARGS(pDevice.GetAddressOf()));
		D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = pDevice->GetResourceAllocationInfo(1, 1, &resourceDesc);

		if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			return Sprintf("[Buffer] '%s' | %s | Alignment: %s",
				D3D::GetObjectName(pResource),
				Math::PrettyPrintDataSize(allocationInfo.SizeInBytes),
				Math::PrettyPrintDataSize(allocationInfo.Alignment));
		}
		else if(resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
		{
			const char* pType = "";
			switch (resourceDesc.Dimension)
			{
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D:	pType = "Texture1D"; break;
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D:	pType = "Texture2D"; break;
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D:	pType = "Texture3D"; break;
			default: noEntry();
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

	static Utils::ForceFunctionToBeLinked forceLink(GetResourceDescription);
}
