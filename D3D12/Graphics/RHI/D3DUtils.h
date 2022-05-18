#pragma once

#include "pix3.h"
#include "Core/Paths.h"
#include "Core/Utils.h"

#define VERIFY_HR(hr) D3D::LogHRESULT(hr, nullptr, #hr, __FILE__, __LINE__)
#define VERIFY_HR_EX(hr, device) D3D::LogHRESULT(hr, device, #hr, __FILE__, __LINE__)

namespace D3D
{
	inline std::string ResourceStateToString(D3D12_RESOURCE_STATES state)
	{
		if (state == 0)
		{
			return "COMMON";
		}

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

	constexpr const char* FormatToString(DXGI_FORMAT format)
	{
#define STATE_CASE(fmt) case DXGI_FORMAT_##fmt: return #fmt
		switch (format)
		{
			// List does not include _TYPELESS formats
			STATE_CASE(R32G32B32A32_FLOAT);
			STATE_CASE(R32G32B32A32_UINT);
			STATE_CASE(R32G32B32A32_SINT);
			STATE_CASE(R32G32B32_FLOAT);
			STATE_CASE(R32G32B32_UINT);
			STATE_CASE(R32G32B32_SINT);
			STATE_CASE(R16G16B16A16_FLOAT);
			STATE_CASE(R16G16B16A16_UNORM);
			STATE_CASE(R16G16B16A16_UINT);
			STATE_CASE(R16G16B16A16_SNORM);
			STATE_CASE(R16G16B16A16_SINT);
			STATE_CASE(R32G32_FLOAT);
			STATE_CASE(R32G32_UINT);
			STATE_CASE(R32G32_SINT);
			STATE_CASE(R10G10B10A2_UNORM);
			STATE_CASE(R10G10B10A2_UINT);
			STATE_CASE(R11G11B10_FLOAT);
			STATE_CASE(R8G8B8A8_UNORM);
			STATE_CASE(R8G8B8A8_UNORM_SRGB);
			STATE_CASE(R8G8B8A8_UINT);
			STATE_CASE(R8G8B8A8_SNORM);
			STATE_CASE(R8G8B8A8_SINT);
			STATE_CASE(R16G16_FLOAT);
			STATE_CASE(R16G16_UNORM);
			STATE_CASE(R16G16_UINT);
			STATE_CASE(R16G16_SNORM);
			STATE_CASE(R16G16_SINT);
			STATE_CASE(R32_FLOAT);
			STATE_CASE(R32_UINT);
			STATE_CASE(R32_SINT);
			STATE_CASE(R8G8_UNORM);
			STATE_CASE(R8G8_UINT);
			STATE_CASE(R8G8_SNORM);
			STATE_CASE(R8G8_SINT);
			STATE_CASE(R16_FLOAT);
			STATE_CASE(R16_UNORM);
			STATE_CASE(R16_UINT);
			STATE_CASE(R16_SNORM);
			STATE_CASE(R16_SINT);
			STATE_CASE(R8_UNORM);
			STATE_CASE(R8_UINT);
			STATE_CASE(R8_SNORM);
			STATE_CASE(R8_SINT);
			STATE_CASE(A8_UNORM);
			STATE_CASE(R9G9B9E5_SHAREDEXP);
			STATE_CASE(R8G8_B8G8_UNORM);
			STATE_CASE(G8R8_G8B8_UNORM);
			STATE_CASE(BC1_UNORM);
			STATE_CASE(BC1_UNORM_SRGB);
			STATE_CASE(BC2_UNORM);
			STATE_CASE(BC2_UNORM_SRGB);
			STATE_CASE(BC3_UNORM);
			STATE_CASE(BC3_UNORM_SRGB);
			STATE_CASE(BC4_UNORM);
			STATE_CASE(BC4_SNORM);
			STATE_CASE(BC5_UNORM);
			STATE_CASE(BC5_SNORM);
			STATE_CASE(B5G6R5_UNORM);
			STATE_CASE(B5G5R5A1_UNORM);

			// DXGI 1.1 formats
			STATE_CASE(B8G8R8A8_UNORM);
			STATE_CASE(B8G8R8X8_UNORM);
			STATE_CASE(R10G10B10_XR_BIAS_A2_UNORM);
			STATE_CASE(B8G8R8A8_UNORM_SRGB);
			STATE_CASE(B8G8R8X8_UNORM_SRGB);
			STATE_CASE(BC6H_UF16);
			STATE_CASE(BC6H_SF16);
			STATE_CASE(BC7_UNORM);
			STATE_CASE(BC7_UNORM_SRGB);

			// Depth
			STATE_CASE(D32_FLOAT_S8X24_UINT);
			STATE_CASE(D32_FLOAT);
			STATE_CASE(D24_UNORM_S8_UINT);
			STATE_CASE(D16_UNORM);

		default: return "UNKNOWN";
		}
	};

	inline void EnqueuePIXCapture(uint32 numFrames = 1)
	{
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
			RefCountPtr<ID3D12InfoQueue> pInfo;
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

	inline void SetObjectName(ID3D12Object* pObject, const char* pName)
	{
		if (pObject && pName)
		{
			VERIFY_HR_EX(pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (uint32)strlen(pName), pName), nullptr);
		}
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

	constexpr bool IsBlockCompressFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	constexpr DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format)
	{
		switch (format)
		{
			// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

			// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;

			// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

			// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;

		default:
			return format;
		}
	}

	constexpr DXGI_FORMAT GetDsvFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		default:
			return format;
		}
	}

	constexpr bool HasStencil(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_D24_UNORM_S8_UINT
			|| format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	}

	constexpr int GetFormatRowDataSize(DXGI_FORMAT format, unsigned int width)
	{
		switch (format)
		{
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_R8_UINT:
			return (unsigned)width;

		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_UINT:
			return (unsigned)(width * 2);

		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_UINT:
			return (unsigned)(width * 4);

		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return (unsigned)(width * 8);

		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return (unsigned)(width * 16);

		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return (unsigned)(((width + 3) >> 2) * 8);

		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return (unsigned)(((width + 3) >> 2) * 16);
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return width * 3 * sizeof(float);
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
			return width * 2 * sizeof(float);
		default:
			noEntry();
			return 0;
		}
	}
}
