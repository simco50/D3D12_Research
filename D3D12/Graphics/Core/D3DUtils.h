#pragma once

#include <DXProgrammableCapture.h>
#include "pix3.h"
#include "Core/Paths.h"
#include <winuser.h>

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

#define STATE_CASE(name) if((state & D3D12_RESOURCE_STATE_##name) == D3D12_RESOURCE_STATE_##name) AddText(#name)

		STATE_CASE(VERTEX_AND_CONSTANT_BUFFER);
		STATE_CASE(INDEX_BUFFER);
		STATE_CASE(RENDER_TARGET);
		STATE_CASE(UNORDERED_ACCESS);
		STATE_CASE(DEPTH_WRITE);
		STATE_CASE(DEPTH_READ);
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
		STATE_CASE(GENERIC_READ);
		STATE_CASE(VIDEO_DECODE_READ);
		STATE_CASE(VIDEO_DECODE_WRITE);
		STATE_CASE(VIDEO_PROCESS_READ);
		STATE_CASE(VIDEO_PROCESS_WRITE);
		STATE_CASE(VIDEO_ENCODE_READ);
		STATE_CASE(VIDEO_ENCODE_WRITE);
#undef STATE_CASE
		return outString;
	}

	inline const char* CommandlistTypeToString(D3D12_COMMAND_LIST_TYPE type)
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
		HWND window = GetActiveWindow();
		if (SUCCEEDED(PIXSetTargetWindow(window)))
		{
			SYSTEMTIME time;
			GetSystemTime(&time);
			Paths::CreateDirectoryTree(Paths::SavedDir());
			char filePath[128];
			FormatString(filePath, ARRAYSIZE(filePath), "%sGPU_Capture_%d_%02d_%02d__%02d_%02d_%02d_%d.wpix",
				Paths::SavedDir().c_str(),
				time.wYear, time.wMonth, time.wDay,
				time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
			if (SUCCEEDED(PIXGpuCaptureNextFrames(MULTIBYTE_TO_UNICODE(filePath), numFrames)))
			{
				E_LOG(Info, "Captured %d frames to '%s'", numFrames, filePath);
			}
		}
	}

	inline void BeginCapture(const char* pFileName)
	{
		PIXCaptureParameters parameters;
		parameters.GpuCaptureParameters.FileName = MULTIBYTE_TO_UNICODE(pFileName);
		if (SUCCEEDED(::PIXBeginCapture(PIX_CAPTURE_GPU, &parameters)))
		{
			E_LOG(Info, "Started PIX capture to file %s", pFileName);
		}
		else
		{
			E_LOG(Info, "Failed to start PIX capture");
		}
	}

	inline void EndCapture()
	{
		if (SUCCEEDED(::PIXEndCapture(false)))
		{
			E_LOG(Info, "Finished PIX capture");
		}
		else
		{
			E_LOG(Info, "Failed to finish PIX capture");
		}
	}

	template<size_t hash>
	class PixCaptureScope
	{
	public:
		PixCaptureScope()
		{
			if (!sCaptured)
			{
				SYSTEMTIME time;
				GetSystemTime(&time);
				Paths::CreateDirectoryTree(Paths::SavedDir());
				char filePath[128];
				FormatString(filePath, ARRAYSIZE(filePath), "%sGPU_Capture_%d_%02d_%02d__%02d_%02d_%02d_%d.wpix",
					Paths::SavedDir().c_str(),
					time.wYear, time.wMonth, time.wDay,
					time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);

				BeginCapture(filePath);
			}
		}

		~PixCaptureScope()
		{
			if (!sCaptured)
			{
				EndCapture();
				sCaptured = 1;
			}
		}

		inline static int sCaptured = 0;
	};

#define PIX_CAPTURE_ONCE() D3D::PixCaptureScope<__COUNTER__> MACRO_CONCAT(pix_capture,__COUNTER__)

	inline void DREDHandler(ID3D12Device* pDevice)
	{
		//D3D12_AUTO_BREADCRUMB_OP
		constexpr const char* OpNames[] =
		{
			"SetMarker",
			"BeginEvent",
			"EndEvent",
			"DrawInstanced",
			"DrawIndexedInstanced",
			"ExecuteIndirect",
			"Dispatch",
			"CopyBufferRegion",
			"CopyTextureRegion",
			"CopyResource",
			"CopyTiles",
			"ResolveSubresource",
			"ClearRenderTargetView",
			"ClearUnorderedAccessView",
			"ClearDepthStencilView",
			"ResourceBarrier",
			"ExecuteBundle",
			"Present",
			"ResolveQueryData",
			"BeginSubmission",
			"EndSubmission",
			"DecodeFrame",
			"ProcessFrames",
			"AtomicCopyBufferUint",
			"AtomicCopyBufferUint64",
			"ResolveSubresourceRegion",
			"WriteBufferImmediate",
			"DecodeFrame1",
			"SetProtectedResourceSession",
			"DecodeFrame2",
			"ProcessFrames1",
			"BuildRaytracingAccelerationStructure",
			"EmitRaytracingAccelerationStructurePostBuildInfo",
			"CopyRaytracingAccelerationStructure",
			"DispatchRays",
			"InitializeMetaCommand",
			"ExecuteMetaCommand",
			"EstimateMotion",
			"ResolveMotionVectorHeap",
			"SetPipelineState1",
			"InitializeExtensionCommand",
			"ExecuteExtensionCommand",
			"DispatchMesh",
			"EncodeFrame",
			"ResolveEncoderOutputMetadata",
		};
		static_assert(ARRAYSIZE(OpNames) == D3D12_AUTO_BREADCRUMB_OP_RESOLVEENCODEROUTPUTMETADATA + 1, "OpNames array length mismatch");

		//D3D12_DRED_ALLOCATION_TYPE
		constexpr const char* AllocTypesNames[] =
		{
			"CommandQueue",
			"CommandAllocator",
			"PipelineState",
			"CommandList",
			"Fence",
			"DescriptorHeap",
			"Heap",
			"Unknown",
			"QueryHeap",
			"CommandSignature",
			"PipelineLibrary",
			"VideoDecoder",
			"Unknown",
			"VideoProcessor",
			"Unknown",
			"Resource",
			"Pass",
			"CryptoSession",
			"CryptoSessionPolicy",
			"ProtectedResourceSession",
			"VideoDecoderHeap",
			"CommandPool",
			"CommandRecorder",
			"StateObjectr",
			"MetaCommand",
			"SchedulingGroup",
			"VideoMotionEstimator",
			"VideoMotionVectorHeap",
			"VideoExtensionCommand",
			"VideoEncoder",
			"VideoEncoderHeap",
		};
		static_assert(ARRAYSIZE(AllocTypesNames) == D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER_HEAP - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE + 1, "AllocTypes array length mismatch");

		ID3D12DeviceRemovedExtendedData2* pDred = nullptr;
		if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pDred))))
		{
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 pDredAutoBreadcrumbsOutput;
			if (SUCCEEDED(pDred->GetAutoBreadcrumbsOutput1(&pDredAutoBreadcrumbsOutput)))
			{
				E_LOG(Warning, "[DRED] Last tracked GPU operations:");

				std::map<int32, const wchar_t*> contextStrings;

				const D3D12_AUTO_BREADCRUMB_NODE1* pNode = pDredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
				while (pNode && pNode->pLastBreadcrumbValue)
				{
					int32 lastCompletedOp = *pNode->pLastBreadcrumbValue;

					if (lastCompletedOp != (int)pNode->BreadcrumbCount && lastCompletedOp != 0)
					{
						E_LOG(Warning, "[DRED] Commandlist \"%s\" on CommandQueue \"%s\", %d completed of %d", pNode->pCommandListDebugNameA, pNode->pCommandQueueDebugNameA, lastCompletedOp, pNode->BreadcrumbCount);

						int32 firstOp = Math::Max(lastCompletedOp - 100, 0);
						int32 lastOp = Math::Min(lastCompletedOp + 20, int32(pNode->BreadcrumbCount) - 1);

						contextStrings.clear();
						for (uint32 breadcrumbContext = firstOp; breadcrumbContext < pNode->BreadcrumbContextsCount; ++breadcrumbContext)
						{
							const D3D12_DRED_BREADCRUMB_CONTEXT& context = pNode->pBreadcrumbContexts[breadcrumbContext];
							contextStrings[context.BreadcrumbIndex] = context.pContextString;
						}

						for (int32 op = firstOp; op <= lastOp; ++op)
						{
							D3D12_AUTO_BREADCRUMB_OP breadcrumbOp = pNode->pCommandHistory[op];

							std::string contextString;
							auto it = contextStrings.find(op);
							if (it != contextStrings.end())
							{
								contextString = Sprintf(" [%s]", UNICODE_TO_MULTIBYTE(it->second));
							}

							const char* opName = (breadcrumbOp < ARRAYSIZE(OpNames)) ? OpNames[breadcrumbOp] : "Unknown Op";
							E_LOG(Warning, "\tOp: %d, %s%s%s", op, opName, contextString.c_str(), (op + 1 == lastCompletedOp) ? " - Last completed" : "");
						}
					}
					pNode = pNode->pNext;
				}
			}

			D3D12_DRED_PAGE_FAULT_OUTPUT2 DredPageFaultOutput;
			if (SUCCEEDED(pDred->GetPageFaultAllocationOutput2(&DredPageFaultOutput)) && DredPageFaultOutput.PageFaultVA != 0)
			{
				E_LOG(Warning, "[DRED] PageFault at VA GPUAddress \"0x%x\"", DredPageFaultOutput.PageFaultVA);

				const D3D12_DRED_ALLOCATION_NODE1* pNode = DredPageFaultOutput.pHeadExistingAllocationNode;
				if (pNode)
				{
					E_LOG(Warning, "[DRED] Active objects with VA ranges that match the faulting VA:");
					while (pNode)
					{
						uint32 alloc_type_index = pNode->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
						const TCHAR* AllocTypeName = (alloc_type_index < ARRAYSIZE(AllocTypesNames)) ? AllocTypesNames[alloc_type_index] : "Unknown Alloc";
						E_LOG(Warning, "\tName: %s (Type: %s)", pNode->ObjectNameA, AllocTypeName);
						pNode = pNode->pNext;
					}
				}

				pNode = DredPageFaultOutput.pHeadRecentFreedAllocationNode;
				if (pNode)
				{
					E_LOG(Warning, "[DRED] Recent freed objects with VA ranges that match the faulting VA:");
					while (pNode)
					{
						uint32 allocTypeIndex = pNode->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
						const TCHAR* AllocTypeName = (allocTypeIndex < ARRAYSIZE(AllocTypesNames)) ? AllocTypesNames[allocTypeIndex] : "Unknown Alloc";
						E_LOG(Warning, "\tName: %s (Type: %s)", pNode->ObjectNameA, AllocTypeName);
						pNode = pNode->pNext;
					}
				}
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
			ComPtr<ID3D12InfoQueue> pInfo;
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

	inline bool IsBlockCompressFormat(DXGI_FORMAT format)
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

	inline DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format)
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
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	inline DXGI_FORMAT GetDsvFormat(DXGI_FORMAT format)
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

	inline bool HasStencil(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_D24_UNORM_S8_UINT
			|| format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	}

	inline int GetFormatRowDataSize(DXGI_FORMAT format, unsigned int width)
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
