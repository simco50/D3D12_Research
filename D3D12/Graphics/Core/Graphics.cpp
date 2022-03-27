#include "stdafx.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "OfflineDescriptorAllocator.h"
#include "GraphicsResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "DynamicResourceAllocator.h"
#include "Texture.h"
#include "ResourceViews.h"
#include "Buffer.h"
#include "StateObject.h"
#include "Core/CommandLine.h"
#include "pix3.h"
#include "Content/Image.h"

// Setup the Agility D3D12 SDK
extern "C" { _declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION; }
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

namespace GraphicsCommon
{
	static std::array<RefCountPtr<Texture>, (uint32)DefaultTexture::MAX> DefaultTextures;

	CommandSignature* pIndirectDrawSignature = nullptr;
	CommandSignature* pIndirectDispatchSignature = nullptr;
	CommandSignature* pIndirectDispatchMeshSignature = nullptr;

	void Create(GraphicsDevice* pDevice)
	{
		CommandContext& context = *pDevice->AllocateCommandContext();

		auto RegisterDefaultTexture = [pDevice, &context](DefaultTexture type, const char* pName, const TextureDesc& desc, uint32* pData)
		{
			RefCountPtr<Texture> pTexture = pDevice->CreateTexture(desc, pName);
			D3D12_SUBRESOURCE_DATA data;
			data.pData = pData;
			data.RowPitch = D3D::GetFormatRowDataSize(desc.Format, desc.Width);
			data.SlicePitch = data.RowPitch * desc.Width;
			context.InitializeTexture(pTexture, &data, 0, 1);
			DefaultTextures[(int)type] = pTexture;
		};

		uint32 BLACK = 0xFF000000;
		RegisterDefaultTexture(DefaultTexture::Black2D, "Default Black", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &BLACK);
		uint32 WHITE = 0xFFFFFFFF;
		RegisterDefaultTexture(DefaultTexture::White2D, "Default White", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &WHITE);
		uint32 MAGENTA = 0xFFFF00FF;
		RegisterDefaultTexture(DefaultTexture::Magenta2D, "Default Magenta", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &MAGENTA);
		uint32 GRAY = 0xFF808080;
		RegisterDefaultTexture(DefaultTexture::Gray2D, "Default Gray", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &GRAY);
		uint32 DEFAULT_NORMAL = 0xFFFF8080;
		RegisterDefaultTexture(DefaultTexture::Normal2D, "Default Normal", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_NORMAL);
		uint32 DEFAULT_ROUGHNESS_METALNESS = 0xFFFF80FF;
		RegisterDefaultTexture(DefaultTexture::RoughnessMetalness, "Default Roughness/Metalness", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_ROUGHNESS_METALNESS);

		uint32 BLACK_CUBE[6] = {};
		RegisterDefaultTexture(DefaultTexture::BlackCube, "Default Black Cube", TextureDesc::CreateCube(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), BLACK_CUBE);

		RegisterDefaultTexture(DefaultTexture::Black3D, "Default Black 3D", TextureDesc::Create3D(1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &BLACK);

		DefaultTextures[(int)DefaultTexture::ColorNoise256] = pDevice->CreateTextureFromFile(context, "Resources/Textures/Noise.png", false, "Noise");
		DefaultTextures[(int)DefaultTexture::BlueNoise512] = pDevice->CreateTextureFromFile(context, "Resources/Textures/BlueNoise.dds", false, "Blue Noise");

		context.Execute(true);

		pIndirectDispatchSignature = new CommandSignature(pDevice);
		pIndirectDispatchSignature->AddDispatch();
		pIndirectDispatchSignature->Finalize("Default Indirect Dispatch");

		pIndirectDrawSignature = new CommandSignature(pDevice);
		pIndirectDrawSignature->AddDraw();
		pIndirectDrawSignature->Finalize("Default Indirect Draw");

		pIndirectDispatchMeshSignature = new CommandSignature(pDevice);
		pIndirectDispatchMeshSignature->AddDispatchMesh();
		pIndirectDispatchMeshSignature->Finalize("Default Indirect Dispatch Mesh");
	}

	void Destroy()
	{
		for (auto& pTexture : DefaultTextures)
		{
			pTexture.Reset();
		}

		delete pIndirectDispatchSignature;
		delete pIndirectDrawSignature;
		delete pIndirectDispatchMeshSignature;
	}

	Texture* GetDefaultTexture(DefaultTexture type)
	{
		return DefaultTextures[(int)type];
	}
}

GraphicsInstance GraphicsInstance::CreateInstance(GraphicsInstanceFlags createFlags /*= GraphicsFlags::None*/)
{
	return GraphicsInstance(createFlags);
}

GraphicsInstance::GraphicsInstance(GraphicsInstanceFlags createFlags)
{
	UINT flags = 0;
	if (EnumHasAnyFlags(createFlags, GraphicsInstanceFlags::DebugDevice))
	{
		flags |= DXGI_CREATE_FACTORY_DEBUG;
	}
	VERIFY_HR(CreateDXGIFactory2(flags, IID_PPV_ARGS(m_pFactory.GetAddressOf())));
	BOOL allowTearing;
	if (SUCCEEDED(m_pFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(BOOL))))
	{
		m_AllowTearing = allowTearing;
	}

	if (EnumHasAnyFlags(createFlags, GraphicsInstanceFlags::DebugDevice))
	{
		RefCountPtr<ID3D12Debug> pDebugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
		{
			pDebugController->EnableDebugLayer();
			E_LOG(Warning, "D3D12 Debug Layer Enabled");
		}
	}

	if (EnumHasAnyFlags(createFlags, GraphicsInstanceFlags::DRED))
	{
		RefCountPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings))))
		{
			// Turn on auto-breadcrumbs and page fault reporting.
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			E_LOG(Warning, "DRED Enabled");
		}
	}

	if (EnumHasAnyFlags(createFlags, GraphicsInstanceFlags::GpuValidation))
	{
		RefCountPtr<ID3D12Debug1> pDebugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
		{
			pDebugController->SetEnableGPUBasedValidation(true);
			E_LOG(Warning, "D3D12 GPU Based Validation Enabled");
		}
	}

	if (EnumHasAnyFlags(createFlags, GraphicsInstanceFlags::Pix))
	{
		if(PIXLoadLatestWinPixGpuCapturerLibrary())
		{
			E_LOG(Warning, "Dynamically loaded PIX");
		}
	}
}

RefCountPtr<SwapChain> GraphicsInstance::CreateSwapchain(GraphicsDevice* pDevice, WindowHandle pNativeWindow, uint32 width, uint32 height, uint32 numFrames, bool vsync)
{
	return new SwapChain(pDevice, m_pFactory.Get(), pNativeWindow, width, height, numFrames, vsync);
}

RefCountPtr<IDXGIAdapter4> GraphicsInstance::EnumerateAdapter(bool useWarp)
{
	RefCountPtr<IDXGIAdapter4> pAdapter;
	RefCountPtr<ID3D12Device> pDevice;
	if (!useWarp)
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
			RefCountPtr<IDXGIOutput> pOutput;
			while (pAdapter->EnumOutputs(outputIndex++, pOutput.ReleaseAndGetAddressOf()) == S_OK)
			{
				RefCountPtr<IDXGIOutput6> pOutput1;
				pOutput->QueryInterface(&pOutput1);
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
		m_pFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(pAdapter.GetAddressOf()));
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);
		E_LOG(Info, "Using %s", UNICODE_TO_MULTIBYTE(desc.Description));

		constexpr D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};

		VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice)));
		D3D12_FEATURE_DATA_FEATURE_LEVELS caps{};
		caps.pFeatureLevelsRequested = featureLevels;
		caps.NumFeatureLevels = ARRAYSIZE(featureLevels);
		VERIFY_HR(pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)));
		VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), caps.MaxSupportedFeatureLevel, IID_PPV_ARGS(pDevice.ReleaseAndGetAddressOf())));
	}

	if (!pDevice)
	{
		E_LOG(Warning, "No D3D12 Adapter selected. Falling back to WARP");
		m_pFactory->EnumWarpAdapter(IID_PPV_ARGS(pAdapter.GetAddressOf()));
	}
	return pAdapter;
}

RefCountPtr<GraphicsDevice> GraphicsInstance::CreateDevice(RefCountPtr<IDXGIAdapter4> pAdapter)
{
	return new GraphicsDevice(pAdapter.Get());
}

GraphicsDevice::GraphicsDevice(IDXGIAdapter4* pAdapter)
	: GraphicsObject(this), m_DeleteQueue(this)
{
	VERIFY_HR(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())));
	m_pDevice->QueryInterface(&m_pRaytracingDevice);
	D3D::SetObjectName(m_pDevice.Get(), "Main Device");

	m_Capabilities.Initialize(this);

	auto OnDeviceRemovedCallback = [](void* pContext, BOOLEAN) {
		GraphicsDevice* pDevice = (GraphicsDevice*)pContext;
		std::string error = D3D::GetErrorString(DXGI_ERROR_DEVICE_REMOVED, pDevice->m_pDevice.Get());
		D3D::DREDHandler(pDevice->GetDevice());
		E_LOG(Error, "%s", error.c_str());
		abort();
	};

	m_pDeviceRemovalFence = new Fence(this, UINT64_MAX, "Device Removed Fence");
	m_DeviceRemovedEvent = CreateEventA(nullptr, false, false, nullptr);
	m_pDeviceRemovalFence->GetFence()->SetEventOnCompletion(UINT64_MAX, m_DeviceRemovedEvent);
	RegisterWaitForSingleObject(&m_DeviceRemovedEvent, m_DeviceRemovedEvent, OnDeviceRemovedCallback, this, INFINITE, 0);

	RefCountPtr<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(pInfoQueue.GetAddressOf()))))
	{
		// Suppress whole categories of messages
		//D3D12_MESSAGE_CATEGORY Categories[] = {};

		// Suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};

		// Suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] =
		{
			// This occurs when there are uninitialized descriptors in a descriptor table, even when a
			// shader does not access the missing descriptors.  I find this is common when switching
			// shader permutations and not wanting to change much code to reorder resources.
			D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
		};

		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories = _countof(Categories);
		//NewFilter.DenyList.pCategoryList = Categories;
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		if (CommandLine::GetBool("d3dbreakvalidation"))
		{
			VERIFY_HR_EX(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true), GetDevice());
			VERIFY_HR_EX(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true), GetDevice());
			E_LOG(Warning, "D3D Validation Break on Severity Enabled");
		}
		pInfoQueue->PushStorageFilter(&NewFilter);

		RefCountPtr<ID3D12InfoQueue1> pInfoQueue1;
		if (SUCCEEDED(pInfoQueue->QueryInterface(&pInfoQueue1)))
		{
			auto MessageCallback = [](
				D3D12_MESSAGE_CATEGORY Category,
				D3D12_MESSAGE_SEVERITY Severity,
				D3D12_MESSAGE_ID ID,
				LPCSTR pDescription,
				void* pContext)
			{
				E_LOG(Warning, "D3D12 Validation Layer: %s", pDescription);
			};

			DWORD callbackCookie = 0;
			VERIFY_HR(pInfoQueue1->RegisterMessageCallback(MessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &callbackCookie));
		}
	}

	bool setStablePowerState = CommandLine::GetBool("stablepowerstate");
	if (setStablePowerState)
	{
		D3D12EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);
		m_pDevice->SetStablePowerState(TRUE);
	}

	//Create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = new CommandQueue(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = new CommandQueue(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = new CommandQueue(this, D3D12_COMMAND_LIST_TYPE_COPY);

	m_pFrameFence = new Fence(this, 1, "Frame Fence");

	// Allocators
	m_pDynamicAllocationManager = new DynamicAllocationManager(this, BufferFlag::Upload);
	m_pGlobalViewHeap = new GlobalOnlineDescriptorHeap(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, 8192);
	m_pGlobalSamplerHeap = new GlobalOnlineDescriptorHeap(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 32, 2048);

	check(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = new OfflineDescriptorAllocator(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = new OfflineDescriptorAllocator(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = new OfflineDescriptorAllocator(this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = new OfflineDescriptorAllocator(this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);

	uint8 smMaj, smMin;
	m_Capabilities.GetShaderModel(smMaj, smMin);
	m_pShaderManager = std::make_unique<ShaderManager>(smMaj, smMin);
	m_pShaderManager->AddIncludeDir("Resources/Shaders/");
	m_pShaderManager->AddIncludeDir("Graphics/Core/");

	GraphicsCommon::Create(this);
}

GraphicsDevice::~GraphicsDevice()
{
	GraphicsCommon::Destroy();

	IdleGPU();
	check(UnregisterWait(m_DeviceRemovedEvent) != 0);
}

CommandQueue* GraphicsDevice::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type);
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
			pContext->Reset();
		}
		else
		{
			RefCountPtr<ID3D12CommandList> pCommandList;
			ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
			VERIFY_HR(GetDevice()->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf())));
			D3D::SetObjectName(pCommandList.Get(), Sprintf("Pooled Commandlist %d", m_CommandLists.size()).c_str());
			m_CommandLists.push_back(std::move(pCommandList));
			m_CommandListPool[typeIndex].emplace_back(new CommandContext(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), type, m_pGlobalViewHeap, m_pDynamicAllocationManager, pAllocator));
			pContext = m_CommandListPool[typeIndex].back();
		}
	}
	return pContext;
}

void GraphicsDevice::FreeCommandList(CommandContext* pCommandList)
{
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	m_FreeCommandLists[(int)pCommandList->GetType()].push(pCommandList);
}

bool GraphicsDevice::IsFenceComplete(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	return pQueue->GetFence()->IsComplete(fenceValue);
}

void GraphicsDevice::WaitForFence(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void GraphicsDevice::TickFrame()
{
	m_DeleteQueue.Clean();
	m_pFrameFence->Signal(GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT));
}

void GraphicsDevice::IdleGPU()
{
	TickFrame();
	m_pFrameFence->CpuWait(m_pFrameFence->GetLastSignaledValue());
	for (auto& pCommandQueue : m_CommandQueues)
	{
		if (pCommandQueue)
		{
			pCommandQueue->WaitForIdle();
		}
	}
}

DescriptorHandle GraphicsDevice::StoreViewDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE view)
{
	DescriptorHandle handle = m_pGlobalViewHeap->AllocatePersistent();
	m_pDevice->CopyDescriptorsSimple(1, handle.CpuHandle, view, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return handle;
}

void GraphicsDevice::FreeViewDescriptor(DescriptorHandle& handle)
{
	if (handle.HeapIndex != DescriptorHandle::InvalidHeapIndex)
	{
		m_pGlobalViewHeap->FreePersistent(handle.HeapIndex);
	}
}

RefCountPtr<Texture> GraphicsDevice::CreateTexture(const TextureDesc& desc, const char* pName)
{
	auto GetResourceDesc = [](const TextureDesc& textureDesc)
	{
		uint32 width = D3D::IsBlockCompressFormat(textureDesc.Format) ? Math::Clamp(textureDesc.Width, 0u, textureDesc.Width) : textureDesc.Width;
		uint32 height = D3D::IsBlockCompressFormat(textureDesc.Format) ? Math::Clamp(textureDesc.Height, 0u, textureDesc.Height) : textureDesc.Height;
		D3D12_RESOURCE_DESC desc{};
		switch (textureDesc.Dimensions)
		{
		case TextureDimension::Texture1D:
		case TextureDimension::Texture1DArray:
			desc = CD3DX12_RESOURCE_DESC::Tex1D(textureDesc.Format, width, (uint16)textureDesc.DepthOrArraySize, (uint16)textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
			break;
		case TextureDimension::Texture2D:
		case TextureDimension::Texture2DArray:
			desc = CD3DX12_RESOURCE_DESC::Tex2D(textureDesc.Format, width, height, (uint16)textureDesc.DepthOrArraySize, (uint16)textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
			desc = CD3DX12_RESOURCE_DESC::Tex2D(textureDesc.Format, width, height, (uint16)textureDesc.DepthOrArraySize * 6, (uint16)textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
			break;
		case TextureDimension::Texture3D:
			desc = CD3DX12_RESOURCE_DESC::Tex3D(textureDesc.Format, width, height, (uint16)textureDesc.DepthOrArraySize, (uint16)textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
			break;
		default:
			noEntry();
			break;
		}

		if (EnumHasAnyFlags(textureDesc.Usage, TextureFlag::UnorderedAccess))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		if (EnumHasAnyFlags(textureDesc.Usage, TextureFlag::RenderTarget))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}
		if (EnumHasAnyFlags(textureDesc.Usage, TextureFlag::DepthStencil))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			if (!EnumHasAnyFlags(textureDesc.Usage, TextureFlag::ShaderResource))
			{
				//I think this can be a significant optimization on some devices because then the depth buffer can never be (de)compressed
				desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
			}
		}
		return desc;
	};

	D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
	TextureFlag depthAndRt = TextureFlag::RenderTarget | TextureFlag::DepthStencil;
	check(EnumHasAllFlags(desc.Usage, depthAndRt) == false);

	D3D12_CLEAR_VALUE* pClearValue = nullptr;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = desc.Format;

	if (EnumHasAnyFlags(desc.Usage, TextureFlag::RenderTarget))
	{
		check(desc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::Color);
		memcpy(&clearValue.Color, &desc.ClearBindingValue.Color, sizeof(Color));
		resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		pClearValue = &clearValue;
	}
	if (EnumHasAnyFlags(desc.Usage, TextureFlag::DepthStencil))
	{
		check(desc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		clearValue.DepthStencil.Depth = desc.ClearBindingValue.DepthStencil.Depth;
		clearValue.DepthStencil.Stencil = desc.ClearBindingValue.DepthStencil.Stencil;
		resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		pClearValue = &clearValue;
	}

	D3D12_RESOURCE_DESC resourceDesc = GetResourceDesc(desc);

	ID3D12Resource* pResource;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	VERIFY_HR_EX(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &resourceDesc, resourceState, pClearValue, IID_PPV_ARGS(&pResource)), m_pDevice);

	Texture* pTexture = new Texture(this, desc, pResource);
	pTexture->SetResourceState(resourceState);
	pTexture->SetName(pName);

	if (EnumHasAnyFlags(desc.Usage, TextureFlag::ShaderResource))
	{
		pTexture->m_pSrv = CreateSRV(pTexture, TextureSRVDesc(0));
	}
	if (EnumHasAnyFlags(desc.Usage, TextureFlag::UnorderedAccess))
	{
		TextureUAVDesc uavDesc(0);
		pTexture->m_pUav = CreateUAV(pTexture, uavDesc);
	}
	if (EnumHasAnyFlags(desc.Usage, TextureFlag::RenderTarget))
	{
		pTexture->m_Rtv = GetParent()->AllocateDescriptor<D3D12_RENDER_TARGET_VIEW_DESC>();

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = desc.Format;
		switch (desc.Dimensions)
		{
		case TextureDimension::Texture1D:
			rtvDesc.Texture1D.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			rtvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
			rtvDesc.Texture1DArray.FirstArraySlice = 0;
			rtvDesc.Texture1DArray.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.Texture2D.PlaneSlice = 0;
			rtvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
		case TextureDimension::Texture2DArray:
			rtvDesc.Texture2DArray.MipSlice = 0;
			rtvDesc.Texture2DArray.PlaneSlice = 0;
			rtvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
			rtvDesc.Texture2DArray.FirstArraySlice = 0;
			rtvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			break;
		case TextureDimension::Texture3D:
			rtvDesc.Texture3D.FirstWSlice = 0;
			rtvDesc.Texture3D.MipSlice = 0;
			rtvDesc.Texture3D.WSize = desc.DepthOrArraySize;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			break;
		}
		GetParent()->GetDevice()->CreateRenderTargetView(pResource, &rtvDesc, pTexture->m_Rtv);
	}
	else if (EnumHasAnyFlags(desc.Usage, TextureFlag::DepthStencil))
	{
		pTexture->m_Rtv = GetParent()->AllocateDescriptor<D3D12_DEPTH_STENCIL_VIEW_DESC>();
		pTexture->m_ReadOnlyDsv = GetParent()->AllocateDescriptor<D3D12_DEPTH_STENCIL_VIEW_DESC>();

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = D3D::GetDsvFormat(desc.Format);
		switch (desc.Dimensions)
		{
		case TextureDimension::Texture1D:
			dsvDesc.Texture1D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			dsvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
			dsvDesc.Texture1DArray.FirstArraySlice = 0;
			dsvDesc.Texture1DArray.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
			break;
		case TextureDimension::Texture3D:
		case TextureDimension::Texture2DArray:
			dsvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
			dsvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize * 6;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			break;
		default:
			break;
		}
		GetParent()->GetDevice()->CreateDepthStencilView(pResource, &dsvDesc, pTexture->m_Rtv);
		dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		GetParent()->GetDevice()->CreateDepthStencilView(pResource, &dsvDesc, pTexture->m_ReadOnlyDsv);
	}

	return pTexture;
}

RefCountPtr<Texture> GraphicsDevice::CreateTextureForSwapchain(ID3D12Resource* pSwapchainResource)
{
	D3D12_RESOURCE_DESC resourceDesc = pSwapchainResource->GetDesc();
	TextureDesc desc;
	desc.Width = (uint32)resourceDesc.Width;
	desc.Height = (uint32)resourceDesc.Height;
	desc.Format = resourceDesc.Format;
	desc.ClearBindingValue = ClearBinding(Colors::Black);
	desc.Mips = resourceDesc.MipLevels;
	desc.SampleCount = resourceDesc.SampleDesc.Count;
	desc.Usage = TextureFlag::RenderTarget;

	Texture* pTexture = new Texture(this, desc, pSwapchainResource);
	pTexture->SetImmediateDelete(true);
	pTexture->SetName("Backbuffer");
	pTexture->SetResourceState(D3D12_RESOURCE_STATE_PRESENT);

	pTexture->m_Rtv = GetParent()->AllocateDescriptor<D3D12_RENDER_TARGET_VIEW_DESC>();
	GetParent()->GetDevice()->CreateRenderTargetView(pSwapchainResource, nullptr, pTexture->m_Rtv);
	pTexture->m_pSrv = CreateSRV(pTexture, TextureSRVDesc(0));
	return pTexture;
}

RefCountPtr<Texture> GraphicsDevice::CreateTextureFromImage(CommandContext& context, Image& image, bool sRGB, const char* pName)
{
	TextureDesc desc;
	desc.Width = image.GetWidth();
	desc.Height = image.GetHeight();
	desc.Format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(image.GetFormat(), sRGB);
	desc.Mips = image.GetMipLevels();
	desc.Usage = TextureFlag::ShaderResource;
	desc.Dimensions = image.IsCubemap() ? TextureDimension::TextureCube : TextureDimension::Texture2D;
	if (D3D::IsBlockCompressFormat(desc.Format))
	{
		desc.Width = Math::Max(desc.Width, 4u);
		desc.Height = Math::Max(desc.Height, 4u);
	}

	const Image* pImg = &image;
	std::vector<D3D12_SUBRESOURCE_DATA> subResourceData;
	int resourceOffset = 0;
	while (pImg)
	{
		subResourceData.resize(subResourceData.size() + desc.Mips);
		for (uint32 i = 0; i < desc.Mips; ++i)
		{
			D3D12_SUBRESOURCE_DATA& data = subResourceData[resourceOffset++];
			MipLevelInfo info = pImg->GetMipInfo(i);
			data.pData = pImg->GetData(i);
			data.RowPitch = info.RowSize;
			data.SlicePitch = (uint64)info.RowSize * info.Width;
		}
		pImg = pImg->GetNextImage();
	}
	RefCountPtr<Texture> pTexture = CreateTexture(desc, pName ? pName : "");
	context.InitializeTexture(pTexture, subResourceData.data(), 0, (int)subResourceData.size());
	return pTexture;
}

RefCountPtr<Texture> GraphicsDevice::CreateTextureFromFile(CommandContext& context, const char* pFilePath, bool sRGB, const char* pName)
{
	Image image;
	if (image.Load(pFilePath))
	{
		return CreateTextureFromImage(context, image, sRGB, pName);
	}
	return nullptr;
}

RefCountPtr<Buffer> GraphicsDevice::CreateBuffer(const BufferDesc& desc, const char* pName)
{
	auto GetResourceDesc = [](const BufferDesc& bufferDesc)
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
			Math::AlignUp<uint64>(bufferDesc.Size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
			D3D12_RESOURCE_FLAG_NONE
		);
		if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure) == false)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
		if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::UnorderedAccess))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		return desc;
	};

	D3D12_RESOURCE_DESC resourceDesc = GetResourceDesc(desc);
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN;

	if (EnumHasAnyFlags(desc.Usage, BufferFlag::Readback))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_COPY_DEST;
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	if (EnumHasAnyFlags(desc.Usage, BufferFlag::Upload))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		heapType = D3D12_HEAP_TYPE_UPLOAD;
	}
	if (EnumHasAnyFlags(desc.Usage, BufferFlag::AccelerationStructure))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	if (initialState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		initialState = D3D12_RESOURCE_STATE_COMMON;
	}

	ID3D12Resource* pResource;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
	VERIFY_HR_EX(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&pResource)), m_pDevice);

	Buffer* pBuffer = new Buffer(this, desc, pResource);
	pBuffer->SetResourceState(initialState);
	pBuffer->SetName(pName);

	if (EnumHasAnyFlags(desc.Usage, BufferFlag::Upload | BufferFlag::Readback))
	{
		VERIFY_HR(pResource->Map(0, nullptr, &pBuffer->m_pMappedData));
	}

	bool isRaw = EnumHasAnyFlags(desc.Usage, BufferFlag::ByteAddress);
	bool withCounter = !isRaw && desc.Format == DXGI_FORMAT_UNKNOWN;

	//#todo: Temp code. Pull out views from buffer
	if (EnumHasAnyFlags(desc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure))
	{
		pBuffer->m_pSrv = CreateSRV(pBuffer, BufferSRVDesc(desc.Format, isRaw));
	}
	if (EnumHasAnyFlags(desc.Usage, BufferFlag::UnorderedAccess))
	{
		pBuffer->m_pUav = CreateUAV(pBuffer, BufferUAVDesc(desc.Format, isRaw, withCounter));
	}

	return pBuffer;
}

void GraphicsDevice::ReleaseResource(ID3D12Object* pResource)
{
	m_DeleteQueue.EnqueueResource(pResource, GetFrameFence());
}

RefCountPtr<PipelineState> GraphicsDevice::CreateComputePipeline(RefCountPtr<RootSignature>& pRootSignature, const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	PipelineStateInitializer desc;
	desc.SetRootSignature(pRootSignature);
	desc.SetComputeShader(pShaderPath, entryPoint, defines.Copy());
	desc.SetName(Sprintf("%s:%s", pShaderPath, entryPoint).c_str());
	return CreatePipeline(desc);
}

RefCountPtr<PipelineState> GraphicsDevice::CreatePipeline(const PipelineStateInitializer& psoDesc)
{
	PipelineState* pPipeline = new PipelineState(this);
	pPipeline->Create(psoDesc);
	return pPipeline;
}

RefCountPtr<StateObject> GraphicsDevice::CreateStateObject(const StateObjectInitializer& stateDesc)
{
	StateObject* pStateObject = new StateObject(this);
	pStateObject->Create(stateDesc);
	return pStateObject;
}

RefCountPtr<ShaderResourceView> GraphicsDevice::CreateSRV(Buffer* pBuffer, const BufferSRVDesc& desc)
{
	check(pBuffer);
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor<D3D12_SHADER_RESOURCE_VIEW_DESC>();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.RaytracingAccelerationStructure.Location = pBuffer->GetGpuHandle();

		m_pDevice->CreateShaderResourceView(nullptr, &srvDesc, descriptor);
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
			srvDesc.Format = desc.Format;
			srvDesc.Buffer.StructureByteStride = desc.Format == DXGI_FORMAT_UNKNOWN ? bufferDesc.ElementSize : 0;
			srvDesc.Buffer.FirstElement = desc.ElementOffset;
			srvDesc.Buffer.NumElements = desc.NumElements > 0 ? desc.NumElements : bufferDesc.NumElements();
		}

		m_pDevice->CreateShaderResourceView(pBuffer->GetResource(), &srvDesc, descriptor);
	}

	DescriptorHandle gpuDescriptor = StoreViewDescriptor(descriptor);
	return new ShaderResourceView(pBuffer, descriptor, gpuDescriptor);
}

RefCountPtr<UnorderedAccessView> GraphicsDevice::CreateUAV(Buffer* pBuffer, const BufferUAVDesc& desc)
{
	check(pBuffer);
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = bufferDesc.NumElements();
	uavDesc.Buffer.StructureByteStride = 0;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (desc.Raw)
	{
		uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		uavDesc.Buffer.NumElements *= bufferDesc.ElementSize / 4;
	}
	else
	{
		uavDesc.Buffer.StructureByteStride = uavDesc.Format == DXGI_FORMAT_UNKNOWN ? bufferDesc.ElementSize : 0;
	}

	RefCountPtr<Buffer> pCounter;
	if (desc.Counter)
	{
		std::string name = pBuffer->GetName() + " - Counter";
		pCounter = GetParent()->GetParent()->CreateBuffer(BufferDesc::CreateByteAddress(4), name.c_str());
	}

	D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor<D3D12_UNORDERED_ACCESS_VIEW_DESC>();
	m_pDevice->CreateUnorderedAccessView(pBuffer->GetResource(), pCounter ? pCounter->GetResource() : nullptr, &uavDesc, descriptor);
	DescriptorHandle gpuDescriptor = StoreViewDescriptor(descriptor);
	return new UnorderedAccessView(pBuffer, descriptor, gpuDescriptor, pCounter);
}

RefCountPtr<ShaderResourceView> GraphicsDevice::CreateSRV(Texture* pTexture, const TextureSRVDesc& desc)
{
	check(pTexture);
	const TextureDesc& textureDesc = pTexture->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = D3D::GetSrvFormatFromDepth(textureDesc.Format);

	switch (textureDesc.Dimensions)
	{
	case TextureDimension::Texture1D:
		srvDesc.Texture1D.MipLevels = textureDesc.Mips;
		srvDesc.Texture1D.MostDetailedMip = 0;
		srvDesc.Texture1D.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
		break;
	case TextureDimension::Texture1DArray:
		srvDesc.Texture1DArray.ArraySize = textureDesc.DepthOrArraySize;
		srvDesc.Texture1DArray.FirstArraySlice = 0;
		srvDesc.Texture1DArray.MipLevels = textureDesc.Mips;
		srvDesc.Texture1DArray.MostDetailedMip = 0;
		srvDesc.Texture1DArray.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureDimension::Texture2D:
		srvDesc.Texture2D.MipLevels = textureDesc.Mips;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = textureDesc.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
		break;
	case TextureDimension::Texture2DArray:
		srvDesc.Texture2DArray.MipLevels = textureDesc.Mips;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.PlaneSlice = 0;
		srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
		srvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.ViewDimension = textureDesc.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureDimension::Texture3D:
		srvDesc.Texture3D.MipLevels = textureDesc.Mips;
		srvDesc.Texture3D.MostDetailedMip = 0;
		srvDesc.Texture3D.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		break;
	case TextureDimension::TextureCube:
		srvDesc.TextureCube.MipLevels = textureDesc.Mips;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		break;
	case TextureDimension::TextureCubeArray:
		srvDesc.TextureCubeArray.MipLevels = textureDesc.Mips;
		srvDesc.TextureCubeArray.MostDetailedMip = 0;
		srvDesc.TextureCubeArray.ResourceMinLODClamp = 0;
		srvDesc.TextureCubeArray.First2DArrayFace = 0;
		srvDesc.TextureCubeArray.NumCubes = textureDesc.DepthOrArraySize;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		break;
	default:
		break;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor<D3D12_SHADER_RESOURCE_VIEW_DESC>();
	m_pDevice->CreateShaderResourceView(pTexture->GetResource(), &srvDesc, descriptor);
	DescriptorHandle gpuDescriptor = StoreViewDescriptor(descriptor);
	return new ShaderResourceView(pTexture, descriptor, gpuDescriptor);
}

RefCountPtr<UnorderedAccessView> GraphicsDevice::CreateUAV(Texture* pTexture, const TextureUAVDesc& desc)
{
	check(pTexture);
	const TextureDesc& textureDesc = pTexture->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	switch (textureDesc.Dimensions)
	{
	case TextureDimension::Texture1D:
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
		break;
	case TextureDimension::Texture1DArray:
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureDimension::Texture2D:
		uavDesc.Texture2D.PlaneSlice = 0;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		break;
	case TextureDimension::Texture2DArray:
		uavDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.PlaneSlice = 0;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureDimension::Texture3D:
		uavDesc.Texture3D.FirstWSlice = 0;
		uavDesc.Texture3D.WSize = textureDesc.DepthOrArraySize;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		break;
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
		uavDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize * 6;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.PlaneSlice = 0;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		break;
	default:
		break;
	}
	uavDesc.Texture1D.MipSlice = desc.MipLevel;
	uavDesc.Texture1DArray.MipSlice = desc.MipLevel;
	uavDesc.Texture2D.MipSlice = desc.MipLevel;
	uavDesc.Texture2DArray.MipSlice = desc.MipLevel;
	uavDesc.Texture3D.MipSlice = desc.MipLevel;

	D3D12_CPU_DESCRIPTOR_HANDLE descriptor = pTexture->GetParent()->AllocateDescriptor<D3D12_UNORDERED_ACCESS_VIEW_DESC>();
	m_pDevice->CreateUnorderedAccessView(pTexture->GetResource(), nullptr, &uavDesc, descriptor);
	DescriptorHandle gpuDescriptor = StoreViewDescriptor(descriptor);
	return new UnorderedAccessView(pTexture, descriptor, gpuDescriptor);
}

Shader* GraphicsDevice::GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const Span<ShaderDefine>& defines /*= {}*/)
{
	return m_pShaderManager->GetShader(pShaderPath, shaderType, pEntryPoint, defines);
}

ShaderLibrary* GraphicsDevice::GetLibrary(const char* pShaderPath, const Span<ShaderDefine>& defines /*= {}*/)
{
	return m_pShaderManager->GetLibrary(pShaderPath, defines);
}

DeferredDeleteQueue::DeferredDeleteQueue(GraphicsDevice* pParent)
	: GraphicsObject(pParent)
{
}

DeferredDeleteQueue::~DeferredDeleteQueue()
{
	GetParent()->IdleGPU();
	Clean();
	check(m_DeletionQueue.empty());
}

void DeferredDeleteQueue::EnqueueResource(ID3D12Object* pResource, Fence* pFence)
{
	std::scoped_lock<std::mutex> lock(m_QueueCS);
	FencedObject object;
	object.pFence = pFence;
	object.FenceValue = pFence->GetCurrentValue();
	object.pResource = pResource;
	m_DeletionQueue.push(object);
}

void DeferredDeleteQueue::Clean()
{
	std::scoped_lock<std::mutex> lock(m_QueueCS);
	while (!m_DeletionQueue.empty())
	{
		const FencedObject& p = m_DeletionQueue.front();
		if (!p.pFence->IsComplete(p.FenceValue))
		{
			break;
		}
		p.pResource->Release();
		m_DeletionQueue.pop();
	}
}

void GraphicsCapabilities::Initialize(GraphicsDevice* pDevice)
{
	m_pDevice = pDevice;

	check(m_FeatureSupport.Init(pDevice->GetDevice()) == S_OK);
	checkf(m_FeatureSupport.ResourceHeapTier() >= D3D12_RESOURCE_HEAP_TIER_2, "Device does not support Resource Heap Tier 2 or higher. Tier 1 is not supported");
	checkf(m_FeatureSupport.ResourceBindingTier() >= D3D12_RESOURCE_BINDING_TIER_3, "Device does not support Resource Binding Tier 3 or higher. Tier 2 and under is not supported.");
	checkf(m_FeatureSupport.HighestShaderModel() >= D3D_SHADER_MODEL_6_6, "Device does not support SM 6.6 which is required for dynamic indexing");
	checkf(m_FeatureSupport.WaveOps(), "Device does not support wave ops which is required.");

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

SwapChain::SwapChain(GraphicsDevice* pDevice, IDXGIFactory6* pFactory, WindowHandle pNativeWindow, uint32 width, uint32 height, uint32 numFrames, bool vsync)
	: GraphicsObject(pDevice), m_Format(DXGI_FORMAT_R8G8B8A8_UNORM), m_CurrentImage(0), m_Vsync(vsync)
{
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	desc.BufferCount = numFrames;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	desc.Format = m_Format;
	desc.Width = width;
	desc.Height = height;
	desc.Scaling = DXGI_SCALING_NONE;
	desc.Stereo = FALSE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.RefreshRate.Denominator = 60;
	fsDesc.RefreshRate.Numerator = 1;
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fsDesc.Windowed = true;

	CommandQueue* pPresentQueue = pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	RefCountPtr<IDXGISwapChain1> swapChain;
	
	VERIFY_HR(pFactory->CreateSwapChainForHwnd(
		pPresentQueue->GetCommandQueue(),
		(HWND)pNativeWindow,
		&desc,
		&fsDesc,
		nullptr,
		swapChain.GetAddressOf()));

	m_pSwapchain.Reset();
	swapChain->QueryInterface(&m_pSwapchain);
	m_Backbuffers.resize(numFrames);
}

SwapChain::~SwapChain()
{
	m_pSwapchain->SetFullscreenState(false, nullptr);
}

void SwapChain::OnResize(uint32 width, uint32 height)
{
	for (size_t i = 0; i < m_Backbuffers.size(); ++i)
	{
		m_Backbuffers[i].Reset();
	}

	//Resize the buffers
	DXGI_SWAP_CHAIN_DESC1 desc{};
	m_pSwapchain->GetDesc1(&desc);
	VERIFY_HR(m_pSwapchain->ResizeBuffers(
		(uint32)m_Backbuffers.size(),
		width,
		height,
		desc.Format,
		desc.Flags
	));

	m_CurrentImage = 0;

	//Recreate the render target views
	for (uint32 i = 0; i < (uint32)m_Backbuffers.size(); ++i)
	{
		ID3D12Resource* pResource = nullptr;
		VERIFY_HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_Backbuffers[i] = GetParent()->CreateTextureForSwapchain(pResource);
	}
}

void SwapChain::Present()
{
	m_pSwapchain->Present(m_Vsync, m_Vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
	m_CurrentImage = m_pSwapchain->GetCurrentBackBufferIndex();
}
