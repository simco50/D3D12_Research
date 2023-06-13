#include "stdafx.h"
#include "DemoApp.h"
#include "Scene/Camera.h"
#include "ImGuizmo.h"
#include "Content/Image.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/Profiler.h"
#include "Graphics/Mesh.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/DynamicResourceAllocator.h"
#include "Graphics/RHI/Shader.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/ShaderBindingTable.h"
#include "Graphics/RHI/StateObject.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Techniques/GpuParticles.h"
#include "Graphics/Techniques/RTAO.h"
#include "Graphics/Techniques/ForwardRenderer.h"
#include "Graphics/Techniques/VolumetricFog.h"
#include "Graphics/Techniques/RTReflections.h"
#include "Graphics/Techniques/PathTracing.h"
#include "Graphics/Techniques/SSAO.h"
#include "Graphics/Techniques/CBTTessellation.h"
#include "Graphics/Techniques/Clouds.h"
#include "Graphics/Techniques/ShaderDebugRenderer.h"
#include "Graphics/Techniques/MeshletRasterizer.h"
#include "Graphics/Techniques/VisualizeTexture.h"
#include "Graphics/Techniques/LightCulling.h"
#include "Graphics/ImGuiRenderer.h"
#include "Core/TaskQueue.h"
#include "Core/CommandLine.h"
#include "Core/Paths.h"
#include "Core/Input.h"
#include "Core/ConsoleVariables.h"
#include "Core/Utils.h"
#include "imgui_internal.h"
#include "IconsFontAwesome4.h"

namespace Tweakables
{
	ConsoleVariable g_Vsync("r.Vsync", true);

	// Post processing
	ConsoleVariable g_WhitePoint("r.Exposure.WhitePoint", 1.0f);
	ConsoleVariable g_MinLogLuminance("r.Exposure.MinLogLuminance", -4.0f);
	ConsoleVariable g_MaxLogLuminance("r.Exposure.MaxLogLuminance", 20.0f);
	ConsoleVariable g_Tau("r.Exposure.Tau", 2.0f);
	ConsoleVariable g_DrawHistogram("vis.Histogram", false);
	ConsoleVariable g_ToneMapper("r.Tonemapper", 2);
	ConsoleVariable g_TAA("r.Taa", true);

	// Shadows
	ConsoleVariable g_SDSM("r.Shadows.SDSM", false);
	ConsoleVariable g_VisualizeShadowCascades("vis.ShadowCascades", false);
	ConsoleVariable g_ShadowCascades("r.Shadows.CascadeCount", 4);
	ConsoleVariable g_PSSMFactor("r.Shadow.PSSMFactor", 0.85f);
	ConsoleVariable g_ShadowsGPUCull("r.Shadows.GPUCull", true);
	ConsoleVariable g_ShadowsOcclusionCulling("r.Shadows.OcclusionCull", true);

	// Bloom
	ConsoleVariable g_Bloom("r.Bloom", true);
	ConsoleVariable g_BloomIntensity("r.Bloom.Intensity", 1.0f);
	ConsoleVariable g_BloomBlendFactor("r.Bloom.BlendFactor", 0.3f);
	ConsoleVariable g_BloomInteralBlendFactor("r.Bloom.InteralBlendFactor", 0.85f);

	// Misc Lighting
	ConsoleVariable g_Sky("r.Sky", true);
	ConsoleVariable g_VolumetricFog("r.VolumetricFog", true);
	ConsoleVariable g_Clouds("r.Clouds", true);
	ConsoleVariable g_RaytracedAO("r.Raytracing.AO", false);
	ConsoleVariable g_VisualizeLights("vis.Lights", false);
	ConsoleVariable g_VisualizeLightDensity("vis.LightDensity", false);
	ConsoleVariable g_EnableDDGI("r.DDGI", true);
	ConsoleVariable g_VisualizeDDGI("vis.DDGI", false);
	ConsoleVariable g_RenderObjectBounds("r.vis.ObjectBounds", false);

	ConsoleVariable g_RaytracedReflections("r.Raytracing.Reflections", false);
	ConsoleVariable g_TLASBoundsThreshold("r.Raytracing.TLASBoundsThreshold", 1.0f * Math::DegreesToRadians);
	ConsoleVariable g_SsrSamples("r.SSRSamples", 8);
	ConsoleVariable g_RenderTerrain("r.Terrain", false);
	ConsoleVariable g_OcclusionCulling("r.OcclusionCulling", true);

	// Misc
	ConsoleVariable CullDebugStats("r.CullingStats", false);

	bool g_DumpRenderGraph = false;
	bool g_EnableRenderGraphResourceTracker = false;
	ConsoleCommand<> gDumpRenderGraph("DumpRenderGraph", []() { g_DumpRenderGraph = true; });
	bool g_Screenshot = false;
	ConsoleCommand<> gScreenshot("Screenshot", []() { g_Screenshot = true; });

	std::string VisualizeTextureName = "";
	ConsoleCommand<const char*> gVisualizeTexture("vis", [](const char* pName) { VisualizeTextureName = pName; });

	// Lighting
	float g_SunInclination = 0.79f;
	float g_SunOrientation = -0.15f;
	float g_SunTemperature = 5900.0f;
	float g_SunIntensity = 5.0f;
}

DemoApp::DemoApp(WindowHandle window, const Vector2i& windowRect)
	: m_Window(window)
{
	E_LOG(Info, "Graphics::InitD3D()");

	GraphicsDeviceOptions options;
	options.UseDebugDevice =		CommandLine::GetBool("d3ddebug");
	options.UseDRED =				CommandLine::GetBool("dred");
	options.LoadPIX =				CommandLine::GetBool("pix");
	options.UseGPUValidation =		CommandLine::GetBool("gpuvalidation");
	options.UseWarp =				CommandLine::GetBool("warp");
	options.UseStablePowerState =	CommandLine::GetBool("stablepowerstate");
	m_pDevice = new GraphicsDevice(options);

	m_pSwapchain = new SwapChain(m_pDevice, DisplayMode::SDR, window);

	GraphicsCommon::Create(m_pDevice);

	m_RenderGraphPool = std::make_unique<RGResourcePool>(m_pDevice);

	Profiler::Get()->Initialize(m_pDevice);
	DebugRenderer::Get()->Initialize(m_pDevice);

	m_pShaderDebugRenderer = std::make_unique<ShaderDebugRenderer>(m_pDevice);
	m_pShaderDebugRenderer->GetGPUData(&m_SceneData.DebugRenderData);

	ImGuiRenderer::Initialize(m_pDevice, window);
	m_pMeshletRasterizer	= std::make_unique<MeshletRasterizer>(m_pDevice);
	m_pDDGI					= std::make_unique<DDGI>(m_pDevice);
	m_pClouds				= std::make_unique<Clouds>(m_pDevice);
	m_pVolumetricFog		= std::make_unique<VolumetricFog>(m_pDevice);
	m_pLightCulling			= std::make_unique<LightCulling>(m_pDevice);
	m_pForwardRenderer		= std::make_unique<ForwardRenderer>(m_pDevice);
	m_pRTReflections		= std::make_unique<RTReflections>(m_pDevice);
	m_pRTAO					= std::make_unique<RTAO>(m_pDevice);
	m_pSSAO					= std::make_unique<SSAO>(m_pDevice);
	m_pParticles			= std::make_unique<GpuParticles>(m_pDevice);
	m_pPathTracing			= std::make_unique<PathTracing>(m_pDevice);
	m_pCBTTessellation		= std::make_unique<CBTTessellation>(m_pDevice);
	m_pVisualizeTexture		= std::make_unique<VisualizeTexture>(m_pDevice);

	InitializePipelines();

	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	SetupScene(*pContext);
	pContext->Execute();

	OnResizeOrMove(windowRect.x, windowRect.y);
	OnResizeViewport(windowRect.x, windowRect.y);
}

DemoApp::~DemoApp()
{
	m_pDevice->IdleGPU();
	ImGuiRenderer::Shutdown();
	GraphicsCommon::Destroy();
	DebugRenderer::Get()->Shutdown();
	Profiler::Get()->Shutdown();
}

void DemoApp::SetupScene(CommandContext& context)
{
	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetNearPlane(80.0f);
	m_pCamera->SetFarPlane(0.1f);
	m_pCamera->SetPosition(Vector3(-1.3f, 12.4f, -1.5f));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PI_DIV_4, Math::PI_DIV_4 * 0.5f, 0));

	LoadMesh("Resources/Scenes/Sponza/Sponza.gltf", context, m_World);

	{
		Light sunLight = Light::Directional(Vector3::Zero, Vector3::Down, 10);
		sunLight.CastShadows = true;
		sunLight.VolumetricLighting = true;
		m_World.Lights.push_back(sunLight);
	}
	{
		Light spot = Light::Spot(Vector3(0, 0, 0), 4.0f, Vector3::Down, 70.0f, 50.0f, 100.0f);
		spot.CastShadows = true;
		spot.VolumetricLighting = true;
		spot.pLightTexture = GraphicsCommon::CreateTextureFromFile(context, "Resources/Textures/LightProjector.png", false, "Light Cookie");

		spot.Position = Vector3(9.5, 3, 3.5);
		m_World.Lights.push_back(spot);
		spot.Position = Vector3(-9.5, 3, 3.5);
		m_World.Lights.push_back(spot);
		spot.Position = Vector3(9.5, 3, -3.5);
		m_World.Lights.push_back(spot);
		spot.Position = Vector3(-9.5, 3, -3.5);
		m_World.Lights.push_back(spot);
	}
	{
		DDGIVolume& volume = m_World.DDGIVolumes.emplace_back();
		volume.Origin = Vector3(-0.484151840f, 5.21196413f, 0.309524536f);
		volume.Extents = Vector3(14.8834171f, 6.22350454f, 9.15293312f);
		volume.NumProbes = Vector3i(16, 12, 14);
		volume.NumRays = 128;
		volume.MaxNumRays = 512;
	}

	m_pLensDirtTexture = GraphicsCommon::CreateTextureFromFile(context, "Resources/Textures/LensDirt.dds", true, "Lens Dirt");
}

void DemoApp::Update()
{
	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	Profiler::Get()->Resolve(pContext);

	{
		GPU_PROFILE_SCOPE("Update", pContext);

		constexpr RenderPath defaultRenderPath = RenderPath::Clustered;
		if (m_RenderPath == RenderPath::Visibility)
			m_RenderPath = m_pDevice->GetCapabilities().SupportsMeshShading() ? m_RenderPath : defaultRenderPath;
		if (m_RenderPath == RenderPath::PathTracing)
			m_RenderPath = m_pDevice->GetCapabilities().SupportsRaytracing() ? m_RenderPath : defaultRenderPath;

		m_pDevice->GetShaderManager()->ConditionallyReloadShaders();

		UpdateImGui();

		m_pCamera->SetJitter(Tweakables::g_TAA && m_RenderPath != RenderPath::PathTracing);
		m_pCamera->Update();
		m_RenderGraphPool->Tick();

		RenderPath newRenderPath = m_RenderPath;
		if (!ImGui::IsAnyItemActive())
		{
			if (Input::Instance().IsKeyPressed('1'))
				newRenderPath = RenderPath::Clustered;
			else if (Input::Instance().IsKeyPressed('2'))
				newRenderPath = RenderPath::Tiled;
			else if (Input::Instance().IsKeyPressed('3'))
				newRenderPath = RenderPath::Visibility;
			else if (Input::Instance().IsKeyPressed('4'))
				newRenderPath = RenderPath::PathTracing;
		}
		if (newRenderPath == RenderPath::Visibility && !m_pDevice->GetCapabilities().SupportsMeshShading())
			newRenderPath = RenderPath::Clustered;
		if (newRenderPath == RenderPath::PathTracing && !m_pDevice->GetCapabilities().SupportsRaytracing())
			newRenderPath = RenderPath::Clustered;
		m_RenderPath = newRenderPath;

		Tweakables::g_RaytracedAO = m_pDevice->GetCapabilities().SupportsRaytracing() && Tweakables::g_RaytracedAO;
		Tweakables::g_RaytracedReflections = m_pDevice->GetCapabilities().SupportsRaytracing() && Tweakables::g_RaytracedReflections;

		if (Tweakables::g_RenderObjectBounds)
		{
			for (const Batch& b : m_SceneData.Batches)
			{
				DebugRenderer::Get()->AddBoundingBox(b.Bounds, Color(0.2f, 0.2f, 0.9f, 1.0f));
				DebugRenderer::Get()->AddSphere(b.Bounds.Center, b.Radius, 5, 5, Color(0.2f, 0.6f, 0.2f, 1.0f));
			}
		}

		Light& sun = m_World.Lights.front();
		sun.Rotation = Quaternion::CreateFromYawPitchRoll(-Tweakables::g_SunOrientation, Tweakables::g_SunInclination * Math::PI_DIV_2, 0);
		sun.Colour = Math::MakeFromColorTemperature(Tweakables::g_SunTemperature);
		sun.Intensity = Tweakables::g_SunIntensity;

		if (Tweakables::g_VisualizeLights)
		{
			for (const Light& light : m_World.Lights)
			{
				DebugRenderer::Get()->AddLight(light);
			}
		}

		if (m_World.DDGIVolumes.size() > 0)
		{
			DDGIVolume& volume = m_World.DDGIVolumes[0];
			volume.Origin = m_SceneData.SceneAABB.Center;
			volume.Extents = 1.1f * Vector3(m_SceneData.SceneAABB.Extents);
		}

		CreateShadowViews(m_SceneData, m_World);
		m_SceneData.MainView = m_pCamera->GetViewTransform();
		m_SceneData.FrameIndex = m_Frame;
		m_SceneData.pWorld = &m_World;
	}
	{
		GPU_PROFILE_SCOPE("Render", pContext);

		if (Tweakables::g_Screenshot)
		{
			Tweakables::g_Screenshot = false;
			TaskContext taskContext;
			TaskQueue::Execute([this](uint32)
				{
					CommandContext* pScreenshotContext = m_pDevice->AllocateCommandContext();
					RefCountPtr<Texture> pSource = m_pColorOutput;
					uint32 width = pSource->GetWidth();
					uint32 height = pSource->GetHeight();

					D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
					D3D12_RESOURCE_DESC resourceDesc = pSource->GetResource()->GetDesc();
					m_pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
					RefCountPtr<Buffer> pScreenshotBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
					pScreenshotContext->InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_COPY_SOURCE);
					pScreenshotContext->InsertResourceBarrier(pScreenshotBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
					pScreenshotContext->CopyTexture(pSource, pScreenshotBuffer, CD3DX12_BOX(0, 0, width, height));

					SyncPoint fence = pScreenshotContext->Execute();
					fence.Wait();

					char* pData = (char*)pScreenshotBuffer->GetMappedData();
					Image img(width, height, 1, ResourceFormat::RGBA8_UNORM, 1);
					uint32 imageRowPitch = width * 4;
					uint32 targetOffset = 0;
					for (uint32 i = 0; i < height; ++i)
					{
						img.SetData((uint32*)pData, targetOffset, imageRowPitch);
						pData += textureFootprint.Footprint.RowPitch;
						targetOffset += imageRowPitch;
					}

					Paths::CreateDirectoryTree(Paths::ScreenshotDir());
					img.Save(Sprintf("%sScreenshot_%s.jpg", Paths::ScreenshotDir().c_str(), Utils::GetTimeString().c_str()).c_str());
				}, taskContext);
		}

		const SceneView* pView = &m_SceneData;
		//const World* pWorld = &m_World;
		SceneView* pViewMut = &m_SceneData;
		World* pWorldMut = &m_World;

		{
			// Other queues are super slow on CPU with debug layer for some reason
			const bool asyncCompute = false;
			if (asyncCompute)
			{
				CommandQueue* pDirectQueue = m_pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
				CommandQueue* pComputeQueue = m_pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
				CommandQueue* pCopyQueue = m_pDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);

				pCopyQueue->InsertWait(pDirectQueue);

				CommandContext* pCopyContext = m_pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
				Renderer::UploadSceneData(*pCopyContext, pViewMut, pWorldMut);

				SyncPoint copySync = pCopyContext->Execute();
				pComputeQueue->InsertWait(copySync);
				CommandContext* pComputeContext = m_pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
				pViewMut->AccelerationStructure.Build(*pComputeContext, *pView);

				SyncPoint computeSync = pComputeContext->Execute();
				pDirectQueue->InsertWait(computeSync);
			}
			else
			{
				Renderer::UploadSceneData(*pContext, pViewMut, pWorldMut);
				pViewMut->AccelerationStructure.Build(*pContext, *pView);
			}
		}

		{
			PROFILE_SCOPE("Frustum Culling");

			// Sort
			auto CompareSort = [this](const Batch& a, const Batch& b)
			{
				float aDist = Vector3::DistanceSquared(a.Bounds.Center, m_SceneData.MainView.Position);
				float bDist = Vector3::DistanceSquared(b.Bounds.Center, m_SceneData.MainView.Position);
				if (a.BlendMode != b.BlendMode)
					return (int)a.BlendMode < (int)b.BlendMode;
				return EnumHasAnyFlags(a.BlendMode, Batch::Blending::AlphaBlend) ? bDist < aDist : aDist < bDist;
			};
			std::sort(m_SceneData.Batches.begin(), m_SceneData.Batches.end(), CompareSort);

			TaskContext taskContext;
			// In Visibility Buffer mode, culling is done on the GPU.
			if (m_RenderPath != RenderPath::Visibility)
			{
				TaskQueue::Execute([&](int)
					{
						m_SceneData.VisibilityMask.SetAll();
						BoundingFrustum frustum = m_pCamera->GetFrustum();
						for (const Batch& b : m_SceneData.Batches)
						{
							m_SceneData.VisibilityMask.AssignBit(b.InstanceID, frustum.Contains(b.Bounds));
						}
					}, taskContext);
			}
			if (!Tweakables::g_ShadowsGPUCull)
			{
				TaskQueue::ExecuteMany([&](TaskDistributeArgs args)
					{
						ShadowView& shadowView = m_SceneData.ShadowViews[args.JobIndex];
						shadowView.Visibility.SetAll();
						for (const Batch& b : m_SceneData.Batches)
						{
							shadowView.Visibility.AssignBit(b.InstanceID, shadowView.View.IsInFrustum(b.Bounds));
						}
					}, taskContext, (uint32)m_SceneData.ShadowViews.size(), 1);
			}

			TaskQueue::Execute([&](int)
				{
					bool boundsSet = false;
					for (const Batch& b : m_SceneData.Batches)
					{
						if (boundsSet)
						{
							BoundingBox::CreateMerged(m_SceneData.SceneAABB, m_SceneData.SceneAABB, b.Bounds);
						}
						else
						{
							m_SceneData.SceneAABB = b.Bounds;
							boundsSet = true;
						}
					}
				}, taskContext);

			TaskQueue::Join(taskContext);
		}

		RGGraph graph(*m_RenderGraphPool);

		const Vector2u viewDimensions = m_SceneData.GetDimensions();

		SceneTextures sceneTextures;
		sceneTextures.pDepth =			graph.Create("Depth Stencil",	TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::DepthStencilFormat, 1, TextureFlag::None, ClearBinding(0.0f, 0)));
		sceneTextures.pRoughness =		graph.Create("Roughness",		TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::R8_UNORM));
		sceneTextures.pColorTarget =	graph.Create("Color Target",	TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RGBA16_FLOAT));
		sceneTextures.pNormals =		graph.Create("Normals",			TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RG16_FLOAT));
		sceneTextures.pVelocity =		graph.Create("Velocity",		TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RG16_FLOAT));
		sceneTextures.pPreviousColor =	graph.TryImport(m_pColorHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));

		LightCull2DData lightCull2DData;
		LightCull3DData lightCull3DData;

		RGTexture* pSky = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::BlackCube));
		if (Tweakables::g_Sky)
		{
			pSky = graph.Create("Sky", TextureDesc::CreateCube(64, 64, ResourceFormat::RGBA16_FLOAT));
			graph.AddPass("Compute Sky", RGPassFlag::Compute | RGPassFlag::NeverCull)
				.Write(pSky)
				.Bind([=](CommandContext& context)
					{
						Texture* pSkyTexture = pSky->Get();
						context.SetComputeRootSignature(m_pCommonRS);
						context.SetPipelineState(m_pRenderSkyPSO);

						context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pSkyTexture));
						context.BindResources(2, pSkyTexture->GetUAV());

						context.Dispatch(ComputeUtils::GetNumThreadGroups(pSkyTexture->GetWidth(), 16, pSkyTexture->GetHeight(), 16, 6));
					});

			graph.AddPass("Transition Sky", RGPassFlag::Raster)
				.Read(pSky);
		}

		// Export makes sure the target texture is filled in during pass execution.
		graph.Export(pSky, &pViewMut->pSky, TextureFlag::ShaderResource);

		RasterResult rasterResult;
		if (m_RenderPath != RenderPath::PathTracing)
		{
			{
				RG_GRAPH_SCOPE("Shadow Depths", graph);
				for (uint32 i = 0; i < (uint32)pView->ShadowViews.size(); ++i)
				{
					const ShadowView& shadowView = pView->ShadowViews[i];

					auto LightTypeToString = [](LightType type) -> const char* {
						switch (type)
						{
						case LightType::Directional:	return "Directional";
						case LightType::Point:			return "Point";
						case LightType::Spot:			return "Spot";
						default:						return "INVALID";
						}
					};
					const std::string passName = Sprintf("View %d (%s - Cascade %d)", i, LightTypeToString(shadowView.pLight->Type), shadowView.ViewIndex);

					RGTexture* pShadowmap = graph.Import(pView->ShadowViews[i].pDepthTexture);

					if (Tweakables::g_ShadowsGPUCull)
					{
						RG_GRAPH_SCOPE(passName.c_str(), graph);

						RasterContext context(graph, pShadowmap, RasterMode::Shadows, &m_ShadowHZBs[i]);
						context.EnableOcclusionCulling = Tweakables::g_ShadowsOcclusionCulling;
						RasterResult result;
						m_pMeshletRasterizer->Render(graph, pView, &shadowView.View, context, result);
					}
					else
					{
						graph.AddPass(passName.c_str(), RGPassFlag::Raster)
							.DepthStencil(pShadowmap, RenderTargetLoadAction::Clear, true)
							.Bind([=](CommandContext& context)
								{
									context.SetGraphicsRootSignature(m_pCommonRS);
									context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

									const ShadowView& view = pView->ShadowViews[i];
									context.BindRootCBV(1, Renderer::GetViewUniforms(pView, &view.View, pShadowmap->Get()));

									{
										GPU_PROFILE_SCOPE("Opaque", &context);
										context.SetPipelineState(m_pShadowsOpaquePSO);
										Renderer::DrawScene(context, pView->Batches, view.Visibility, Batch::Blending::Opaque);
									}
									{
										GPU_PROFILE_SCOPE("Masked", &context);
										context.SetPipelineState(m_pShadowsAlphaMaskPSO);
										Renderer::DrawScene(context, pView->Batches, view.Visibility, Batch::Blending::AlphaMask | Batch::Blending::AlphaBlend);
									}
								});
					}
				}
			}

			const bool doPrepass = true;
			const bool needVisibilityBuffer = m_RenderPath == RenderPath::Visibility;

			if (doPrepass)
			{
				if (needVisibilityBuffer)
				{
					RasterContext rasterContext(graph, sceneTextures.pDepth, RasterMode::VisibilityBuffer, &m_pHZB);
					rasterContext.EnableDebug = m_VisibilityDebugRenderMode > 0;
					rasterContext.EnableOcclusionCulling = Tweakables::g_OcclusionCulling;
					m_pMeshletRasterizer->Render(graph,	pView, &pView->MainView, rasterContext, rasterResult);
					if (Tweakables::CullDebugStats)
						m_pMeshletRasterizer->PrintStats(graph, pView, rasterContext);
				}
				else
				{
					graph.AddPass("Depth Prepass", RGPassFlag::Raster)
						.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Clear, true)
						.Bind([=](CommandContext& context)
							{
								context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
								context.SetGraphicsRootSignature(m_pCommonRS);

								context.BindRootCBV(1, Renderer::GetViewUniforms(pView, sceneTextures.pDepth->Get()));
								{
									GPU_PROFILE_SCOPE("Opaque", &context);
									context.SetPipelineState(m_pDepthPrepassOpaquePSO);
									Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
								}
								{
									GPU_PROFILE_SCOPE("Masked", &context);
									context.SetPipelineState(m_pDepthPrepassAlphaMaskPSO);
									Renderer::DrawScene(context, pView, Batch::Blending::AlphaMask);
								}
							});
				}

				if (Tweakables::g_RenderTerrain.GetBool())
					m_pCBTTessellation->Execute(graph, pView, sceneTextures);
			}

			if (Tweakables::g_SDSM)
			{
				RG_GRAPH_SCOPE("Depth Reduce", graph);

				Vector2u depthTarget = sceneTextures.pDepth->GetDesc().Size2D();
				depthTarget.x = Math::Max(depthTarget.x / 16u, 1u);
				depthTarget.y = Math::Max(depthTarget.y / 16u, 1u);
				RGTexture* pReductionTarget = graph.Create("Depth Reduction Target", TextureDesc::Create2D(depthTarget.x, depthTarget.y, ResourceFormat::RG32_FLOAT));

				graph.AddPass("Depth Reduce - Setup", RGPassFlag::Compute)
					.Read(sceneTextures.pDepth)
					.Write(pReductionTarget)
					.Bind([=](CommandContext& context)
						{
							Texture* pSource = sceneTextures.pDepth->Get();
							Texture* pTarget = pReductionTarget->Get();

							context.SetComputeRootSignature(m_pCommonRS);
							context.SetPipelineState(pSource->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO : m_pPrepareReduceDepthPSO);

							context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
							context.BindResources(2, pTarget->GetUAV());
							context.BindResources(3, pSource->GetSRV());

							context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());
						});

				for (;;)
				{
					RGTexture* pReductionSource = pReductionTarget;
					pReductionTarget = graph.Create("Depth Reduction Target", TextureDesc::Create2D(depthTarget.x, depthTarget.y, ResourceFormat::RG32_FLOAT));

					graph.AddPass("Depth Reduce - Subpass", RGPassFlag::Compute)
						.Read(pReductionSource)
						.Write(pReductionTarget)
						.Bind([=](CommandContext& context)
							{
								Texture* pTarget = pReductionTarget->Get();
								context.SetComputeRootSignature(m_pCommonRS);
								context.SetPipelineState(m_pReduceDepthPSO);
								context.BindResources(2, pTarget->GetUAV());
								context.BindResources(3, pReductionSource->Get()->GetSRV());
								context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());
							});

							if (depthTarget.x == 1 && depthTarget.y == 1)
								break;

							depthTarget.x = Math::Max(1u, depthTarget.x / 16);
							depthTarget.y = Math::Max(1u, depthTarget.y / 16);
				}

				RGBuffer* pReadbackTarget = RGUtils::CreatePersistent(graph, "SDSM Readback", BufferDesc::CreateTyped(2, ResourceFormat::RG32_FLOAT, BufferFlag::Readback), &m_ReductionReadbackTargets[m_Frame % SwapChain::NUM_FRAMES], true);
				graph.AddPass("Readback Copy", RGPassFlag::Copy | RGPassFlag::NeverCull)
					.Read(pReductionTarget)
					.Write(pReadbackTarget)
					.Bind([=](CommandContext& context)
						{
							context.CopyTexture(pReductionTarget->Get(), pReadbackTarget->Get(), CD3DX12_BOX(0, 1));
						});
			}

			m_pParticles->Simulate(graph, pView, sceneTextures.pDepth);

			if (Tweakables::g_EnableDDGI)
			{
				m_pDDGI->Execute(graph, pView, pWorldMut);
			}

			graph.AddPass("Camera Motion", RGPassFlag::Compute)
				.Read(sceneTextures.pDepth)
				.Write(sceneTextures.pVelocity)
				.Bind([=](CommandContext& context)
					{
						Texture* pVelocity = sceneTextures.pVelocity->Get();

						context.SetComputeRootSignature(m_pCommonRS);
						context.SetPipelineState(m_pCameraMotionPSO);

						context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pVelocity));
						context.BindResources(2, pVelocity->GetUAV());
						context.BindResources(3, sceneTextures.pDepth->Get()->GetSRV());

						context.Dispatch(ComputeUtils::GetNumThreadGroups(pVelocity->GetWidth(), 8, pVelocity->GetHeight(), 8));
					});

			sceneTextures.pAmbientOcclusion = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::White2D));
			if (Tweakables::g_RaytracedAO)
				m_pRTAO->Execute(graph, pView, sceneTextures);
			else
				sceneTextures.pAmbientOcclusion = m_pSSAO->Execute(graph, pView, sceneTextures);

			m_pLightCulling->ComputeClusteredLightCulling(graph, pView, lightCull3DData);

			RGTexture* pFog = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D));
			if (Tweakables::g_VolumetricFog)
			{
				pFog = m_pVolumetricFog->RenderFog(graph, pView, lightCull3DData, m_FogData);
			}

			if (m_RenderPath == RenderPath::Tiled)
			{
				m_pLightCulling->ComputeTiledLightCulling(graph, pView, sceneTextures, lightCull2DData);
				m_pForwardRenderer->RenderForwardTiled(graph, pView, sceneTextures, lightCull2DData, pFog);
			}
			else if (m_RenderPath == RenderPath::Clustered)
			{
				m_pForwardRenderer->RenderForwardClustered(graph, pView, sceneTextures, lightCull3DData, pFog);
			}
			else if (m_RenderPath == RenderPath::Visibility)
			{
				graph.AddPass("Visibility Shading", RGPassFlag::Raster)
					.Read({ pFog, rasterResult.pVisibleMeshlets})
					.Read({ rasterResult.pVisibilityBuffer, sceneTextures.pDepth, sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor })
					.Read({ lightCull3DData.pLightGrid, lightCull3DData.pLightIndexGrid })
					.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::NoAccess, false, RenderTargetLoadAction::Load)
					.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::DontCare)
					.RenderTarget(sceneTextures.pNormals, RenderTargetLoadAction::DontCare)
					.RenderTarget(sceneTextures.pRoughness, RenderTargetLoadAction::DontCare)
					.Bind([=](CommandContext& context)
						{
							Texture* pColorTarget = sceneTextures.pColorTarget->Get();

							context.SetGraphicsRootSignature(m_pCommonRS);
							context.SetPipelineState(m_pVisibilityShadingPSO);
							context.SetStencilRef(0x1);
							context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

							struct
							{
								Vector4u ClusterDimensions;
								Vector2u ClusterSize;
								Vector2 LightGridParams;
							} parameters;
							parameters.ClusterDimensions = Vector4u(lightCull3DData.ClusterCount.x, lightCull3DData.ClusterCount.y, lightCull3DData.ClusterCount.z, 0);
							parameters.ClusterSize = lightCull3DData.ClusterSize;
							parameters.LightGridParams = lightCull3DData.LightGridParams;

							context.BindRootCBV(0, parameters);
							context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pColorTarget));
							context.BindResources(3, {
								rasterResult.pVisibilityBuffer->Get()->GetSRV(),
								sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
								sceneTextures.pDepth->Get()->GetSRV(),
								sceneTextures.pPreviousColor->Get()->GetSRV(),
								pFog->Get()->GetSRV(),
								rasterResult.pVisibleMeshlets->Get()->GetSRV(),
								lightCull3DData.pLightGrid->Get()->GetSRV(),
								lightCull3DData.pLightIndexGrid->Get()->GetSRV(),

								});
							context.Draw(0, 3);
						});

				m_pForwardRenderer->RenderForwardClustered(graph, pView, sceneTextures, lightCull3DData, pFog, true);
			}

			m_pParticles->Render(graph, pView, sceneTextures);

			if (Tweakables::g_RenderTerrain.GetBool())
				m_pCBTTessellation->Shade(graph, pView, sceneTextures);

			graph.AddPass("Render Sky", RGPassFlag::Raster)
				.Read(pSky)
				.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
				.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::Load)
				.Bind([=](CommandContext& context)
					{
						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						context.SetGraphicsRootSignature(m_pCommonRS);
						context.SetPipelineState(m_pSkyboxPSO);

						context.BindRootCBV(1, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));
						context.Draw(0, 36);
					});

			if (Tweakables::g_Clouds)
			{
				m_pClouds->Render(graph, sceneTextures, pView);
			}

			TextureDesc colorDesc = sceneTextures.pColorTarget->GetDesc();
			if (colorDesc.SampleCount > 1)
			{
				colorDesc.SampleCount = 1;
				RGTexture* pResolveColor = graph.Create("Resolved Color", colorDesc);
				RGUtils::AddResolvePass(graph, sceneTextures.pColorTarget, pResolveColor);
				sceneTextures.pColorTarget = pResolveColor;
			}

			if (Tweakables::g_RaytracedReflections)
			{
				m_pRTReflections->Execute(graph, pView, sceneTextures);
			}

			if (Tweakables::g_TAA.Get())
			{
				RGTexture* pTaaTarget = graph.Create("TAA Target", sceneTextures.pColorTarget->GetDesc());

				graph.AddPass("Temporal Resolve", RGPassFlag::Compute)
					.Read({ sceneTextures.pVelocity, sceneTextures.pDepth, sceneTextures.pColorTarget, sceneTextures.pPreviousColor })
					.Write(pTaaTarget)
					.Bind([=](CommandContext& context)
						{
							Texture* pTarget = pTaaTarget->Get();
							context.SetComputeRootSignature(m_pCommonRS);
							context.SetPipelineState(m_pTemporalResolvePSO);

							struct
							{
								float MinBlendFactor;
							} params;
							params.MinBlendFactor = pView->CameraCut ? 1.0f : 0.0f;

							context.BindRootCBV(0, params);
							context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
							context.BindResources(2, pTarget->GetUAV());
							context.BindResources(3,
								{
									sceneTextures.pVelocity->Get()->GetSRV(),
									sceneTextures.pPreviousColor->Get()->GetSRV(),
									sceneTextures.pColorTarget->Get()->GetSRV(),
									sceneTextures.pDepth->Get()->GetSRV(),
								});

							context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
						});

				sceneTextures.pColorTarget = pTaaTarget;
			}

			// Probes contain irradiance data, and need to go through tonemapper.
			if (Tweakables::g_VisualizeDDGI)
			{
				m_pDDGI->RenderVisualization(graph, pView, pWorldMut, sceneTextures);
			}
		}
		else
		{
			m_pPathTracing->Render(graph, pView, sceneTextures.pColorTarget);
		}

		graph.Export(sceneTextures.pColorTarget, &m_pColorHistory, TextureFlag::ShaderResource);

		/*
			Post Processing
		*/

		RGBuffer* pAverageLuminance = RGUtils::CreatePersistent(graph, "Average Luminance", BufferDesc::CreateStructured(3, sizeof(float)), &m_pAverageLuminance, true);
		{
			RG_GRAPH_SCOPE("Eye Adaptation", graph);

			TextureDesc sourceDesc = sceneTextures.pColorTarget->GetDesc();
			sourceDesc.Width = Math::DivideAndRoundUp(sourceDesc.Width, 4);
			sourceDesc.Height = Math::DivideAndRoundUp(sourceDesc.Height, 4);
			RGTexture* pDownscaleTarget = graph.Create("Downscaled HDR Target", sourceDesc);

			graph.AddPass("Downsample Color", RGPassFlag::Compute)
				.Read(sceneTextures.pColorTarget)
				.Write(pDownscaleTarget)
				.Bind([=](CommandContext& context)
					{
						Texture* pTarget = pDownscaleTarget->Get();

						context.SetComputeRootSignature(m_pCommonRS);
						context.SetPipelineState(m_pGenerateMipsPSO);

						struct
						{
							Vector2i TargetDimensions;
							Vector2 TargetDimensionsInv;
						} parameters;
						parameters.TargetDimensions.x = pTarget->GetWidth();
						parameters.TargetDimensions.y = pTarget->GetHeight();
						parameters.TargetDimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

						context.BindRootCBV(0, parameters);
						context.BindResources(2, pTarget->GetUAV());
						context.BindResources(3, sceneTextures.pColorTarget->Get()->GetSRV());

						context.Dispatch(ComputeUtils::GetNumThreadGroups(parameters.TargetDimensions.x, 8, parameters.TargetDimensions.y, 8));
					});

			RGBuffer* pLuminanceHistogram = graph.Create("Luminance Histogram", BufferDesc::CreateByteAddress(sizeof(uint32) * 256));
			graph.AddPass("Luminance Histogram", RGPassFlag::Compute)
				.Read(pDownscaleTarget)
				.Write(pLuminanceHistogram)
				.Bind([=](CommandContext& context)
					{
						Texture* pColorSource = pDownscaleTarget->Get();
						Buffer* pHistogram = pLuminanceHistogram->Get();

						context.ClearUAVu(pHistogram->GetUAV());
						context.InsertUAVBarrier(pHistogram);

						context.SetComputeRootSignature(m_pCommonRS);
						context.SetPipelineState(m_pLuminanceHistogramPSO);

						struct
						{
							uint32 Width;
							uint32 Height;
							float MinLogLuminance;
							float OneOverLogLuminanceRange;
						} parameters;
						parameters.Width = pColorSource->GetWidth();
						parameters.Height = pColorSource->GetHeight();
						parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
						parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get());

						context.BindRootCBV(0, parameters);
						context.BindResources(2, pHistogram->GetUAV());
						context.BindResources(3, pColorSource->GetSRV());

						context.Dispatch(ComputeUtils::GetNumThreadGroups(pColorSource->GetWidth(), 16, pColorSource->GetHeight(), 16));
					});

			uint32 numPixels = sourceDesc.Width * sourceDesc.Height;

			graph.AddPass("Average Luminance", RGPassFlag::Compute)
				.Read(pLuminanceHistogram)
				.Write(pAverageLuminance)
				.Bind([=](CommandContext& context)
					{
						context.SetComputeRootSignature(m_pCommonRS);
						context.SetPipelineState(m_pAverageLuminancePSO);

						struct
						{
							int32 PixelCount;
							float MinLogLuminance;
							float LogLuminanceRange;
							float TimeDelta;
							float Tau;
						} parameters;

						parameters.PixelCount = numPixels;
						parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
						parameters.LogLuminanceRange = Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get();
						parameters.TimeDelta = Time::DeltaTime();
						parameters.Tau = Tweakables::g_Tau.Get();

						context.BindRootCBV(0, parameters);
						context.BindResources(2, pAverageLuminance->Get()->GetUAV());
						context.BindResources(3, pLuminanceHistogram->Get()->GetSRV());

						context.Dispatch(1);
					});

			if (Tweakables::g_DrawHistogram.Get())
			{
				RGTexture* pHistogramDebugTexture = RGUtils::CreatePersistent(graph, "Debug Histogram", TextureDesc::Create2D(256 * 4, 256, ResourceFormat::RGBA8_UNORM, 1, TextureFlag::ShaderResource), &m_pDebugHistogramTexture, true);
				graph.AddPass("Draw Histogram", RGPassFlag::Compute)
					.Read({ pLuminanceHistogram, pAverageLuminance })
					.Write(pHistogramDebugTexture)
					.Bind([=](CommandContext& context)
						{
							context.ClearUAVf(pHistogramDebugTexture->Get()->GetUAV());
							context.InsertUAVBarrier(pHistogramDebugTexture->Get());

							context.SetPipelineState(m_pDrawHistogramPSO);
							context.SetComputeRootSignature(m_pCommonRS);

							struct
							{
								float MinLogLuminance;
								float InverseLogLuminanceRange;
								Vector2 InvTextureDimensions;
							} parameters;

							parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
							parameters.InverseLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get());
							parameters.InvTextureDimensions.x = 1.0f / pHistogramDebugTexture->GetDesc().Width;
							parameters.InvTextureDimensions.y = 1.0f / pHistogramDebugTexture->GetDesc().Height;

							context.BindRootCBV(0, parameters);
							context.BindResources(2, pHistogramDebugTexture->Get()->GetUAV());
							context.BindResources(3, {
								pLuminanceHistogram->Get()->GetSRV(),
								pAverageLuminance->Get()->GetSRV(),
								});

							context.Dispatch(1, pLuminanceHistogram->Get()->GetNumElements());
						});
			}
		}

		RGTexture* pBloomTexture = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));

		if (Tweakables::g_Bloom.Get())
		{
			RG_GRAPH_SCOPE("Bloom", graph);

			auto ComputeNumMips = [](uint32 width, uint32 height) -> uint32
			{
				return (uint32)Math::Floor(log2f((float)Math::Max(width, height))) + 1u;
			};

			Vector2u bloomDimensions = Vector2u(viewDimensions.x >> 1, viewDimensions.y >> 1);
			const uint32 mipBias = 3;
			uint32 numMips = ComputeNumMips(bloomDimensions.x, bloomDimensions.y) - mipBias;
			RGTexture* pDownscaleTarget = graph.Create("Downscale Target", TextureDesc::Create2D(bloomDimensions.x, bloomDimensions.y, ResourceFormat::RGBA16_FLOAT, numMips));

			RGTexture* pSourceTexture = sceneTextures.pColorTarget;
			for (uint32 i = 0; i < numMips; ++i)
			{
				Vector2u targetDimensions(Math::Max(1u, bloomDimensions.x >> i), Math::Max(1u, bloomDimensions.y >> i));
				graph.AddPass(Sprintf("Downsample %d [%dx%d > %dx%d]", i, targetDimensions.x << 1, targetDimensions.y << 1, targetDimensions.x, targetDimensions.y).c_str(), RGPassFlag::Compute)
					.Write(pDownscaleTarget)
					.Bind([=](CommandContext& context)
						{
							context.SetComputeRootSignature(m_pCommonRS);
							context.SetPipelineState(i == 0 ? m_pBloomDownsampleKarisAveragePSO : m_pBloomDownsamplePSO);
							struct
							{
								Vector2 TargetDimensionsInv;
								uint32 SourceMip;
							} parameters;
							parameters.TargetDimensionsInv = Vector2(1.0f / targetDimensions.x, 1.0f / targetDimensions.y);
							parameters.SourceMip = i == 0 ? 0 : i - 1;

							context.BindRootCBV(0, parameters);
							context.BindResources(2, pDownscaleTarget->Get()->GetSubResourceUAV(i));
							context.BindResources(3, pSourceTexture->Get()->GetSRV());
							context.Dispatch(ComputeUtils::GetNumThreadGroups(targetDimensions.x, 8, targetDimensions.y, 8));
							context.InsertUAVBarrier();
						});

				pSourceTexture = pDownscaleTarget;
			}

			numMips = Math::Max(2u, numMips);
			RGTexture* pUpscaleTarget = graph.Create("Upscale Target", TextureDesc::Create2D(bloomDimensions.x, bloomDimensions.y, ResourceFormat::RGBA16_FLOAT, numMips - 1));
			RGTexture* pPreviousSource = pDownscaleTarget;

			for (int32 i = numMips - 2; i >= 0; --i)
			{
				Vector2u targetDimensions(Math::Max(1u, bloomDimensions.x >> i), Math::Max(1u, bloomDimensions.y >> i));
				graph.AddPass(Sprintf("UpsampleCombine %d [%dx%d > %dx%d]", numMips - 2 - i, Math::Max(1u, targetDimensions.x >> 1), Math::Max(1u, targetDimensions.y >> 1), targetDimensions.x, targetDimensions.y).c_str(), RGPassFlag::Compute)
					.Read(pDownscaleTarget)
					.Write(pUpscaleTarget)
					.Bind([=](CommandContext& context)
						{
							context.SetComputeRootSignature(m_pCommonRS);
							context.SetPipelineState(m_pBloomUpsamplePSO);
							struct
							{
								Vector2 TargetDimensionsInv;
								uint32 SourceCurrentMip;
								uint32 SourcePreviousMip;
								float Radius;
							} parameters;
							parameters.TargetDimensionsInv = Vector2(1.0f / targetDimensions.x, 1.0f / targetDimensions.y);
							parameters.SourceCurrentMip = i;
							parameters.SourcePreviousMip = i + 1;
							parameters.Radius = Tweakables::g_BloomInteralBlendFactor;

							context.BindRootCBV(0, parameters);
							context.BindResources(2, pUpscaleTarget->Get()->GetSubResourceUAV(i));
							context.BindResources(3, {
								pDownscaleTarget->Get()->GetSRV(),
								pPreviousSource->Get()->GetSRV(),
								});
							context.Dispatch(ComputeUtils::GetNumThreadGroups(targetDimensions.x, 8, targetDimensions.y, 8));
							context.InsertUAVBarrier();
						});

				pPreviousSource = pUpscaleTarget;
			}

			pBloomTexture = pUpscaleTarget;
		}

		RGTexture* pTonemapTarget = graph.Create("Tonemap Target", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RGBA8_UNORM));

		graph.AddPass("Tonemap", RGPassFlag::Compute)
			.Read({ sceneTextures.pColorTarget, pAverageLuminance, pBloomTexture })
			.Write(pTonemapTarget)
			.Bind([=](CommandContext& context)
				{
					Texture* pTarget = pTonemapTarget->Get();

					struct
					{
						float WhitePoint;
						uint32 Tonemapper;
						float BloomIntensity;
						float BloomBlendFactor;
						Vector3 LensDirtTint;
					} parameters;
					parameters.WhitePoint = Tweakables::g_WhitePoint.Get();
					parameters.Tonemapper = Tweakables::g_ToneMapper.Get();
					parameters.BloomIntensity = Tweakables::g_BloomIntensity.Get();
					parameters.BloomBlendFactor = Tweakables::g_BloomBlendFactor.Get();
					parameters.LensDirtTint = m_LensDirtTint;

					context.SetPipelineState(m_pToneMapPSO);
					context.SetComputeRootSignature(m_pCommonRS);

					context.BindRootCBV(0, parameters);
					context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
					context.BindResources(2, pTarget->GetUAV());
					context.BindResources(3, {
						sceneTextures.pColorTarget->Get()->GetSRV(),
						pAverageLuminance->Get()->GetSRV(),
						pBloomTexture->Get()->GetSRV(),
						m_pLensDirtTexture->GetSRV(),
						});
					context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
				});

		sceneTextures.pColorTarget = pTonemapTarget;

		/*
			Debug Views
		*/

		if (m_RenderPath != RenderPath::PathTracing)
		{
			if (Tweakables::g_VisualizeLightDensity)
			{
				if (m_RenderPath == RenderPath::Clustered || m_RenderPath == RenderPath::Visibility)
					m_pLightCulling->VisualizeLightDensity(graph, pView, sceneTextures, lightCull3DData);
				else if (m_RenderPath == RenderPath::Tiled)
					m_pLightCulling->VisualizeLightDensity(graph, m_pDevice, pView, sceneTextures, lightCull2DData);
			}

			if (m_RenderPath == RenderPath::Visibility && m_VisibilityDebugRenderMode > 0)
			{
				graph.AddPass("Visibility Debug Render", RGPassFlag::Compute)
					.Read({ rasterResult.pVisibilityBuffer, rasterResult.pVisibleMeshlets, rasterResult.pDebugData })
					.Write({ sceneTextures.pColorTarget })
					.Bind([=](CommandContext& context)
						{
							Texture* pColorTarget = sceneTextures.pColorTarget->Get();

							context.SetComputeRootSignature(m_pCommonRS);
							context.SetPipelineState(m_pVisibilityDebugRenderPSO);

							uint32 mode = m_VisibilityDebugRenderMode;
							context.BindRootCBV(0, mode);
							context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pColorTarget));
							context.BindResources(2, pColorTarget->GetUAV());
							context.BindResources(3, {
								rasterResult.pVisibilityBuffer->Get()->GetSRV(),
								rasterResult.pVisibleMeshlets->Get()->GetSRV(),
								rasterResult.pDebugData->Get()->GetSRV(),
								});
							context.Dispatch(ComputeUtils::GetNumThreadGroups(pColorTarget->GetWidth(), 8, pColorTarget->GetHeight(), 8));
						});
			}
		}

		DebugRenderer::Get()->Render(graph, pView, sceneTextures.pColorTarget, sceneTextures.pDepth);
		m_pShaderDebugRenderer->Render(graph, pView, sceneTextures.pColorTarget, sceneTextures.pDepth);

		if (!Tweakables::VisualizeTextureName.empty())
		{
			RGTexture* pVisualizeTexture = graph.FindTexture(Tweakables::VisualizeTextureName.c_str());
			m_pVisualizeTexture->Capture(graph, pVisualizeTexture);
		}

		graph.Export(sceneTextures.pColorTarget, &m_pColorOutput, TextureFlag::ShaderResource);

		/*
			UI & Present
		*/

		ImGuiRenderer::Render(graph, graph.Import(m_pSwapchain->GetBackBuffer()));

		if (Tweakables::g_DumpRenderGraph)
		{
			graph.DumpGraph(Sprintf("%sRenderGraph_%s.html", Paths::SavedDir(), Utils::GetTimeString()).c_str());
			Tweakables::g_DumpRenderGraph = false;
		}
		if(Tweakables::g_EnableRenderGraphResourceTracker)
			graph.EnableResourceTrackerView();

		{
			GPU_PROFILE_SCOPE("Render", pContext);
			graph.Execute(pContext);
		}
		
	}

	pContext->InsertResourceBarrier(m_pSwapchain->GetBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
	pContext->Execute();

	{
		PROFILE_SCOPE("Present");
		m_pSwapchain->Present();
		m_pDevice->TickFrame();
		++m_Frame;
		m_SceneData.CameraCut = false;
	}
}

void DemoApp::OnResizeOrMove(int width, int height)
{
	E_LOG(Info, "Window resized: %dx%d", width, height);
	m_pSwapchain->OnResizeOrMove(width, height);
}

void DemoApp::OnResizeViewport(int width, int height)
{
	E_LOG(Info, "Viewport resized: %dx%d", width, height);
	m_pCamera->SetViewport(FloatRect(0, 0, (float)width, (float)height));
	m_SceneData.CameraCut = true;
}

void DemoApp::InitializePipelines()
{
	// Common Root Signature - Make it 12 DWORDs as is often recommended by IHVs
	m_pCommonRS = new RootSignature(m_pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 16, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 64, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Common");

	//Shadow mapping - Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("DepthOnly.hlsl", "VSMain");
		psoDesc.SetDepthOnlyTarget(GraphicsCommon::ShadowFormat, 1);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthBias(-10, 0, -4.0f);
		psoDesc.SetName("Shadow Mapping Opaque");
		m_pShadowsOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetPixelShader("DepthOnly.hlsl", "PSMain");
		psoDesc.SetName("Shadow Mapping Alpha Mask");
		m_pShadowsAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Depth prepass - Simple vertex shader to fill the depth buffer to optimize later passes
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("DepthOnly.hlsl", "VSMain");
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthOnlyTarget(GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetName("Depth Prepass Opaque");
		m_pDepthPrepassOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetPixelShader("DepthOnly.hlsl", "PSMain");
		psoDesc.SetName("Depth Prepass Alpha Mask");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDepthPrepassAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	ShaderDefineHelper tonemapperDefines;
	tonemapperDefines.Set("NUM_HISTOGRAM_BINS", 256);
	m_pLuminanceHistogramPSO		= m_pDevice->CreateComputePipeline(m_pCommonRS, "LuminanceHistogram.hlsl", "CSMain", *tonemapperDefines);
	m_pDrawHistogramPSO				= m_pDevice->CreateComputePipeline(m_pCommonRS, "DrawLuminanceHistogram.hlsl", "DrawLuminanceHistogram", *tonemapperDefines);
	m_pAverageLuminancePSO			= m_pDevice->CreateComputePipeline(m_pCommonRS, "AverageLuminance.hlsl", "CSMain", *tonemapperDefines);
	m_pToneMapPSO					= m_pDevice->CreateComputePipeline(m_pCommonRS, "PostProcessing/Tonemapping.hlsl", "CSMain", *tonemapperDefines);

	//Depth resolve
	m_pResolveDepthPSO				= m_pDevice->CreateComputePipeline(m_pCommonRS, "ResolveDepth.hlsl", "CSMain", { "DEPTH_RESOLVE_MIN" });
	m_pPrepareReduceDepthPSO		= m_pDevice->CreateComputePipeline(m_pCommonRS, "ReduceDepth.hlsl", "PrepareReduceDepth");
	m_pPrepareReduceDepthMsaaPSO	= m_pDevice->CreateComputePipeline(m_pCommonRS, "ReduceDepth.hlsl", "PrepareReduceDepth", { "WITH_MSAA" });
	m_pReduceDepthPSO				= m_pDevice->CreateComputePipeline(m_pCommonRS, "ReduceDepth.hlsl", "ReduceDepth");

	m_pCameraMotionPSO				= m_pDevice->CreateComputePipeline(m_pCommonRS, "CameraMotionVectors.hlsl", "CSMain");
	m_pTemporalResolvePSO			= m_pDevice->CreateComputePipeline(m_pCommonRS, "PostProcessing/TemporalResolve.hlsl", "CSMain");

	m_pGenerateMipsPSO				= m_pDevice->CreateComputePipeline(m_pCommonRS, "GenerateMips.hlsl", "CSMain");

	//Sky
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("ProceduralSky.hlsl", "VSMain");
		psoDesc.SetPixelShader("ProceduralSky.hlsl", "PSMain");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA16_FLOAT, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pRenderSkyPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "ProceduralSky.hlsl", "ComputeSkyCS");
	}

	//Bloom
	m_pBloomDownsamplePSO				= m_pDevice->CreateComputePipeline(m_pCommonRS, "PostProcessing/Bloom.hlsl", "DownsampleCS");
	m_pBloomDownsampleKarisAveragePSO	= m_pDevice->CreateComputePipeline(m_pCommonRS, "PostProcessing/Bloom.hlsl", "DownsampleCS", {"KARIS_AVERAGE=1"});
	m_pBloomUpsamplePSO					= m_pDevice->CreateComputePipeline(m_pCommonRS, "PostProcessing/Bloom.hlsl", "UpsampleCS");

	//Visibility Shading
	{
		constexpr ResourceFormat formats[] = {
			ResourceFormat::RGBA16_FLOAT,
			ResourceFormat::RG16_FLOAT,
			ResourceFormat::R8_UNORM,
		};

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("FullScreenTriangle.hlsl", "WithTexCoordVS");
		psoDesc.SetPixelShader("VisibilityShading.hlsl", "ShadePS");
		psoDesc.SetRenderTargetFormats(formats, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_EQUAL, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_ZERO, D3D12_STENCIL_OP_KEEP, 0x2, 0x0);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("Visibility Shading");
		m_pVisibilityShadingPSO = m_pDevice->CreatePipeline(psoDesc);
	}
	m_pVisibilityDebugRenderPSO			= m_pDevice->CreateComputePipeline(m_pCommonRS, "VisibilityDebugView.hlsl", "DebugRenderCS");
}



void DemoApp::UpdateImGui()
{
	PROFILE_SCOPE("ImGui Update");
	ImGuiRenderer::NewFrame();

	m_FrameHistory.AddTime(Time::DeltaTime());

	static ImGuiConsole console;
	static bool showProfiler = false;
	static bool showImguiDemo = false;

	ImGuiViewport* pViewport = ImGui::GetMainViewport();
	ImGuiID dockspace = ImGui::DockSpaceOverViewport(pViewport);

	if (!ImGui::FindWindowSettingsByID(ImHashStr("ViewportSettings")))
	{
		ImGui::CreateNewWindowSettings("ViewportSettings");
		ImGuiID viewportID, parametersID;
		ImGui::DockBuilderRemoveNode(dockspace);
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_CentralNode);
		ImGui::DockBuilderSetNodeSize(dockspace, pViewport->Size);
		ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Right, 0.2f, &parametersID, &viewportID);
		ImGui::DockBuilderDockWindow("Parameters", parametersID);
		ImGui::DockBuilderGetNode(viewportID)->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;
		ImGui::DockBuilderGetNode(viewportID)->UpdateMergedFlags();
		ImGui::DockBuilderDockWindow(ICON_FA_DESKTOP " Viewport", viewportID);
		ImGui::DockBuilderFinish(dockspace);
	}

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu(ICON_FA_FILE " File"))
		{
			if (ImGui::MenuItem(ICON_FA_FILE " Load Mesh", nullptr, nullptr))
			{
				OPENFILENAME ofn{};
				TCHAR szFile[260]{};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = m_Window;
				ofn.lpstrFile = szFile;
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "Supported files (*.gltf;*.glb;*.dat;*.ldr;*.mpd)\0*.gltf;*.glb;*.dat;*.ldr;*.mpd\0All Files (*.*)\0*.*\0";;
				ofn.nFilterIndex = 1;
				ofn.lpstrFileTitle = NULL;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = NULL;
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameA(&ofn) == TRUE)
				{
					m_World.Meshes.clear();
					CommandContext* pContext = m_pDevice->AllocateCommandContext();
					LoadMesh(ofn.lpstrFile, *pContext, m_World);
					pContext->Execute();
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_WINDOW_MAXIMIZE " Windows"))
		{
			if (ImGui::MenuItem(ICON_FA_CLOCK_O " Profiler", 0, showProfiler))
			{
				showProfiler = !showProfiler;
			}
			bool& showConsole = console.IsVisible();
			if (ImGui::MenuItem("Output Log", 0, showConsole))
			{
				showConsole = !showConsole;
			}
			if (ImGui::MenuItem("Luminance Histogram", 0, &Tweakables::g_DrawHistogram.Get()))
			{
				Tweakables::g_DrawHistogram.Set(!Tweakables::g_DrawHistogram.GetBool());
			}

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_WRENCH " Tools"))
		{
			if (ImGui::MenuItem("Dump RenderGraph"))
			{
				Tweakables::g_DumpRenderGraph = true;
			}
			if (ImGui::MenuItem("RenderGraph Resource Tracker"))
			{
				Tweakables::g_EnableRenderGraphResourceTracker = true;
			}
			if (ImGui::MenuItem("Screenshot"))
			{
				Tweakables::g_Screenshot = true;
			}
			if (ImGui::MenuItem("Pix Capture"))
			{
				D3D::EnqueuePIXCapture();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_QUESTION " Help"))
		{
			if (ImGui::MenuItem("ImGui Demo", 0, showImguiDemo))
			{
				showImguiDemo = !showImguiDemo;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}


	ImGui::Begin(ICON_FA_DESKTOP " Viewport", 0, ImGuiWindowFlags_NoScrollbar);
	ImVec2 viewportPos = ImGui::GetWindowPos();
	ImVec2 viewportSize = ImGui::GetWindowSize();
	ImVec2 imageSize = ImMax(ImGui::GetContentRegionAvail(), ImVec2(16.0f, 16.0f));
	if (m_pColorOutput)
	{
		if (imageSize.x != m_pColorOutput->GetWidth() || imageSize.y != m_pColorOutput->GetHeight())
		{
			OnResizeViewport((int)imageSize.x, (int)imageSize.y);
		}
		ImGui::Image(m_pColorOutput, imageSize);
	}
	ImVec2 viewportOrigin = ImGui::GetItemRectMin();
	ImVec2 viewportExtents = ImGui::GetItemRectSize();

	ImGui::End();

	ImGuizmo::SetRect(viewportOrigin.x, viewportOrigin.y, viewportExtents.x, viewportExtents.y);
	m_pVisualizeTexture->RenderUI(viewportOrigin, viewportExtents);

	console.Update();

	if (showImguiDemo)
	{
		ImGui::ShowDemoWindow();
	}

	if (Tweakables::g_DrawHistogram && m_pDebugHistogramTexture)
	{
		ImGui::Begin("Luminance Histogram");
		ImVec2 cursor = ImGui::GetCursorPos();
		ImVec2 size = ImGui::GetAutoSize(ImVec2((float)m_pDebugHistogramTexture->GetWidth(), (float)m_pDebugHistogramTexture->GetHeight()));
		ImGui::Image(m_pDebugHistogramTexture, size);
		ImGui::GetWindowDrawList()->AddText(cursor, IM_COL32(255, 255, 255, 255), Sprintf("%.2f", Tweakables::g_MinLogLuminance.Get()).c_str());
		ImGui::End();
	}

	if (Tweakables::g_VisualizeShadowCascades)
	{
		if (ImGui::Begin("Shadow Cascades"))
		{
			const Light& sunLight = m_World.Lights[0];
			for (int i = 0; i < Tweakables::g_ShadowCascades; ++i)
			{
				ImGui::Image(sunLight.ShadowMaps[i], ImVec2(230, 230));
				ImGui::SameLine();
			}
		}
		ImGui::End();
	}

	if (showProfiler)
	{
		if (ImGui::Begin("Profiler", &showProfiler))
		{
			ImGui::Text("MS: %4.2f | FPS: %4.2f | %d x %d", Time::DeltaTime() * 1000.0f, 1.0f / Time::DeltaTime(), m_SceneData.GetDimensions().x, m_SceneData.GetDimensions().y);

			const float* pHistoryData;
			uint32 historySize, historyOffset;
			m_FrameHistory.GetHistory(&pHistoryData, &historySize, &historyOffset);
			ImGui::PlotLines("", pHistoryData, (int)historySize, historyOffset, 0, 0.0f, 0.03f, ImVec2(ImGui::GetContentRegionAvail().x, 100));

			if (ImGui::TreeNodeEx("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
			{
				Profiler::Get()->DrawImGui();
				ImGui::TreePop();
			}
		}
		ImGui::End();
	}

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("General"))
		{
			if (ImGui::Checkbox("Vertical Sync", &Tweakables::g_Vsync.Get()))
			{
				m_pSwapchain->SetVSync(Tweakables::g_Vsync);
			}

			static constexpr const char* pPathNames[] =
			{
				"Tiled",
				"Clustered",
				"Path Tracing",
				"Visibility",
			};
			ImGui::Combo("Render Path", (int*)&m_RenderPath, pPathNames, ARRAYSIZE(pPathNames));

			if (m_RenderPath == RenderPath::Visibility)
			{
				ImGui::Checkbox("Occlusion Culling", &Tweakables::g_OcclusionCulling.Get());
				static constexpr const char* pDebugViewNames[] =
				{
					"Off",
					"InstanceID",
					"MeshletID",
					"PrimitiveID",
					"Overdraw",
				};
				ImGui::Combo("VisBuffer Debug View", (int*)&m_VisibilityDebugRenderMode, pDebugViewNames, ARRAYSIZE(pDebugViewNames));

				ImGui::Checkbox("Cull statistics", &Tweakables::CullDebugStats.Get());
			}

			ImGui::Text("Camera");
			ImGui::Text("Location: [%.2f, %.2f, %.2f]", m_pCamera->GetPosition().x, m_pCamera->GetPosition().y, m_pCamera->GetPosition().z);
			float fov = m_pCamera->GetFoV();
			if (ImGui::SliderAngle("Field of View", &fov, 10, 120))
			{
				m_pCamera->SetFoV(fov);
			}
			Vector2 farNear(m_pCamera->GetFar(), m_pCamera->GetNear());
			if (ImGui::DragFloatRange2("Near/Far", &farNear.x, &farNear.y, 1, 0.1f, 100))
			{
				m_pCamera->SetFarPlane(farNear.x);
				m_pCamera->SetNearPlane(farNear.y);
			}
		}

		if (ImGui::CollapsingHeader("Atmosphere"))
		{
			ImGui::SliderFloat("Sun Orientation", &Tweakables::g_SunOrientation, -Math::PI, Math::PI);
			ImGui::SliderFloat("Sun Inclination", &Tweakables::g_SunInclination, 0, 1);
			ImGui::SliderFloat("Sun Temperature", &Tweakables::g_SunTemperature, 1000, 15000);
			ImGui::SliderFloat("Sun Intensity", &Tweakables::g_SunIntensity, 0, 30);
			ImGui::Checkbox("Sky", &Tweakables::g_Sky.Get());
			ImGui::Checkbox("Volumetric Fog", &Tweakables::g_VolumetricFog.Get());
			ImGui::Checkbox("Clouds", &Tweakables::g_Clouds.Get());
		}

		if (ImGui::CollapsingHeader("Shadows"))
		{
			ImGui::SliderInt("Shadow Cascades", &Tweakables::g_ShadowCascades.Get(), 1, 4);
			ImGui::Checkbox("SDSM", &Tweakables::g_SDSM.Get());
			ImGui::SliderFloat("PSSM Factor", &Tweakables::g_PSSMFactor.Get(), 0, 1);
			ImGui::Checkbox("Visualize Cascades", &Tweakables::g_VisualizeShadowCascades.Get());
			ImGui::Checkbox("GPU Cull", &Tweakables::g_ShadowsGPUCull.Get());
			ImGui::Checkbox("GPU Occlusion Cull", &Tweakables::g_ShadowsOcclusionCulling.Get());
		}
		if (ImGui::CollapsingHeader("Bloom"))
		{
			ImGui::Checkbox("Enabled", &Tweakables::g_Bloom.Get());
			ImGui::SliderFloat("Intensity", &Tweakables::g_BloomIntensity.Get(), 0.0f, 4.0f);
			ImGui::SliderFloat("Blend Factor", &Tweakables::g_BloomBlendFactor.Get(), 0.0f, 1.0f);
			ImGui::SliderFloat("Internal Blend Factor", &Tweakables::g_BloomInteralBlendFactor.Get(), 0.0f, 1.0f);
			ImGui::ColorEdit3("Lens Dirt Tint", &m_LensDirtTint.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		}
		if (ImGui::CollapsingHeader("Exposure/Tonemapping"))
		{
			ImGui::DragFloatRange2("Log Luminance", &Tweakables::g_MinLogLuminance.Get(), &Tweakables::g_MaxLogLuminance.Get(), 1.0f, -100, 50);
			ImGui::Checkbox("Draw Exposure Histogram", &Tweakables::g_DrawHistogram.Get());
			ImGui::SliderFloat("White Point", &Tweakables::g_WhitePoint.Get(), 0, 20);
			ImGui::SliderFloat("Tau", &Tweakables::g_Tau.Get(), 0, 5);

			static constexpr const char* pTonemapperNames[] = {
				"Reinhard",
				"Reinhard Extended",
				"ACES Fast",
				"Unreal 3",
				"Uncharted 2",
			};
			ImGui::Combo("Tonemapper", (int*)&Tweakables::g_ToneMapper.Get(), pTonemapperNames, ARRAYSIZE(pTonemapperNames));
		}

		if (ImGui::CollapsingHeader("Misc"))
		{
			ImGui::Checkbox("TAA", &Tweakables::g_TAA.Get());
			ImGui::Checkbox("Debug Render Lights", &Tweakables::g_VisualizeLights.Get());
			ImGui::Checkbox("Visualize Light Density", &Tweakables::g_VisualizeLightDensity.Get());
			ImGui::SliderInt("SSR Samples", &Tweakables::g_SsrSamples.Get(), 0, 32);
			ImGui::Checkbox("Object Bounds", &Tweakables::g_RenderObjectBounds.Get());
			ImGui::Checkbox("Render Terrain", &Tweakables::g_RenderTerrain.Get());
		}

		if (ImGui::CollapsingHeader("Raytracing"))
		{
			if (m_pDevice->GetCapabilities().SupportsRaytracing())
			{
				ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO.Get());
				ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections.Get());
				ImGui::Checkbox("DDGI", &Tweakables::g_EnableDDGI.Get());
				if (m_World.DDGIVolumes.size() > 0)
					ImGui::SliderInt("DDGI RayCount", &m_World.DDGIVolumes.front().NumRays, 1, m_World.DDGIVolumes.front().MaxNumRays);
				ImGui::Checkbox("Visualize DDGI", &Tweakables::g_VisualizeDDGI.Get());
				ImGui::SliderAngle("TLAS Bounds Threshold", &Tweakables::g_TLASBoundsThreshold.Get(), 0, 40);
			}
		}
	}
	ImGui::End();
}

void DemoApp::LoadMesh(const std::string& filePath, CommandContext& context, World& world)
{
	std::unique_ptr<Mesh> pMesh = std::make_unique<Mesh>();
	pMesh->Load(filePath.c_str(), m_pDevice, &context, 1.0f);
	world.Meshes.push_back(std::move(pMesh));
}

void DemoApp::CreateShadowViews(SceneView& view, World& world)
{
	PROFILE_SCOPE("Shadow Setup");

	float minPoint = 0;
	float maxPoint = 1;

	const uint32 numCascades = Tweakables::g_ShadowCascades;
	const float pssmLambda = Tweakables::g_PSSMFactor;
	view.NumShadowCascades = numCascades;

	if (Tweakables::g_SDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % SwapChain::NUM_FRAMES];
		if (pSourceBuffer)
		{
			Vector2* pData = (Vector2*)pSourceBuffer->GetMappedData();
			minPoint = pData->x;
			maxPoint = pData->y;
		}
	}

	float n = m_pCamera->GetNear();
	float f = m_pCamera->GetFar();
	float nearPlane = Math::Min(n, f);
	float farPlane = Math::Max(n, f);
	float clipPlaneRange = farPlane - nearPlane;

	float minZ = nearPlane + minPoint * clipPlaneRange;
	float maxZ = nearPlane + maxPoint * clipPlaneRange;

	constexpr uint32 MAX_CASCADES = 4;
	std::array<float, MAX_CASCADES> cascadeSplits{};

	for (uint32 i = 0; i < numCascades; ++i)
	{
		float p = (i + 1) / (float)numCascades;
		float log = minZ * std::pow(maxZ / minZ, p);
		float uniform = minZ + (maxZ - minZ) * p;
		float d = pssmLambda * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
	}

	int32 shadowIndex = 0;
	view.ShadowViews.clear();
	auto AddShadowView = [&](Light& light, ShadowView shadowView, uint32 resolution, uint32 shadowMapLightIndex)
	{
		if (shadowMapLightIndex == 0)
			light.MatrixIndex = shadowIndex;
		if (shadowIndex >= (int32)m_ShadowMaps.size())
			m_ShadowMaps.push_back(m_pDevice->CreateTexture(TextureDesc::Create2D(resolution, resolution, GraphicsCommon::ShadowFormat, 1, TextureFlag::DepthStencil | TextureFlag::ShaderResource, ClearBinding(0.0f, 0)), Sprintf("Shadow Map %d", (uint32)m_ShadowMaps.size()).c_str()));
		RefCountPtr<Texture> pTarget = m_ShadowMaps[shadowIndex];

		light.ShadowMaps.resize(Math::Max(shadowMapLightIndex + 1, (uint32)light.ShadowMaps.size()));
		light.ShadowMaps[shadowMapLightIndex] = pTarget;
		light.ShadowMapSize = resolution;
		shadowView.pDepthTexture = pTarget;
		shadowView.pLight = &light;
		shadowView.ViewIndex = shadowMapLightIndex;
		shadowView.View.Viewport = FloatRect(0, 0, (float)resolution, (float)resolution);
		view.ShadowViews.push_back(shadowView);
		shadowIndex++;
	};

	for (size_t lightIndex = 0; lightIndex < world.Lights.size(); ++lightIndex)
	{
		Light& light = world.Lights[lightIndex];
		light.ShadowMaps.clear();

		if (!light.CastShadows)
			continue;

		if (light.Type == LightType::Directional)
		{
			// Frustum corners in world space
			const Matrix vpInverse = m_pCamera->GetViewProjection().Invert();
			const Vector3 frustumCornersWS[] = {
				Vector3::Transform(Vector3(-1, -1, 1), vpInverse),
				Vector3::Transform(Vector3(-1, -1, 0), vpInverse),
				Vector3::Transform(Vector3(-1, 1, 1), vpInverse),
				Vector3::Transform(Vector3(-1, 1, 0), vpInverse),
				Vector3::Transform(Vector3(1, 1, 1), vpInverse),
				Vector3::Transform(Vector3(1, 1, 0), vpInverse),
				Vector3::Transform(Vector3(1, -1, 1), vpInverse),
				Vector3::Transform(Vector3(1, -1, 0), vpInverse),
			};

			const Matrix lightView = Matrix::CreateFromQuaternion(light.Rotation).Invert();
			for (int i = 0; i < Tweakables::g_ShadowCascades; ++i)
			{
				float previousCascadeSplit = i == 0 ? minPoint : cascadeSplits[i - 1];
				float currentCascadeSplit = cascadeSplits[i];

				// Compute the frustum corners for the cascade in view space
				const Vector3 cornersVS[] = {
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[0], frustumCornersWS[1], previousCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[0], frustumCornersWS[1], currentCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[2], frustumCornersWS[3], previousCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[2], frustumCornersWS[3], currentCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[4], frustumCornersWS[5], previousCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[4], frustumCornersWS[5], currentCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[6], frustumCornersWS[7], previousCascadeSplit), lightView),
					Vector3::Transform(Vector3::Lerp(frustumCornersWS[6], frustumCornersWS[7], currentCascadeSplit), lightView),
				};

				Vector3 center = Vector3::Zero;
				for (const Vector3& corner : cornersVS)
					center += corner;
				center /= ARRAYSIZE(cornersVS);

				//Create a bounding sphere to maintain aspect in projection to avoid flickering when rotating
				float radius = 0;
				for (const Vector3& corner : cornersVS)
				{
					float dist = Vector3::Distance(center, corner);
					radius = Math::Max(dist, radius);
				}
				Vector3 minExtents = center - Vector3(radius);
				Vector3 maxExtents = center + Vector3(radius);

				// Snap the cascade to the resolution of the shadowmap
				Vector3 extents = maxExtents - minExtents;
				Vector3 texelSize = extents / 2048;
				minExtents = Math::Floor(minExtents / texelSize) * texelSize;
				maxExtents = Math::Floor(maxExtents / texelSize) * texelSize;
				center = (minExtents + maxExtents) * 0.5f;

				// Extent the Z bounds
				float extentsZ = fabs(center.z - minExtents.z);
				extentsZ = Math::Max(extentsZ, Math::Min(1500.0f, farPlane) * 0.5f);
				minExtents.z = center.z - extentsZ;
				maxExtents.z = center.z + extentsZ;

				Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z);

				ShadowView shadowView;
				ViewTransform& viewTransform = shadowView.View;
				viewTransform.IsPerspective = false;
				viewTransform.ViewProjection = lightView * projectionMatrix;
				viewTransform.ViewProjectionPrev = viewTransform.ViewProjection;
				viewTransform.OrthographicFrustum.Center = center;
				viewTransform.OrthographicFrustum.Extents = maxExtents - minExtents;
				viewTransform.OrthographicFrustum.Extents.z *= 10;
				viewTransform.OrthographicFrustum.Orientation = Quaternion::CreateFromRotationMatrix(lightView.Invert());
				(&view.ShadowCascadeDepths.x)[i] = nearPlane + currentCascadeSplit * (farPlane - nearPlane);
				AddShadowView(light, shadowView, 2048, i);
			}
		}
		else if (light.Type == LightType::Spot)
		{
			BoundingBox box(light.Position, Vector3(light.Range));
			if (!m_pCamera->GetFrustum().Contains(box))
				continue;

			const Matrix projection = Math::CreatePerspectiveMatrix(light.UmbraAngleDegrees * Math::DegreesToRadians, 1.0f, light.Range, 0.01f);
			const Matrix lightView = (Matrix::CreateFromQuaternion(light.Rotation) * Matrix::CreateTranslation(light.Position)).Invert();

			ShadowView shadowView;
			ViewTransform& viewTransform = shadowView.View;
			viewTransform.IsPerspective = true;
			viewTransform.ViewProjection = lightView * projection;
			viewTransform.ViewProjectionPrev = viewTransform.ViewProjection;
			viewTransform.PerspectiveFrustum = Math::CreateBoundingFrustum(projection, lightView);
			AddShadowView(light, shadowView, 512, 0);
		}
		else if (light.Type == LightType::Point)
		{
			BoundingSphere sphere(light.Position, light.Range);
			if (!m_pCamera->GetFrustum().Contains(sphere))
				continue;

			Matrix viewMatrices[] = {
				Math::CreateLookToMatrix(light.Position, Vector3::Right,	Vector3::Up),
				Math::CreateLookToMatrix(light.Position, Vector3::Left,		Vector3::Up),
				Math::CreateLookToMatrix(light.Position, Vector3::Up,		Vector3::Forward),
				Math::CreateLookToMatrix(light.Position, Vector3::Down,		Vector3::Backward),
				Math::CreateLookToMatrix(light.Position, Vector3::Backward, Vector3::Up),
				Math::CreateLookToMatrix(light.Position, Vector3::Forward,	Vector3::Up),
			};
			Matrix projection = Math::CreatePerspectiveMatrix(Math::PI_DIV_2, 1, light.Range, 0.01f);

			for (int i = 0; i < ARRAYSIZE(viewMatrices); ++i)
			{
				ShadowView shadowView;
				ViewTransform& viewTransform = shadowView.View;
				viewTransform.IsPerspective = true;
				viewTransform.ViewProjection = viewMatrices[i] * projection;
				viewTransform.ViewProjectionPrev = viewTransform.ViewProjection;
				viewTransform.PerspectiveFrustum = Math::CreateBoundingFrustum(projection, viewMatrices[i]);
				AddShadowView(light, shadowView, 512, i);
			}
		}
	}

	m_ShadowHZBs.resize(shadowIndex);
}
