#include "stdafx.h"
#include "Device.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "DeviceResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "RingBufferAllocator.h"
#include "GPUDescriptorHeap.h"
#include "Texture.h"
#include "Buffer.h"
#include "StateObject.h"
#include "pix3.h"
#include "dxgidebug.h"
#include "Core/Commandline.h"
#include "Core/Callstack.h"

GraphicsDevice::DRED::DRED(GraphicsDevice* pDevice)
{
	auto OnDeviceRemovedCallback = [](void* pContext, BOOLEAN) {

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
			"Barrier",
			"BeginCommandList",
			"DispatchGraph",
			"SetProgram",
		};
		static_assert(ARRAYSIZE(OpNames) == D3D12_AUTO_BREADCRUMB_OP_SETPROGRAM + 1, "OpNames array length mismatch");

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

		ID3D12Device* pDevice = (ID3D12Device*)pContext;
		ID3D12DeviceRemovedExtendedData2* pDred = nullptr;
		if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pDred))))
		{
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 pDredAutoBreadcrumbsOutput;
			if (SUCCEEDED(pDred->GetAutoBreadcrumbsOutput1(&pDredAutoBreadcrumbsOutput)))
			{
				E_LOG(Warning, "[DRED] Last tracked GPU operations:");

				HashMap<int32, const wchar_t*> contextStrings;

				const D3D12_AUTO_BREADCRUMB_NODE1* pNode = pDredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
				while (pNode && pNode->pLastBreadcrumbValue)
				{
					uint32 lastCompletedOp = *pNode->pLastBreadcrumbValue;

					if (lastCompletedOp != (int)pNode->BreadcrumbCount && lastCompletedOp != 0)
					{
						E_LOG(Warning, "[DRED] Commandlist \"%s\" on CommandQueue \"%s\", %d completed of %d", pNode->pCommandListDebugNameA, pNode->pCommandQueueDebugNameA, lastCompletedOp, pNode->BreadcrumbCount);

						int32 firstOp = Math::Max(lastCompletedOp - 100, 0u);
						int32 lastOp = Math::Min(lastCompletedOp + 20, pNode->BreadcrumbCount - 1);

						contextStrings.clear();
						for (uint32 breadcrumbContext = firstOp; breadcrumbContext < pNode->BreadcrumbContextsCount; ++breadcrumbContext)
						{
							const D3D12_DRED_BREADCRUMB_CONTEXT& context = pNode->pBreadcrumbContexts[breadcrumbContext];
							contextStrings[context.BreadcrumbIndex] = context.pContextString;
						}

						for (int32 op = firstOp; op <= lastOp; ++op)
						{
							D3D12_AUTO_BREADCRUMB_OP breadcrumbOp = pNode->pCommandHistory[op];

							String contextString;
							auto it = contextStrings.find(op);
							if (it != contextStrings.end())
							{
								contextString = Sprintf(" [%s]", UNICODE_TO_MULTIBYTE(it->second));
							}

							const char* opName = (breadcrumbOp < ARRAYSIZE(OpNames)) ? OpNames[breadcrumbOp] : "Unknown Op";
							E_LOG(Warning, "\tOp: %d, %s%s%s", op, opName, contextString.c_str(), (op + 1u == lastCompletedOp) ? " - Last completed" : "");
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
	};

	m_pFence = new Fence(pDevice, "Device Removed Fence");
	m_WaitHandle = CreateEventA(nullptr, false, false, nullptr);
	VERIFY_HR(m_pFence->GetFence()->SetEventOnCompletion(UINT64_MAX, m_WaitHandle));
	gVerify(RegisterWaitForSingleObject(&m_WaitHandle, m_WaitHandle, OnDeviceRemovedCallback, pDevice->GetDevice(), INFINITE, 0), == TRUE);
}

GraphicsDevice::DRED::~DRED()
{
	if (m_pFence)
	{
		m_pFence->Signal(UINT64_MAX);
		gVerify(UnregisterWaitEx(m_WaitHandle, INVALID_HANDLE_VALUE), == TRUE);
	}
}

GraphicsDevice::LiveObjectReporter::~LiveObjectReporter()
{
	Ref<IDXGIDebug1> pDXGIDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(pDXGIDebug.GetAddressOf()))))
	{
		Ref<IDXGIInfoQueue> pInfoQueue;
		VERIFY_HR(DXGIGetDebugInterface1(0, IID_PPV_ARGS(pInfoQueue.GetAddressOf())));
		pInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
		VERIFY_HR(pDXGIDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_IGNORE_INTERNAL | DXGI_DEBUG_RLO_DETAIL)));
		gAssert(pInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL) == 0);
	}
}


static void MessageCallback(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR pDescription, void* pContext)
{
	switch (id)
	{
	case D3D12_MESSAGE_ID_LIVE_OBJECT_SUMMARY:
	case D3D12_MESSAGE_ID_LIVE_DEVICE:
	case D3D12_MESSAGE_ID_LIVE_SWAPCHAIN:
	case D3D12_MESSAGE_ID_LIVE_COMMANDQUEUE:
	case D3D12_MESSAGE_ID_LIVE_COMMANDALLOCATOR:
	case D3D12_MESSAGE_ID_LIVE_PIPELINESTATE:
	case D3D12_MESSAGE_ID_LIVE_COMMANDLIST12:
	case D3D12_MESSAGE_ID_LIVE_RESOURCE:
	case D3D12_MESSAGE_ID_LIVE_DESCRIPTORHEAP:
	case D3D12_MESSAGE_ID_LIVE_ROOTSIGNATURE:
	case D3D12_MESSAGE_ID_LIVE_LIBRARY:
	case D3D12_MESSAGE_ID_LIVE_HEAP:
	case D3D12_MESSAGE_ID_LIVE_MONITOREDFENCE:
	case D3D12_MESSAGE_ID_LIVE_QUERYHEAP:
	case D3D12_MESSAGE_ID_LIVE_COMMANDSIGNATURE:
	case D3D12_MESSAGE_ID_LIVE_PIPELINELIBRARY:
	case D3D12_MESSAGE_ID_LIVE_VIDEODECODECOMMANDLIST:
	case D3D12_MESSAGE_ID_LIVE_VIDEODECODER:
	case D3D12_MESSAGE_ID_LIVE_VIDEODECODESTREAM:
	case D3D12_MESSAGE_ID_LIVE_VIDEODECODECOMMANDQUEUE:
	case D3D12_MESSAGE_ID_LIVE_VIDEOPROCESSCOMMANDLIST:
	case D3D12_MESSAGE_ID_LIVE_VIDEOPROCESSCOMMANDQUEUE:
	case D3D12_MESSAGE_ID_LIVE_VIDEOPROCESSOR:
	case D3D12_MESSAGE_ID_LIVE_VIDEOPROCESSSTREAM:
	case D3D12_MESSAGE_ID_LIVE_CRYPTO_SESSION:
	case D3D12_MESSAGE_ID_LIVE_CRYPTO_SESSION_POLICY:
	case D3D12_MESSAGE_ID_LIVE_PROTECTED_RESOURCE_SESSION:
	case D3D12_MESSAGE_ID_LIVE_VIDEODECODERHEAP:
	case D3D12_MESSAGE_ID_LIVE_COMMANDRECORDER:
	case D3D12_MESSAGE_ID_LIVE_COMMANDPOOL:
	case D3D12_MESSAGE_ID_LIVE_META_COMMAND:
	case D3D12_MESSAGE_ID_LIVE_LIFETIMETRACKER:
	case D3D12_MESSAGE_ID_LIVE_TRACKEDWORKLOAD:
	case D3D12_MESSAGE_ID_LIVE_VIDEOENCODECOMMANDLIST:
	case D3D12_MESSAGE_ID_LIVE_VIDEOENCODECOMMANDQUEUE:
	case D3D12_MESSAGE_ID_LIVE_VIDEOMOTIONESTIMATOR:
	case D3D12_MESSAGE_ID_LIVE_VIDEOMOTIONVECTORHEAP:
	case D3D12_MESSAGE_ID_LIVE_VIDEOEXTENSIONCOMMAND:
	case D3D12_MESSAGE_ID_LIVE_SHADERCACHESESSION:
	case D3D12_MESSAGE_ID_LIVE_VIDEOENCODER:
	case D3D12_MESSAGE_ID_LIVE_VIDEOENCODERHEAP: {
		E_LOG(Warning, "D3D12 Resource Leak: %s", pDescription);
		const char*	pFindResult = strstr(pDescription, "Live ID3D12Resource at 0x");
		if (pFindResult != nullptr)
		{
			intptr_t address;
			if (sscanf_s(pFindResult, "Live ID3D12Resource at 0x%llx", &address) == 1)
			{
				ID3D12Resource* resource  = reinterpret_cast<ID3D12Resource*>(address);
				Callstack<6>	callstack;
				if (D3D::GetResourceCallstack(resource, callstack))
					E_LOG(Warning, "Callstack:\n%s", callstack.ToString());
			}
		}
		break;
	}
	default:
		E_LOG(Warning, "D3D12 Validation Layer: %s", pDescription);
		break;
	}
};

GraphicsDevice::GraphicsDevice(GraphicsDeviceOptions options)
	: DeviceObject(this), m_DeleteQueue(this)
{
	if (options.LoadPIX)
	{
		if (PIXLoadLatestWinPixGpuCapturerLibrary())
		{
			E_LOG(Warning, "Dynamically loaded PIX");
		}
	}

	UINT flags = 0;
	if (options.UseDebugDevice || options.UseGPUValidation)
		flags |= DXGI_CREATE_FACTORY_DEBUG;

	VERIFY_HR(CreateDXGIFactory2(flags, IID_PPV_ARGS(m_pFactory.GetAddressOf())));

	Ref<ID3D12SDKConfiguration1> pSDKConfig;
	VERIFY_HR(D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(pSDKConfig.GetAddressOf())));

	Ref<ID3D12DeviceFactory> pDeviceFactory;
	VERIFY_HR(pSDKConfig->CreateDeviceFactory(D3D12_SDK_VERSION, "", IID_PPV_ARGS(pDeviceFactory.GetAddressOf())));
	
	if (options.UseDebugDevice || options.UseGPUValidation)
	{
		Ref<ID3D12Debug6> pDebugController;
		if(SUCCEEDED(pDeviceFactory->GetConfigurationInterface(CLSID_D3D12Debug, IID_PPV_ARGS(pDebugController.GetAddressOf()))))
		{
			pDebugController->EnableDebugLayer();
			E_LOG(Warning, "D3D12 Debug Layer Enabled");

			if (options.UseGPUValidation)
			{
				pDebugController->SetEnableGPUBasedValidation(true);
				E_LOG(Warning, "D3D12 GPU Based Validation Enabled");
			}
		}
	}

	if (options.UseDRED)
	{
		Ref<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if(SUCCEEDED(pDeviceFactory->GetConfigurationInterface(CLSID_D3D12DeviceRemovedExtendedData, IID_PPV_ARGS(pDredSettings.GetAddressOf()))))
		{
			// Turn on auto-breadcrumbs and page fault reporting.
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			E_LOG(Warning, "DRED Enabled");
		}
	}

	Ref<IDXGIAdapter4> pAdapter;
	if (!options.UseWarp)
	{
		uint32 adapterIndex = 0;
		E_LOG(Info, "Adapters:");
		DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
		while (m_pFactory->EnumAdapterByGpuPreference(adapterIndex++, gpuPreference, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) == S_OK)
		{
			DXGI_ADAPTER_DESC3 desc;
			pAdapter->GetDesc3(&desc);
			E_LOG(Info, "\t%s - %f GB", UNICODE_TO_MULTIBYTE(desc.Description), (float)desc.DedicatedVideoMemory * Math::BytesToGigaBytes);

			uint32 outputIndex = 0;
			Ref<IDXGIOutput> pOutput;
			while (pAdapter->EnumOutputs(outputIndex++, pOutput.ReleaseAndGetAddressOf()) == S_OK)
			{
				Ref<IDXGIOutput6> pOutput1;
				if (pOutput.As<IDXGIOutput6>(&pOutput1))
				{
					DXGI_OUTPUT_DESC1 outputDesc;
					pOutput1->GetDesc1(&outputDesc);

					E_LOG(Info, "\t\tMonitor %d - %dx%d - HDR: %s - %d BPP - Min Lum %f - Max Lum %f - MaxFFL %f",
						outputIndex,
						outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
						outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top,
						outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "Yes" : "No",
						outputDesc.BitsPerColor,
						outputDesc.MinLuminance,
						outputDesc.MaxLuminance,
						outputDesc.MaxFullFrameLuminance);
				}
			}
		}
		m_pFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(pAdapter.GetAddressOf()));
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);
		E_LOG(Info, "Using %s", UNICODE_TO_MULTIBYTE(desc.Description));
		VERIFY_HR(pDeviceFactory->CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_pDevice.GetAddressOf())));
	}

	if (!m_pDevice)
	{
		E_LOG(Warning, "No D3D12 Adapter selected. Falling back to WARP");
		m_pFactory->EnumWarpAdapter(IID_PPV_ARGS(pAdapter.GetAddressOf()));
		VERIFY_HR(pDeviceFactory->CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())));
	}

	if (options.UseGPUValidation)
	{
		Ref<ID3D12DebugDevice2> pDebugDevice;
		if (m_pDevice.As(&pDebugDevice))
		{
			D3D12_DEBUG_DEVICE_GPU_BASED_VALIDATION_SETTINGS validationSettings{
				.MaxMessagesPerCommandList = 256,
				.DefaultShaderPatchMode	   = D3D12_GPU_BASED_VALIDATION_SHADER_PATCH_MODE_UNGUARDED_VALIDATION,
				.PipelineStateCreateFlags  = D3D12_GPU_BASED_VALIDATION_PIPELINE_STATE_CREATE_FLAG_NONE,
			};
			pDebugDevice->SetDebugParameter(D3D12_DEBUG_DEVICE_PARAMETER_GPU_BASED_VALIDATION_SETTINGS, &validationSettings, sizeof(validationSettings));
		}
	}

	D3D::SetObjectName(m_pDevice.Get(), "Main Device");

	m_Capabilities.Initialize(this);

	if (options.UseDRED)
	{
		m_pDRED = std::make_unique<DRED>(this);
	}

	Ref<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(pInfoQueue.GetAddressOf()))))
	{
		VERIFY_HR_EX(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true), GetDevice());
		VERIFY_HR_EX(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true), GetDevice());
		VERIFY_HR_EX(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true), GetDevice());
		E_LOG(Warning, "D3D Validation Break on Severity Enabled");

		D3D12_MESSAGE_ID ignoreIDs[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
			D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
		};

		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs	= ARRAYSIZE(ignoreIDs);
		filter.DenyList.pIDList = ignoreIDs;
		pInfoQueue->AddStorageFilterEntries(&filter);

		Ref<ID3D12InfoQueue1> pInfoQueue1;
		if (pInfoQueue.As(&pInfoQueue1))
		{
			DWORD callbackCookie = 0;
			VERIFY_HR(pInfoQueue1->RegisterMessageCallback(MessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &callbackCookie));
		}
	}

	if (options.UseStablePowerState)
	{
		VERIFY_HR(m_pDevice->SetStablePowerState(TRUE));
		E_LOG(Warning, "D3D12 Enabled Stable Power State");
	}

	m_pFrameFence = new Fence(this, "Frame Fence");

	m_GraphicsQueue												= new CommandQueue(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_ComputeQueue												= new CommandQueue(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CopyQueue													= new CommandQueue(this, D3D12_COMMAND_LIST_TYPE_COPY);

	const uint64 scratchAllocatorPageSize						= 256 * Math::KilobytesToBytes;
	m_pScratchAllocationManager									= new ScratchAllocationManager(this, BufferFlag::Upload, scratchAllocatorPageSize);

	const uint64 uploadRingBufferSize							= 128 * Math::MegaBytesToBytes;
	m_pRingBufferAllocator										= new RingBufferAllocator(this, uploadRingBufferSize);

	m_pGlobalViewHeap											= new GPUDescriptorHeap(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 100000);
	m_pGlobalSamplerHeap										= new GPUDescriptorHeap(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048);

	uint8 smMaj, smMin;
	m_Capabilities.GetShaderModel(smMaj, smMin);
	E_LOG(Info, "Shader Model %d.%d", smMaj, smMin);
	m_pShaderManager = std::make_unique<ShaderManager>(smMaj, smMin);
	m_pShaderManager->AddIncludeDir("Resources/Shaders/");
}

GraphicsDevice::~GraphicsDevice()
{
	IdleGPU();

	// Disable break on validation before destroying to not make live-leak detection break each time.
	Ref<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(pInfoQueue.GetAddressOf()))))
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
	}
}

CommandContext* GraphicsDevice::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = (int)type;
	CommandContext* pContext = nullptr;
	{
		std::scoped_lock<std::mutex> lock(m_ContextAllocationMutex);
		if (m_FreeCommandLists[typeIndex].size() > 0)
		{
			pContext = m_FreeCommandLists[typeIndex].front();
			m_FreeCommandLists[typeIndex].pop();
		}
		else
		{
			Ref<ID3D12CommandList> pCommandList;
			VERIFY_HR(m_pDevice->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(pCommandList.GetAddressOf())));
			D3D::SetObjectName(pCommandList.Get(), Sprintf("Pooled %s Commandlist %d", D3D::CommandlistTypeToString(type), m_CommandListPool[typeIndex].size()).c_str());
			pContext = m_CommandListPool[typeIndex].emplace_back(new CommandContext(this, pCommandList, type, m_pScratchAllocationManager));
		}
	}
	pContext->Reset();
	return pContext;
}

void GraphicsDevice::FreeCommandList(CommandContext* pCommandList)
{
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	m_FreeCommandLists[(int)pCommandList->GetType()].push(pCommandList);
}

Ref<ID3D12CommandAllocator> GraphicsDevice::AllocateCommandAllocator(D3D12_COMMAND_LIST_TYPE type)
{
	auto CreateAllocator = [this, type]() {
		Ref<ID3D12CommandAllocator> pAllocator;
		GetParent()->GetDevice()->CreateCommandAllocator(type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
		D3D::SetObjectName(pAllocator.Get(), Sprintf("Pooled Allocator %d - %s", (int)m_CommandAllocatorPool[type].GetSize(), D3D::CommandlistTypeToString(type)).c_str());
		return pAllocator;
	};
	Ref<ID3D12CommandAllocator> pAllocator = m_CommandAllocatorPool[type].Allocate(CreateAllocator);
	pAllocator->Reset();
	return pAllocator;
}

void GraphicsDevice::FreeCommandAllocator(Ref<ID3D12CommandAllocator>& pAllocator, D3D12_COMMAND_LIST_TYPE type, const SyncPoint& syncPoint)
{
	m_CommandAllocatorPool[type].Free(std::move(pAllocator), syncPoint);
}

void GraphicsDevice::TickFrame()
{
	m_DeleteQueue.Clean();
	uint64 fenceValue = m_pFrameFence->Signal(m_GraphicsQueue);

	m_FrameFenceValues[m_FrameIndex % NUM_BUFFERS] = fenceValue;
	++m_FrameIndex;
	m_pFrameFence->CpuWait(m_FrameFenceValues[m_FrameIndex % NUM_BUFFERS]);
}

void GraphicsDevice::IdleGPU()
{
	TickFrame();
	m_pFrameFence->CpuWait(m_pFrameFence->GetLastSignaledValue());

	m_GraphicsQueue->WaitForIdle();
	m_ComputeQueue->WaitForIdle();
	m_CopyQueue->WaitForIdle();
}

void GraphicsDevice::ReleaseResourceDescriptor(DescriptorHandle& handle)
{
	if (handle.IsValid())
		m_pGlobalViewHeap->Free(handle);
}

DescriptorPtr GraphicsDevice::FindResourceDescriptorPtr(DescriptorHandle handle)
{
	gAssert(handle.IsValid());
	return m_pGlobalViewHeap->GetStartPtr().Offset(handle.HeapIndex, m_pGlobalViewHeap->GetDescriptorSize());
}


static ID3D12ResourceX* CreateD3D12Resource(ID3D12Device* pDevice, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12Heap* pHeap, uint64 offset)
{
	ID3D12ResourceX*	  pResource	 = nullptr;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
	if (pHeap)
	{
		VERIFY_HR_EX(pDevice->CreatePlacedResource(pHeap, offset, &resourceDesc, initialState, pOptimizedClearValue, IID_PPV_ARGS(&pResource)), pDevice);
	}
	else
	{
		VERIFY_HR_EX(pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState, pOptimizedClearValue, IID_PPV_ARGS(&pResource)), pDevice);
	}
	D3D::SetResourceCallstack(pResource);
	return pResource;
}


Ref<Texture> GraphicsDevice::CreateTexture(const TextureDesc& desc, const char* pName, Span<D3D12_SUBRESOURCE_DATA> initData)
{
	return CreateTexture(desc, nullptr, 0, pName, initData);
}

Ref<Texture> GraphicsDevice::CreateTexture(const TextureDesc& desc, ID3D12Heap* pHeap, uint64 offset, const char* pName, Span<D3D12_SUBRESOURCE_DATA> initData)
{
	D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
	gAssert(EnumHasAllFlags(desc.Flags, TextureFlag::RenderTarget | TextureFlag::DepthStencil) == false);

	D3D12_CLEAR_VALUE* pClearValue = nullptr;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = D3D::ConvertFormat(desc.Format);

	if (EnumHasAnyFlags(desc.Flags, TextureFlag::RenderTarget))
	{
		gAssert(desc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::Color);
		memcpy(&clearValue.Color, &desc.ClearBindingValue.Color, sizeof(Color));
		resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		pClearValue = &clearValue;
	}
	if (EnumHasAnyFlags(desc.Flags, TextureFlag::DepthStencil))
	{
		gAssert(desc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		clearValue.DepthStencil.Depth = desc.ClearBindingValue.DepthStencil.Depth;
		clearValue.DepthStencil.Stencil = desc.ClearBindingValue.DepthStencil.Stencil;
		resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		pClearValue = &clearValue;
	}

	D3D12_RESOURCE_DESC resourceDesc = D3D::GetResourceDesc(desc);
	ID3D12ResourceX* pResource = CreateD3D12Resource(m_pDevice, resourceDesc, D3D12_HEAP_TYPE_DEFAULT, resourceState, pClearValue, pHeap, offset);
	Texture* pTexture = new Texture(this, desc, pResource);
	pTexture->SetName(pName);

	if (initData.GetSize() > 0)
	{
		uint64 requiredSize = 0;
		Array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(initData.GetSize());
		Array<uint32> numRows(initData.GetSize());
		Array<uint64> rowSizes(initData.GetSize());
		m_pDevice->GetCopyableFootprints(&resourceDesc, 0, initData.GetSize(), 0, layouts.data(), numRows.data(), rowSizes.data(), &requiredSize);
		RingBufferAllocation allocation;
		m_pRingBufferAllocator->Allocate((uint32)requiredSize, allocation);

		for (uint32 subResource = 0; subResource < initData.GetSize(); ++subResource)
		{
			const D3D12_SUBRESOURCE_DATA& srcData = initData[subResource];
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT& dstLayout = layouts[subResource];

			D3D12_MEMCPY_DEST dest =
			{
				.pData = (char*)allocation.pMappedMemory + dstLayout.Offset,
				.RowPitch = dstLayout.Footprint.RowPitch,
				.SlicePitch = (uint64)dstLayout.Footprint.RowPitch * numRows[subResource]
			};

			for (uint32 z = 0; z < dstLayout.Footprint.Depth; ++z)
			{
				char* pDest = (char*)dest.pData + dest.SlicePitch * z;
				const char* pSrc = (char*)srcData.pData + srcData.SlicePitch * z;
				for (uint32 y = 0; y < numRows[subResource]; ++y)
				{
					memcpy(pDest + y * dest.RowPitch, pSrc + y * srcData.RowPitch, rowSizes[subResource]);
				}
			}

			dstLayout.Offset += allocation.Offset;

			const CD3DX12_TEXTURE_COPY_LOCATION dst(pTexture->GetResource(), subResource);
			const CD3DX12_TEXTURE_COPY_LOCATION src(allocation.pBackingResource->GetResource(), dstLayout);
			allocation.pContext->GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}

		m_pRingBufferAllocator->Free(allocation);
	}

	if (EnumHasAnyFlags(desc.Flags, TextureFlag::ShaderResource))
	{
		pTexture->m_SRV = CreateSRV(pTexture, TextureSRVDesc(0, (uint8)pTexture->GetMipLevels()));
	}
	if (EnumHasAnyFlags(desc.Flags, TextureFlag::UnorderedAccess))
	{
		pTexture->m_pResourceState = std::make_unique<ResourceState>();

		pTexture->m_UAVs.resize(desc.Mips);
		for (uint8 mip = 0; mip < desc.Mips; ++mip)
			pTexture->m_UAVs[mip] = CreateUAV(pTexture, TextureUAVDesc(mip));
	}
	if (EnumHasAnyFlags(desc.Flags, TextureFlag::RenderTarget))
	{
		pTexture->m_pResourceState = std::make_unique<ResourceState>();
	}
	else if (EnumHasAnyFlags(desc.Flags, TextureFlag::DepthStencil))
	{
		pTexture->m_pResourceState = std::make_unique<ResourceState>();
	}

	if(pTexture->UseStateTracking())
		pTexture->SetResourceState(resourceState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	return pTexture;
}

Ref<Texture> GraphicsDevice::CreateTextureForSwapchain(ID3D12ResourceX* pSwapchainResource, uint32 index)
{
	D3D12_RESOURCE_DESC resourceDesc = pSwapchainResource->GetDesc();
	TextureDesc			desc{
				.Width			   = (uint32)resourceDesc.Width,
				.Height			   = (uint32)resourceDesc.Height,
				.Mips			   = resourceDesc.MipLevels,
				.SampleCount	   = resourceDesc.SampleDesc.Count,
				.Format			   = ResourceFormat::Unknown,
				.Flags			   = TextureFlag::RenderTarget,
				.ClearBindingValue = ClearBinding(Colors::Black),
	};

	Texture* pTexture = new Texture(this, desc, pSwapchainResource);
	pTexture->SetName(Sprintf("Backbuffer %d", index).c_str());
	pTexture->m_pResourceState = std::make_unique<ResourceState>();
	pTexture->SetResourceState(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	pTexture->m_SRV = CreateSRV(pTexture, TextureSRVDesc(0, 1));
	return pTexture;
}



Ref<Buffer> GraphicsDevice::CreateBuffer(const BufferDesc& desc, ID3D12Heap* pHeap, uint64 offset, const char* pName, const void* pInitData)
{
	D3D12_RESOURCE_DESC	  resourceDesc = D3D::GetResourceDesc(desc);
	D3D12_HEAP_TYPE		  heapType	   = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN;

	if (EnumHasAnyFlags(desc.Flags, BufferFlag::Readback))
	{
		gAssert(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_COPY_DEST;
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	if (EnumHasAnyFlags(desc.Flags, BufferFlag::Upload))
	{
		gAssert(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		heapType = D3D12_HEAP_TYPE_UPLOAD;
	}
	if (EnumHasAnyFlags(desc.Flags, BufferFlag::AccelerationStructure))
	{
		gAssert(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	if (initialState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		initialState = D3D12_RESOURCE_STATE_COMMON;
	}

	ID3D12ResourceX* pResource = CreateD3D12Resource(m_pDevice, resourceDesc, heapType, initialState, nullptr, pHeap, offset);
	Buffer* pBuffer = new Buffer(this, desc, pResource);
	pBuffer->SetName(pName);

	if (EnumHasAnyFlags(desc.Flags, BufferFlag::Upload | BufferFlag::Readback))
	{
		VERIFY_HR(pResource->Map(0, nullptr, &pBuffer->m_pMappedData));
		pBuffer->m_pResourceState = std::make_unique<ResourceState>();
	}

	bool isRaw = EnumHasAnyFlags(desc.Flags, BufferFlag::ByteAddress);
	bool withCounter = !isRaw && desc.Format == ResourceFormat::Unknown;

	//#todo: Temp code. Pull out views from buffer
	if (EnumHasAnyFlags(desc.Flags, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure))
	{
		pBuffer->m_SRV = CreateSRV(pBuffer, BufferSRVDesc(desc.Format, isRaw));
	}
	if (EnumHasAnyFlags(desc.Flags, BufferFlag::UnorderedAccess))
	{
		pBuffer->m_UAV = CreateUAV(pBuffer, BufferUAVDesc(desc.Format, isRaw, withCounter));
		pBuffer->m_pResourceState = std::make_unique<ResourceState>();
	}

	if (pInitData)
	{
		if (EnumHasAllFlags(desc.Flags, BufferFlag::Upload))
		{
			memcpy((char*)pBuffer->GetMappedData(), pInitData, desc.Size);
		}
		else
		{
			RingBufferAllocation allocation;
			m_pRingBufferAllocator->Allocate((uint32)desc.Size, allocation);
			memcpy((char*)allocation.pMappedMemory, pInitData, desc.Size);
			allocation.pContext->CopyBuffer(allocation.pBackingResource, pBuffer, desc.Size, allocation.Offset, 0);
			m_pRingBufferAllocator->Free(allocation);
		}
	}

	if(pBuffer->UseStateTracking())
		pBuffer->SetResourceState(initialState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	return pBuffer;
}

Ref<Buffer> GraphicsDevice::CreateBuffer(const BufferDesc& desc, const char* pName, const void* pInitData)
{
	return CreateBuffer(desc, nullptr, 0, pName, pInitData);
}

void GraphicsDevice::DeferReleaseObject(ID3D12Object* pObject)
{
	if (pObject)
	{
		m_DeleteQueue.EnqueueResource(pObject, GetFrameFence());
	}
}

Ref<PipelineState> GraphicsDevice::CreateComputePipeline(RootSignature* pRootSignature, const char* pShaderPath, const char* entryPoint, Span<ShaderDefine> defines)
{
	PipelineStateInitializer desc;
	desc.SetRootSignature(pRootSignature);
	desc.SetComputeShader(pShaderPath, entryPoint, defines.Copy());
	desc.SetName(Sprintf("%s:%s", pShaderPath, entryPoint).c_str());
	return CreatePipeline(desc);
}

Ref<PipelineState> GraphicsDevice::CreatePipeline(const PipelineStateInitializer& psoDesc)
{
	Ref<PipelineState> pPSO = new PipelineState(this, psoDesc);
	if (CommandLine::GetBool("immediate_pso"))
		pPSO->CreateInternal();
	return pPSO;
}

Ref<StateObject> GraphicsDevice::CreateStateObject(const StateObjectInitializer& stateDesc)
{
	Ref<StateObject> pSO = new StateObject(this, stateDesc);
	if (CommandLine::GetBool("immediate_pso"))
		pSO->ConditionallyReload();
	return pSO;
}

BufferView GraphicsDevice::CreateSRV(const Buffer* pBuffer, const BufferSRVDesc& desc)
{
	gAssert(pBuffer);
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	DescriptorPtr descriptor = m_pGlobalViewHeap->Allocate(pBuffer);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::AccelerationStructure))
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.RaytracingAccelerationStructure.Location = pBuffer->GetGPUAddress();

		m_pDevice->CreateShaderResourceView(nullptr, &srvDesc, descriptor.CPUOpaqueHandle);
	}
	else
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		if (desc.Raw)
		{
			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.Buffer.StructureByteStride = 0;
			srvDesc.Buffer.FirstElement = desc.ElementOffset / 4;
			srvDesc.Buffer.NumElements = desc.NumElements > 0 ? desc.NumElements / 4 : (uint32)(bufferDesc.Size / 4);
			srvDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
		}
		else
		{
			srvDesc.Format = D3D::ConvertFormat(desc.Format);
			srvDesc.Buffer.StructureByteStride = desc.Format == ResourceFormat::Unknown ? bufferDesc.ElementSize : 0;
			srvDesc.Buffer.FirstElement = desc.ElementOffset;
			srvDesc.Buffer.NumElements = desc.NumElements > 0 ? desc.NumElements : bufferDesc.NumElements();
		}

		m_pDevice->CreateShaderResourceView(pBuffer->GetResource(), &srvDesc, descriptor.CPUOpaqueHandle);
	}

	m_pDevice->CopyDescriptorsSimple(1, descriptor.CPUHandle, descriptor.CPUOpaqueHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return BufferView(descriptor);
}

RWBufferView GraphicsDevice::CreateUAV(const Buffer* pBuffer, const BufferUAVDesc& desc)
{
	gAssert(pBuffer);
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = D3D::ConvertFormat(desc.Format);
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = bufferDesc.NumElements();
	uavDesc.Buffer.StructureByteStride = 0;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (desc.Raw)
	{
		uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		uavDesc.Buffer.NumElements *= bufferDesc.ElementSize / 4;
		uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	}
	else
	{
		uavDesc.Buffer.StructureByteStride = uavDesc.Format == DXGI_FORMAT_UNKNOWN ? bufferDesc.ElementSize : 0;
	}

	DescriptorPtr descriptor = m_pGlobalViewHeap->Allocate(pBuffer);
	m_pDevice->CreateUnorderedAccessView(pBuffer->GetResource(), nullptr, &uavDesc, descriptor.CPUOpaqueHandle);
	m_pDevice->CopyDescriptorsSimple(1, descriptor.CPUHandle, descriptor.CPUOpaqueHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return RWBufferView(descriptor);
}

TextureView GraphicsDevice::CreateSRV(const Texture* pTexture, const TextureSRVDesc& desc)
{
	gAssert(pTexture);
	const TextureDesc& textureDesc = pTexture->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	auto SRVFormatFromDepth = [](ResourceFormat format)
	{
		switch (format)
		{
		case ResourceFormat::D32S8:			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		case ResourceFormat::D32_FLOAT:		return DXGI_FORMAT_R32_FLOAT;
		case ResourceFormat::D24S8:			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case ResourceFormat::D16_UNORM:		return DXGI_FORMAT_R16_UNORM;
		default:							return D3D::ConvertFormat(format);
		}
	};

	srvDesc.Format = D3D::GetFormatSRGB(SRVFormatFromDepth(textureDesc.Format), EnumHasAllFlags(textureDesc.Flags, TextureFlag::sRGB));

	switch (textureDesc.Type)
	{
	case TextureType::Texture1D:
		srvDesc.Texture1D.MipLevels					= desc.NumMipLevels;
		srvDesc.Texture1D.MostDetailedMip			= desc.MipLevel;
		srvDesc.Texture1D.ResourceMinLODClamp		= 0;
		srvDesc.ViewDimension						= D3D12_SRV_DIMENSION_TEXTURE1D;
		break;
	case TextureType::Texture1DArray:
		srvDesc.Texture1DArray.ArraySize			= textureDesc.ArraySize;
		srvDesc.Texture1DArray.FirstArraySlice		= 0;
		srvDesc.Texture1DArray.MipLevels			= desc.NumMipLevels;
		srvDesc.Texture1DArray.MostDetailedMip		= desc.MipLevel;
		srvDesc.Texture1DArray.ResourceMinLODClamp	= 0;
		srvDesc.ViewDimension						= D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureType::Texture2D:
		srvDesc.Texture2D.MipLevels					= desc.NumMipLevels;
		srvDesc.Texture2D.MostDetailedMip			= desc.MipLevel;
		srvDesc.Texture2D.PlaneSlice				= 0;
		srvDesc.Texture2D.ResourceMinLODClamp		= 0;
		srvDesc.ViewDimension						= textureDesc.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
		break;
	case TextureType::Texture2DArray:
		srvDesc.Texture2DArray.MipLevels			= desc.NumMipLevels;
		srvDesc.Texture2DArray.MostDetailedMip		= desc.MipLevel;
		srvDesc.Texture2DArray.PlaneSlice			= 0;
		srvDesc.Texture2DArray.ResourceMinLODClamp	= 0;
		srvDesc.Texture2DArray.ArraySize			= textureDesc.ArraySize;
		srvDesc.Texture2DArray.FirstArraySlice		= 0;
		srvDesc.ViewDimension						= textureDesc.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureType::Texture3D:
		srvDesc.Texture3D.MipLevels					= desc.NumMipLevels;
		srvDesc.Texture3D.MostDetailedMip			= desc.MipLevel;
		srvDesc.Texture3D.ResourceMinLODClamp		= 0;
		srvDesc.ViewDimension						= D3D12_SRV_DIMENSION_TEXTURE3D;
		break;
	case TextureType::TextureCube:
		srvDesc.TextureCube.MipLevels				= desc.NumMipLevels;
		srvDesc.TextureCube.MostDetailedMip			= desc.MipLevel;
		srvDesc.TextureCube.ResourceMinLODClamp		= 0;
		srvDesc.ViewDimension						= D3D12_SRV_DIMENSION_TEXTURECUBE;
		break;
	case TextureType::TextureCubeArray:
		srvDesc.TextureCubeArray.MipLevels			= desc.NumMipLevels;
		srvDesc.TextureCubeArray.MostDetailedMip	= desc.MipLevel;
		srvDesc.TextureCubeArray.ResourceMinLODClamp= 0;
		srvDesc.TextureCubeArray.First2DArrayFace	= 0;
		srvDesc.TextureCubeArray.NumCubes			= textureDesc.ArraySize;
		srvDesc.ViewDimension						= D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		break;
	default:
		break;
	}

	DescriptorPtr descriptor = m_pGlobalViewHeap->Allocate(pTexture);
	m_pDevice->CreateShaderResourceView(pTexture->GetResource(), &srvDesc, descriptor.CPUOpaqueHandle);
	m_pDevice->CopyDescriptorsSimple(1, descriptor.CPUHandle, descriptor.CPUOpaqueHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return TextureView(descriptor);
}

RWTextureView GraphicsDevice::CreateUAV(const Texture* pTexture, const TextureUAVDesc& desc)
{
	gAssert(pTexture);
	const TextureDesc& textureDesc = pTexture->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	switch (textureDesc.Type)
	{
	case TextureType::Texture1D:
		uavDesc.Texture1D.MipSlice				= desc.MipLevel;
		uavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE1D;
		break;
	case TextureType::Texture1DArray:
		uavDesc.Texture1DArray.MipSlice			= desc.MipLevel;
		uavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureType::Texture2D:
		uavDesc.Texture2D.MipSlice				= desc.MipLevel;
		uavDesc.Texture2D.PlaneSlice			= 0;
		uavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE2D;
		break;
	case TextureType::Texture2DArray:
		uavDesc.Texture2DArray.MipSlice			= desc.MipLevel;
		uavDesc.Texture2DArray.ArraySize		= textureDesc.ArraySize;
		uavDesc.Texture2DArray.FirstArraySlice	= 0;
		uavDesc.Texture2DArray.PlaneSlice		= 0;
		uavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureType::Texture3D:
		uavDesc.Texture3D.MipSlice				= desc.MipLevel;
		uavDesc.Texture3D.FirstWSlice			= 0;
		uavDesc.Texture3D.WSize					= 0xFFFFFFFF;
		uavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE3D;
		break;
	case TextureType::TextureCube:
	case TextureType::TextureCubeArray:
		uavDesc.Texture2DArray.ArraySize		= textureDesc.ArraySize * 6;
		uavDesc.Texture2DArray.FirstArraySlice	= 0;
		uavDesc.Texture2DArray.PlaneSlice		= 0;
		uavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		break;
	default:
		break;
	}
	uavDesc.Format = D3D::ConvertFormat(pTexture->GetFormat());

	DescriptorPtr descriptor = m_pGlobalViewHeap->Allocate(pTexture);
	m_pDevice->CreateUnorderedAccessView(pTexture->GetResource(), nullptr, &uavDesc, descriptor.CPUOpaqueHandle);
	m_pDevice->CopyDescriptorsSimple(1, descriptor.CPUHandle, descriptor.CPUOpaqueHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return RWTextureView(descriptor);
}

Ref<CommandSignature> GraphicsDevice::CreateCommandSignature(const CommandSignatureInitializer& signatureDesc, const char* pName, RootSignature* pRootSignature)
{
	Ref<ID3D12CommandSignature> pCmdSignature;
	D3D12_COMMAND_SIGNATURE_DESC desc = signatureDesc.GetDesc();
	VERIFY_HR_EX(GetParent()->GetDevice()->CreateCommandSignature(&desc, pRootSignature ? pRootSignature->GetRootSignature() : nullptr, IID_PPV_ARGS(pCmdSignature.GetAddressOf())), m_pDevice);
	D3D::SetObjectName(pCmdSignature.Get(), pName);
	return new CommandSignature(this, pCmdSignature);
}

ShaderResult GraphicsDevice::GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, Span<ShaderDefine> defines /*= {}*/)
{
	return m_pShaderManager->GetShader(pShaderPath, shaderType, pEntryPoint, defines);
}

ShaderResult GraphicsDevice::GetLibrary(const char* pShaderPath, Span<ShaderDefine> defines /*= {}*/)
{
	return m_pShaderManager->GetShader(pShaderPath, ShaderType::MAX, nullptr, defines);
}

GraphicsDevice::DeferredDeleteQueue::DeferredDeleteQueue(GraphicsDevice* pParent)
	: DeviceObject(pParent)
{
}

GraphicsDevice::DeferredDeleteQueue::~DeferredDeleteQueue()
{
	GetParent()->IdleGPU();
	Clean();
	gAssert(m_DeletionQueue.empty());
}

void GraphicsDevice::DeferredDeleteQueue::EnqueueResource(ID3D12Object* pResource, Fence* pFence)
{
	std::scoped_lock lock(m_QueueCS);
	FencedObject object;
	object.pFence = pFence;
	object.FenceValue = pFence->GetCurrentValue();
	object.pResource = pResource;
	m_DeletionQueue.push(object);
}

void GraphicsDevice::DeferredDeleteQueue::Clean()
{
	std::scoped_lock lock(m_QueueCS);
	while (!m_DeletionQueue.empty())
	{
		const FencedObject& p = m_DeletionQueue.front();
		if (!p.pFence->IsComplete(p.FenceValue))
		{
			break;
		}
		gVerify(p.pResource->Release(), == 0);
		m_DeletionQueue.pop();
	}
}

void GraphicsCapabilities::Initialize(GraphicsDevice* pDevice)
{
	m_pDevice = pDevice;

	VERIFY_HR(m_FeatureSupport.Init(pDevice->GetDevice()));
	gAssert(m_FeatureSupport.ResourceBindingTier() >= D3D12_RESOURCE_BINDING_TIER_3, "Device does not support Resource Binding Tier 3 or higher. Tier 2 and under is not supported.");
	gAssert(m_FeatureSupport.HighestShaderModel() >= D3D_SHADER_MODEL_6_6, "Device does not support SM 6.6 which is required for dynamic indexing");
	gAssert(m_FeatureSupport.WaveOps(), "Device does not support wave ops which is required.");

	RenderPassTier = m_FeatureSupport.RenderPassesTier();
	RayTracingTier = m_FeatureSupport.RaytracingTier();
	VRSTier = m_FeatureSupport.VariableShadingRateTier();
	VRSTileSize = m_FeatureSupport.ShadingRateImageTileSize();
	MeshShaderSupport = m_FeatureSupport.MeshShaderTier();
	SamplerFeedbackSupport = m_FeatureSupport.SamplerFeedbackTier();
	ShaderModel = (uint16)m_FeatureSupport.HighestShaderModel();
}

bool GraphicsCapabilities::CheckUAVSupport(DXGI_FORMAT format) const
{
	switch (format)
	{
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
		// Unconditionally supported.
		return true;

	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SINT:
		// All these are supported if this optional feature is set.
		return m_FeatureSupport.TypedUAVLoadAdditionalFormats();

	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:

		// Conditionally supported by specific pDevices.
		if (m_FeatureSupport.TypedUAVLoadAdditionalFormats())
		{
			D3D12_FORMAT_SUPPORT1 f1 = D3D12_FORMAT_SUPPORT1_NONE;
			D3D12_FORMAT_SUPPORT2 f2 = D3D12_FORMAT_SUPPORT2_NONE;
			VERIFY_HR(m_FeatureSupport.FormatSupport(format, f1, f2));
			const DWORD mask = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
			return ((f2 & mask) == mask);
		}
		return false;

	default:
		return false;
	}
}

DXGI_COLOR_SPACE_TYPE GetColorSpace(DisplayMode displayMode)
{
	switch (displayMode)
	{
	default:
	case DisplayMode::SDR:			return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	case DisplayMode::HDR_PQ:		return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	case DisplayMode::HDR_scRGB:	return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
	}
}

ResourceFormat GetSwapchainFormat(DisplayMode displayMode)
{
	switch (displayMode)
	{
	default:
	case DisplayMode::SDR:			return ResourceFormat::RGBA8_UNORM;
	case DisplayMode::HDR_PQ:		return ResourceFormat::RGB10A2_UNORM;
	case DisplayMode::HDR_scRGB:	return ResourceFormat::RGBA16_FLOAT;
	}
}

SwapChain::SwapChain(GraphicsDevice* pDevice, DisplayMode displayMode, uint32 numFrames, WindowHandle pNativeWindow)
	: DeviceObject(pDevice), m_Window(pNativeWindow), m_DesiredDisplayMode(displayMode), m_Format(GetSwapchainFormat(displayMode)), m_CurrentImage(0), m_NumFrames(numFrames)
{
	m_pPresentFence = new Fence(pDevice, "Present Fence");

	RecreateSwapChain();
}

SwapChain::~SwapChain()
{
	m_pPresentFence->CpuWait();
	m_pSwapchain->SetFullscreenState(false, nullptr);

	ReleaseBackbuffers();
}

void SwapChain::OnResizeOrMove(uint32 width, uint32 height)
{
	DisplayMode desiredDisplayMode = m_DesiredDisplayMode;
	if (!DisplaySupportsHDR())
		desiredDisplayMode = DisplayMode::SDR;

	ResourceFormat desiredFormat = GetSwapchainFormat(desiredDisplayMode);
	if (desiredFormat != m_Format || width != m_Width || height != m_Height)
	{
		m_Width = width;
		m_Height = height;
		m_Format = desiredFormat;

		m_pPresentFence->CpuWait();

		ReleaseBackbuffers();

		//Resize the buffers
		DXGI_SWAP_CHAIN_DESC1 desc{};
		m_pSwapchain->GetDesc1(&desc);

		VERIFY_HR(m_pSwapchain->ResizeBuffers(
			(uint32)m_Backbuffers.size(),
			width,
			height,
			D3D::ConvertFormat(m_Format),
			desc.Flags
		));

		UINT colorSpaceSupport = 0;
		DXGI_COLOR_SPACE_TYPE colorSpace = GetColorSpace(desiredDisplayMode);
		if (SUCCEEDED(m_pSwapchain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport)) &&
			(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
		{
			VERIFY_HR(m_pSwapchain->SetColorSpace1(colorSpace));
		}

		//Recreate the render target views
		for (uint32 i = 0; i < (uint32)m_Backbuffers.size(); ++i)
		{
			ID3D12ResourceX* pResource = nullptr;
			VERIFY_HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
			m_Backbuffers[i] = GetParent()->CreateTextureForSwapchain(pResource, i);
		}

		m_CurrentImage = m_pSwapchain->GetCurrentBackBufferIndex();
	}
}

void SwapChain::Present()
{
	m_pSwapchain->Present(m_Vsync ? 1 : 0, !m_Vsync && m_AllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
	m_CurrentImage = m_pSwapchain->GetCurrentBackBufferIndex();

	// Signal and store when the GPU work for the frame we just flipped is finished.
	CommandQueue* pDirectQueue = GetParent()->GetGraphicsQueue();
	m_pPresentFence->Signal(pDirectQueue);

	WaitForSingleObject(m_WaitableObject, INFINITE);
}

void SwapChain::SetNumFrames(uint32 numFrames)
{
	m_NumFrames = numFrames;
	RecreateSwapChain();
}

void SwapChain::SetMaxFrameLatency(uint32 maxFrameLatency)
{
	m_MaxFrameLatency = maxFrameLatency;
	if (m_UseWaitableObject)
		m_pSwapchain->SetMaximumFrameLatency(maxFrameLatency);
}

void SwapChain::SetUseWaitableSwapChain(bool enabled)
{
	if (m_UseWaitableObject != enabled)
	{
		m_UseWaitableObject = enabled;
		RecreateSwapChain();
	}
}

bool SwapChain::DisplaySupportsHDR() const
{
	Ref<IDXGIOutput> pOutput;
	Ref<IDXGIOutput6> pOutput6;
	if (SUCCEEDED(m_pSwapchain->GetContainingOutput(pOutput.GetAddressOf())))
	{
		if (SUCCEEDED(pOutput->QueryInterface(pOutput6.GetAddressOf())))
		{
			DXGI_OUTPUT_DESC1 desc;
			if (SUCCEEDED(pOutput6->GetDesc1(&desc)))
				return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		}
	}
	return false;
}

Vector2i SwapChain::GetViewport() const
{
	Texture* pTexture = GetBackBuffer();
	return Vector2i(pTexture->GetWidth(), pTexture->GetHeight());
}

void SwapChain::ReleaseBackbuffers()
{
	for (Ref<Texture>& pTexture : m_Backbuffers)
	{
		if (pTexture)
			pTexture->ReleaseImmediate();
		pTexture.Reset();
	}
}

void SwapChain::RecreateSwapChain()
{
	m_pPresentFence->CpuWait();

	GraphicsDevice* pDevice = GetParent();

	DXGI_SWAP_CHAIN_DESC1 desc{};
	BOOL allowTearing = FALSE;
	if (SUCCEEDED(pDevice->GetFactory()->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(BOOL))))
	{
		m_AllowTearing = allowTearing;
		desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	if (m_UseWaitableObject)
		desc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.BufferCount = m_NumFrames;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.Format = D3D::ConvertFormat(m_Format);
	desc.Width = 0;
	desc.Height = 0;
	desc.Scaling = DXGI_SCALING_NONE;
	desc.Stereo = FALSE;
	// The compositor can use DirectFlip, where it uses the application's back buffer as the entire display back buffer.
	// With DXGI_SWAP_EFFECT_FLIP_DISCARD, the compositor can _could_ still perform this optimization, by drawing other content onto the application's back buffer.
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fsDesc.Windowed = true;

	m_Backbuffers.clear();
	m_Backbuffers.resize(m_NumFrames);
	m_pSwapchain.Reset();

	CommandQueue* pPresentQueue = pDevice->GetGraphicsQueue();
	Ref<IDXGISwapChain1> swapChain;

	VERIFY_HR(pDevice->GetFactory()->CreateSwapChainForHwnd(
		pPresentQueue->GetCommandQueue(),
		(HWND)m_Window,
		&desc,
		&fsDesc,
		nullptr,
		swapChain.GetAddressOf()));

	swapChain.As(&m_pSwapchain);

	if (m_WaitableObject)
	{
		CloseHandle(m_WaitableObject);
		m_WaitableObject = nullptr;
	}

	if (m_UseWaitableObject)
	{
		m_pSwapchain->SetMaximumFrameLatency(m_MaxFrameLatency);
		m_WaitableObject = m_pSwapchain->GetFrameLatencyWaitableObject();
	}

	m_Width = 0;
	m_Height = 0;

	DXGI_SWAP_CHAIN_DESC1 descActual{};
	m_pSwapchain->GetDesc1(&descActual);
	OnResizeOrMove(descActual.Width, descActual.Height);
}
