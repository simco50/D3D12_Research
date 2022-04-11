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
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Techniques/GpuParticles.h"
#include "Graphics/Techniques/RTAO.h"
#include "Graphics/Techniques/TiledForward.h"
#include "Graphics/Techniques/ClusteredForward.h"
#include "Graphics/Techniques/RTReflections.h"
#include "Graphics/Techniques/PathTracing.h"
#include "Graphics/Techniques/SSAO.h"
#include "Graphics/Techniques/CBTTessellation.h"
#include "Graphics/ImGuiRenderer.h"
#include "Core/TaskQueue.h"
#include "Core/CommandLine.h"
#include "Core/Paths.h"
#include "Core/Input.h"
#include "Core/ConsoleVariables.h"
#include "Core/Utils.h"
#include "imgui_internal.h"
#include "IconsFontAwesome4.h"

static const int32 FRAME_COUNT = 3;
static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D32_FLOAT;

void EditTransform(const Camera& camera, Matrix& matrix)
{
	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::ROTATE);
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);

	if (!Input::Instance().IsMouseDown(VK_LBUTTON))
	{
		if (Input::Instance().IsKeyPressed('W'))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		else if (Input::Instance().IsKeyPressed('E'))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		else if (Input::Instance().IsKeyPressed('R'))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
	}

	if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(&matrix.m[0][0], matrixTranslation, matrixRotation, matrixScale);
	ImGui::InputFloat3("Tr", matrixTranslation);
	ImGui::InputFloat3("Rt", matrixRotation);
	ImGui::InputFloat3("Sc", matrixScale);
	ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &matrix.m[0][0]);

	if (mCurrentGizmoOperation != ImGuizmo::SCALE)
	{
		if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
			mCurrentGizmoMode = ImGuizmo::LOCAL;
		ImGui::SameLine();
		if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
			mCurrentGizmoMode = ImGuizmo::WORLD;

		if (Input::Instance().IsKeyPressed(VK_SPACE))
		{
			mCurrentGizmoMode = mCurrentGizmoMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
		}
	}

	static Vector3 translationSnap = Vector3(1);
	static float rotateSnap = 5;
	static float scaleSnap = 0.1f;
	float* pSnapValue = &translationSnap.x;

	switch (mCurrentGizmoOperation)
	{
	case ImGuizmo::TRANSLATE:
		ImGui::InputFloat3("Snap", &translationSnap.x);
		pSnapValue = &translationSnap.x;
		break;
	case ImGuizmo::ROTATE:
		ImGui::InputFloat("Angle Snap", &rotateSnap);
		pSnapValue = &rotateSnap;
		break;
	case ImGuizmo::SCALE:
		ImGui::InputFloat("Scale Snap", &scaleSnap);
		pSnapValue = &scaleSnap;
		break;
	default:
		break;
	}

	Matrix view = camera.GetView();
	Matrix projection = camera.GetProjection();
	Math::ReverseZProjection(projection);
	ImGuizmo::Manipulate(&view.m[0][0], &projection.m[0][0], mCurrentGizmoOperation, mCurrentGizmoMode, &matrix.m[0][0], NULL, pSnapValue);
}

namespace Tweakables
{
	// Post processing
	ConsoleVariable g_WhitePoint("r.Exposure.WhitePoint", 1.0f);
	ConsoleVariable g_MinLogLuminance("r.Exposure.MinLogLuminance", -4.0f);
	ConsoleVariable g_MaxLogLuminance("r.Exposure.MaxLogLuminance", 20.0f);
	ConsoleVariable g_Tau("r.Exposure.Tau", 2.0f);
	ConsoleVariable g_DrawHistogram("vis.Histogram", false);
	ConsoleVariable g_ToneMapper("r.Tonemapper", 1);
	ConsoleVariable g_TAA("r.Taa", true);

	// Shadows
	ConsoleVariable g_SDSM("r.Shadows.SDSM", false);
	ConsoleVariable g_VisualizeShadowCascades("vis.ShadowCascades", false);
	ConsoleVariable g_ShadowCascades("r.Shadows.CascadeCount", 4);
	ConsoleVariable g_PSSMFactor("r.Shadow.PSSMFactor", 1.0f);

	// Bloom
	ConsoleVariable g_Bloom("r.Bloom", true);
	ConsoleVariable g_BloomThreshold("r.Bloom.Threshold", 4.0f);
	ConsoleVariable g_BloomMaxBrightness("r.Bloom.MaxBrightness", 8.0f);

	// Misc Lighting
	ConsoleVariable g_VolumetricFog("r.VolumetricFog", true);
	ConsoleVariable g_RaytracedAO("r.Raytracing.AO", false);
	ConsoleVariable g_VisualizeLights("vis.Lights", false);
	ConsoleVariable g_VisualizeLightDensity("vis.LightDensity", false);
	ConsoleVariable g_EnableDDGI("r.DDGI", true);
	ConsoleVariable g_VisualizeDDGI("vis.DDGI", false);
	ConsoleVariable g_RenderObjectBounds("r.vis.ObjectBounds", false);

	ConsoleVariable g_RaytracedReflections("r.Raytracing.Reflections", true);
	ConsoleVariable g_TLASBoundsThreshold("r.Raytracing.TLASBoundsThreshold", 1.0f * Math::DegreesToRadians);
	ConsoleVariable g_SsrSamples("r.SSRSamples", 8);
	ConsoleVariable g_RenderTerrain("r.Terrain", false);

	ConsoleVariable g_FreezeClusterCulling("r.FreezeClusterCulling", false);

	// Misc
	bool g_DumpRenderGraph = false;
	DelegateConsoleCommand<> gDumpRenderGraph("DumpRenderGraph", []() { g_DumpRenderGraph = true; });
	bool g_Screenshot = false;
	DelegateConsoleCommand<> gScreenshot("Screenshot", []() { g_Screenshot = true; });

	// Lighting
	float g_SunInclination = 0.79f;
	float g_SunOrientation = -1.503f;
	float g_SunTemperature = 5900.0f;
	float g_SunIntensity = 5.0f;
}

DemoApp::DemoApp(WindowHandle window, const IntVector2& windowRect)
	: m_Window(window)
{
	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetNearPlane(80.0f);
	m_pCamera->SetFarPlane(0.1f);

	E_LOG(Info, "Graphics::InitD3D()");

	GraphicsInstanceFlags instanceFlags = GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("d3ddebug") ? GraphicsInstanceFlags::DebugDevice : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("dred") ? GraphicsInstanceFlags::DRED : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("gpuvalidation") ? GraphicsInstanceFlags::GpuValidation : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("pix") ? GraphicsInstanceFlags::Pix : GraphicsInstanceFlags::None;
	GraphicsInstance instance = GraphicsInstance::CreateInstance(instanceFlags);

	RefCountPtr<IDXGIAdapter4> pAdapter = instance.EnumerateAdapter(CommandLine::GetBool("warp"));
	m_pDevice = instance.CreateDevice(pAdapter);
	m_pSwapchain = instance.CreateSwapchain(m_pDevice, window, windowRect.x, windowRect.y, FRAME_COUNT, true);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(m_pDevice, window, FRAME_COUNT);

	m_pClusteredForward = std::make_unique<ClusteredForward>(m_pDevice);
	m_pTiledForward = std::make_unique<TiledForward>(m_pDevice);
	m_pRTReflections = std::make_unique<RTReflections>(m_pDevice);
	m_pRTAO = std::make_unique<RTAO>(m_pDevice);
	m_pSSAO = std::make_unique<SSAO>(m_pDevice);
	m_pParticles = std::make_unique<GpuParticles>(m_pDevice);
	m_pPathTracing = std::make_unique<PathTracing>(m_pDevice);
	m_pCBTTessellation = std::make_unique<CBTTessellation>(m_pDevice);

	Profiler::Get()->Initialize(m_pDevice, FRAME_COUNT);
	DebugRenderer::Get()->Initialize(m_pDevice);

	OnResize(windowRect.x, windowRect.y);
	OnResizeViewport(windowRect.x, windowRect.y);

	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	InitializePipelines();
	SetupScene(*pContext);
	pContext->Execute(true);

	Tweakables::g_RaytracedAO = m_pDevice->GetCapabilities().SupportsRaytracing() ? Tweakables::g_RaytracedAO : false;
	Tweakables::g_RaytracedReflections = m_pDevice->GetCapabilities().SupportsRaytracing() ? Tweakables::g_RaytracedReflections : false;

	if (m_RenderPath == RenderPath::Visibility && !m_pDevice->GetCapabilities().SupportsMeshShading())
		m_RenderPath = RenderPath::Clustered;
	else if(m_RenderPath == RenderPath::PathTracing && !m_pDevice->GetCapabilities().SupportsRaytracing())
		m_RenderPath = RenderPath::Clustered;
}

DemoApp::~DemoApp()
{
	m_pDevice->IdleGPU();
	DebugRenderer::Get()->Shutdown();
	Profiler::Get()->Shutdown();
}

void DemoApp::SetupScene(CommandContext& context)
{
	m_pCamera->SetPosition(Vector3(-1.3f, 2.4f, -1.5f));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV4, Math::PIDIV4 * 0.5f, 0));

	{
#if 1
		m_pCamera->SetPosition(Vector3(-1.3f, 2.4f, -1.5f));
		m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV4, Math::PIDIV4 * 0.5f, 0));

		LoadMesh("Resources/Scenes/Sponza/Sponza.gltf", context);
#elif 0
		// Hardcode the camera of the scene :-)
		Matrix m(
			0.868393660f, 8.00937414e-08f, -0.495875478f, 0,
			0.0342082977f, 0.997617662f, 0.0599068627f, 0,
			0.494694114f, -0.0689857975f, 0.866324782f, 0,
			0, 0, 0, 1
		);

		m_pCamera->SetPosition(Vector3(-2.22535753f, 0.957680941f, -5.52742338f));
		m_pCamera->SetFoV(68.75f * Math::PI / 180.0f);
		m_pCamera->SetRotation(Quaternion::CreateFromRotationMatrix(m));

		LoadMesh("D:/References/GltfScenes/bathroom_pt/LAZIENKA.gltf", context);
#elif 1
		LoadMesh("D:/References/GltfScenes/Sphere/scene.gltf", context);
#elif 0
		LoadMesh("D:/References/GltfScenes/BlenderSplash/MyScene.gltf", context);
#endif
	}

	{
		Vector3 Position(-150, 160, -10);
		Vector3 Direction;
		Position.Normalize(Direction);
		Light sunLight = Light::Directional(Position, -Direction, 10);
		sunLight.CastShadows = true;
		sunLight.VolumetricLighting = true;
		m_Lights.push_back(sunLight);
	}

#if 0
	for (int i = 0; i < 5; ++i)
	{
		Vector3 loc(
			Math::RandomRange(-10.0f, 10.0f),
			Math::RandomRange(-4.0f, 5.0f),
			Math::RandomRange(-10.0f, 10.0f)
		);
		Light spotLight = Light::Spot(loc, 100, Vector3(0, 1, 0), 65, 50, 1000, Color(Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), 1.0f));
		spotLight.CastShadows = true;
		//spotLight.LightTexture = m_pDevice->RegisterBindlessResource(m_pLightCookie.get(), GetDefaultTexture(DefaultTexture::White2D));
		spotLight.VolumetricLighting = true;
		m_Lights.push_back(spotLight);
	}
#endif
}

void DemoApp::Update()
{
	PROFILE_BEGIN("Update");
	m_pImGuiRenderer->NewFrame();
	m_pDevice->GetShaderManager()->ConditionallyReloadShaders();
	UpdateImGui();
	m_pCamera->Update();

	if (Input::Instance().IsKeyPressed('1'))
	{
		m_RenderPath = RenderPath::Clustered;
	}
	else if (Input::Instance().IsKeyPressed('2'))
	{
		m_RenderPath = RenderPath::Tiled;
	}
	else if (Input::Instance().IsKeyPressed('3') && m_pVisibilityRenderingPSO)
	{
		m_RenderPath = RenderPath::Visibility;
	}
	else if (Input::Instance().IsKeyPressed('4') && m_pPathTracing->IsSupported())
	{
		m_RenderPath = RenderPath::PathTracing;
	}

	if (Tweakables::g_RenderObjectBounds)
	{
		for (const Batch& b : m_SceneData.Batches)
		{
			DebugRenderer::Get()->AddBoundingBox(b.Bounds, Color(0.2f, 0.2f, 0.9f, 1.0f));
			DebugRenderer::Get()->AddSphere(b.Bounds.Center, b.Radius, 6, 6, Color(0.2f, 0.6f, 0.2f, 1.0f));
		}
	}

	float costheta = cosf(Tweakables::g_SunOrientation);
	float sintheta = sinf(Tweakables::g_SunOrientation);
	float cosphi = cosf(Tweakables::g_SunInclination * Math::PIDIV2);
	float sinphi = sinf(Tweakables::g_SunInclination * Math::PIDIV2);
	m_Lights[0].Direction = -Vector3(costheta * cosphi, sinphi, sintheta * cosphi);
	m_Lights[0].Colour = Math::MakeFromColorTemperature(Tweakables::g_SunTemperature);
	m_Lights[0].Intensity = Tweakables::g_SunIntensity;

	if (m_DDGIVolumes.size() > 0)
	{
		DDGIVolume& volume = m_DDGIVolumes[0];
		volume.Origin = m_SceneData.SceneAABB.Center;
		volume.Extents = 1.1f * Vector3(m_SceneData.SceneAABB.Extents);
	}

	if (Tweakables::g_VisualizeLights)
	{
		for (const Light& light : m_Lights)
		{
			DebugRenderer::Get()->AddLight(light);
		}
	}

	CreateShadowViews();
	m_SceneData.View = m_pCamera->GetViewTransform();
	m_SceneData.FrameIndex = m_Frame;

	{
		PROFILE_SCOPE("Frustum Culling");
		bool boundsSet = false;
		BoundingFrustum frustum = m_pCamera->GetFrustum();
		for (const Batch& b : m_SceneData.Batches)
		{
			m_SceneData.VisibilityMask.AssignBit(b.InstanceData.World, frustum.Contains(b.Bounds));
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
	}

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////

	if (Tweakables::g_Screenshot)
	{
		Tweakables::g_Screenshot = false;

		CommandContext* pContext = m_pDevice->AllocateCommandContext();
		Texture* pSource = m_pTonemapTarget;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
		D3D12_RESOURCE_DESC resourceDesc = m_pTonemapTarget->GetResource()->GetDesc();
		m_pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
		RefCountPtr<Buffer> pScreenshotBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
		pContext->InsertResourceBarrier(m_pTonemapTarget, D3D12_RESOURCE_STATE_COPY_SOURCE);
		pContext->InsertResourceBarrier(pScreenshotBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
		pContext->CopyTexture(m_pTonemapTarget, pScreenshotBuffer, CD3DX12_BOX(0, 0, m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight()));

		ScreenshotRequest request;
		request.Width = pSource->GetWidth();
		request.Height = pSource->GetHeight();
		request.RowPitch = textureFootprint.Footprint.RowPitch;
		request.pBuffer = pScreenshotBuffer;
		request.Fence = pContext->Execute(false);
		m_ScreenshotBuffers.emplace(request);
	}

	if (!m_ScreenshotBuffers.empty())
	{
		while (!m_ScreenshotBuffers.empty() && m_pDevice->IsFenceComplete(m_ScreenshotBuffers.front().Fence))
		{
			const ScreenshotRequest& request = m_ScreenshotBuffers.front();

			TaskContext taskContext;
			TaskQueue::Execute([request](uint32)
				{
					char* pData = (char*)request.pBuffer->GetMappedData();
					Image img;
					img.SetSize(request.Width, request.Height, 4);
					uint32 imageRowPitch = request.Width * 4;
					uint32 targetOffset = 0;
					for (uint32 i = 0; i < request.Height; ++i)
					{
						img.SetData((uint32*)pData, targetOffset, imageRowPitch);
						pData += request.RowPitch;
						targetOffset += imageRowPitch;
					}

					SYSTEMTIME time;
					GetSystemTime(&time);
					Paths::CreateDirectoryTree(Paths::ScreenshotDir());
					char filePath[128];
					FormatString(filePath, ARRAYSIZE(filePath), "%sScreenshot_%d_%02d_%02d__%02d_%02d_%02d_%d.png",
						Paths::ScreenshotDir().c_str(),
						time.wYear, time.wMonth, time.wDay,
						time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
					img.Save(filePath);
				}, taskContext);
			m_ScreenshotBuffers.pop();
		}
	}

	{
		RGGraph graph(m_pDevice);
		RGPassBuilder updateScenePass = graph.AddPass("Update GPU Scene");
		updateScenePass.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				UploadSceneData(context);
			});
		graph.Compile();
		graph.Execute();
	}

	RGGraph graph(m_pDevice);

	if (m_RenderPath == RenderPath::Clustered || m_RenderPath == RenderPath::Tiled || m_RenderPath == RenderPath::Visibility)
	{
		// PARTICLES GPU SIM
		m_pParticles->Simulate(graph, m_SceneData, GetDepthStencil());

		// SHADOWS
		RGPassBuilder shadows = graph.AddPass("Shadow Mapping");
		shadows.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pCommonRS);

				// hack - copy the main viewport and then just modify the viewproj
				SceneView view = m_SceneData;

				for (int i = 0; i < view.ShadowViews.size(); ++i)
				{
					GPU_PROFILE_SCOPE("Light View", &context);
					const ShadowView& shadowView = view.ShadowViews[i];
					Texture* pShadowmap = m_ShadowMaps[i];
					context.InsertResourceBarrier(pShadowmap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
					context.BeginRenderPass(RenderPassInfo::DepthOnly(pShadowmap, RenderPassAccess::Clear_Store));

					view.View.ViewProjection = shadowView.ViewProjection;
					context.SetRootCBV(1, GetViewUniforms(view, pShadowmap));

					VisibilityMask mask;
					mask.SetAll();
					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pShadowsOpaquePSO);
						DrawScene(context, m_SceneData, mask, Batch::Blending::Opaque);
					}
					{
						GPU_PROFILE_SCOPE("Masked", &context);
						context.SetPipelineState(m_pShadowsAlphaMaskPSO);
						DrawScene(context, m_SceneData, mask, Batch::Blending::AlphaMask | Batch::Blending::AlphaBlend);
					}
					context.EndRenderPass();
				}
			});
	}

	if (m_RenderPath == RenderPath::Clustered || m_RenderPath == RenderPath::Tiled)
	{
		//DEPTH PREPASS
		// - Depth only pass that renders the entire scene
		// - Optimization that prevents wasteful lighting calculations during the base pass
		// - Required for light culling
		RGPassBuilder prepass = graph.AddPass("Depth Prepass");
		prepass.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pDepthStencil = GetDepthStencil();
				context.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

				context.BeginRenderPass(RenderPassInfo::DepthOnly(pDepthStencil, RenderPassAccess::Clear_Store));
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				context.SetGraphicsRootSignature(m_pCommonRS);

				context.SetRootCBV(1, GetViewUniforms(m_SceneData, pDepthStencil));

				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetPipelineState(m_pDepthPrepassOpaquePSO);
					DrawScene(context, m_SceneData, Batch::Blending::Opaque);
				}
				{
					GPU_PROFILE_SCOPE("Masked", &context);
					context.SetPipelineState(m_pDepthPrepassAlphaMaskPSO);
					DrawScene(context, m_SceneData, Batch::Blending::AlphaMask);
				}

				context.EndRenderPass();
			});
	}
	else if (m_RenderPath == RenderPath::Visibility)
	{
		RGPassBuilder visibility = graph.AddPass("Visibility Buffer");
		visibility.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
			{
				Texture* pDepthStencil = GetDepthStencil();
				renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				renderContext.InsertResourceBarrier(m_pVisibilityTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);

				renderContext.BeginRenderPass(RenderPassInfo(m_pVisibilityTexture, RenderPassAccess::DontCare_Store, pDepthStencil, RenderPassAccess::Clear_Store, true));
				renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				renderContext.SetGraphicsRootSignature(m_pCommonRS);

				renderContext.SetRootCBV(1, GetViewUniforms(m_SceneData, GetCurrentRenderTarget()));
				{
					GPU_PROFILE_SCOPE("Opaque", &renderContext);
					renderContext.SetPipelineState(m_pVisibilityRenderingPSO);
					DrawScene(renderContext, m_SceneData, Batch::Blending::Opaque);
				}

				{
					GPU_PROFILE_SCOPE("Opaque Masked", &renderContext);
					renderContext.SetPipelineState(m_pVisibilityRenderingMaskedPSO);
					DrawScene(renderContext, m_SceneData, Batch::Blending::AlphaMask | Batch::Blending::AlphaBlend);
				}

				renderContext.EndRenderPass();

			});
	}
	if((m_RenderPath == RenderPath::Clustered || m_RenderPath == RenderPath::Tiled || m_RenderPath == RenderPath::Visibility) && Tweakables::g_EnableDDGI && m_DDGIVolumes.size() > 0 && m_pDevice->GetCapabilities().SupportsRaytracing())
	{
		RG_GRAPH_SCOPE("DDGI", graph);

		uint32 randomIndex = Math::RandomRange(0, (int)m_DDGIVolumes.size() - 1);
		DDGIVolume& ddgi = m_DDGIVolumes[randomIndex];

		struct
		{
			Vector3 RandomVector;
			float RandomAngle;
			float HistoryBlendWeight;
			uint32 VolumeIndex;
		} parameters;

		parameters.RandomVector = Math::RandVector();
		parameters.RandomAngle = Math::RandomRange(0.0f, 2.0f * Math::PI);
		parameters.HistoryBlendWeight = 0.98f;
		parameters.VolumeIndex = randomIndex;

		const uint32 numProbes = ddgi.NumProbes.x * ddgi.NumProbes.y * ddgi.NumProbes.z;

		RGPassBuilder ddgiRays = graph.AddPass("DDGI Raytrace");
		ddgiRays.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(ddgi.pProbeStates, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(ddgi.pIrradiance[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(ddgi.pRayBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pDDGITraceRaysSO);

				context.SetRootConstants(0, parameters);
				context.SetRootCBV(1, GetViewUniforms(m_SceneData));
				context.BindResources(2, ddgi.pRayBuffer->GetUAV());

				ShaderBindingTable bindingTable(m_pDDGITraceRaysSO);
				bindingTable.BindRayGenShader("TraceRaysRGS");
				bindingTable.BindMissShader("MaterialMS", 0);
				bindingTable.BindMissShader("OcclusionMS", 1);
				bindingTable.BindHitGroup("MaterialHG", 0);

				context.DispatchRays(bindingTable, ddgi.NumRays, numProbes);
			});

		RGPassBuilder ddgiUpdateIrradiance = graph.AddPass("DDGI Update Irradiance");
		ddgiUpdateIrradiance.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(ddgi.pIrradiance[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pDDGIUpdateIrradianceColorPSO);

				context.SetRootConstants(0, parameters);
				context.SetRootCBV(1, GetViewUniforms(m_SceneData));
				context.BindResources(2, ddgi.pIrradiance[1]->GetUAV());
				context.BindResources(3, {
					ddgi.pRayBuffer->GetSRV(),
					});

				context.Dispatch(numProbes);
				context.InsertResourceBarrier(ddgi.pIrradiance[1], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			});

		RGPassBuilder ddgiUpdateDepth = graph.AddPass("DDGI Update Depth");
		ddgiUpdateDepth.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(ddgi.pDepth[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pDDGIUpdateIrradianceDepthPSO);

				context.SetRootConstants(0, parameters);
				context.SetRootCBV(1, GetViewUniforms(m_SceneData));
				context.BindResources(2, {
					ddgi.pDepth[1]->GetUAV(),
					});
				context.BindResources(3, {
					ddgi.pRayBuffer->GetSRV(),
					});

				context.Dispatch(numProbes);
				context.InsertResourceBarrier(ddgi.pDepth[1], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			});

		RGPassBuilder ddgiUpdateStates = graph.AddPass("DDGI Update Probe States");
		ddgiUpdateStates.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(ddgi.pProbeStates, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(ddgi.pProbeOffset, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pDDGIUpdateProbeStatesPSO);

				context.SetRootConstants(0, parameters);
				context.SetRootCBV(1, GetViewUniforms(m_SceneData));
				context.BindResources(2, {
					ddgi.pProbeStates->GetUAV(),
					ddgi.pProbeOffset->GetUAV(),
					});
				context.BindResources(3, {
					ddgi.pRayBuffer->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(numProbes, 32));

				context.InsertResourceBarrier(ddgi.pProbeOffset, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				context.InsertResourceBarrier(ddgi.pProbeStates, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			});

		std::swap(ddgi.pIrradiance[0], ddgi.pIrradiance[1]);
		std::swap(ddgi.pDepth[0], ddgi.pDepth[1]);
	}

	RGPassBuilder computeSky = graph.AddPass("Compute Sky");
	computeSky.Bind([=](CommandContext& context, const RGPassResources& resources)
		{
			context.InsertResourceBarrier(m_pSkyTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.SetComputeRootSignature(m_pCommonRS);
			context.SetPipelineState(m_pRenderSkyPSO);

			context.SetRootCBV(1, GetViewUniforms(m_SceneData, m_pSkyTexture));
			context.BindResources(2, m_pSkyTexture->GetUAV());

			context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pSkyTexture->GetWidth(), 16, m_pSkyTexture->GetHeight(), 16));
		});

	if (m_RenderPath == RenderPath::Clustered || m_RenderPath == RenderPath::Tiled || m_RenderPath == RenderPath::Visibility)
	{
		//[WITH MSAA] DEPTH RESOLVE
		// - If MSAA is enabled, run a compute shader to resolve the depth buffer
		if (m_pDepthStencil->GetDesc().SampleCount > 1)
		{
			RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
			depthResolve.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(m_pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pResolvedDepthStencil, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pResolveDepthPSO);

					context.BindResources(2, m_pResolvedDepthStencil->GetUAV());
					context.BindResources(3, m_pDepthStencil->GetSRV());

					context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pDepthStencil->GetWidth(), 16, m_pDepthStencil->GetHeight(), 16));

					context.InsertResourceBarrier(m_pResolvedDepthStencil, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);
					context.FlushResourceBarriers();
				});
		}

		RGPassBuilder cameraMotion = graph.AddPass("Camera Motion");
		cameraMotion.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pVelocity, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pCameraMotionPSO);

				context.SetRootCBV(1, GetViewUniforms(m_SceneData, m_pVelocity));

				context.BindResources(2, m_pVelocity->GetUAV());
				context.BindResources(3, GetDepthStencil()->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pVelocity->GetWidth(), 8, m_pVelocity->GetHeight(), 8));
			});


		SceneTextures sceneTextures;
		sceneTextures.pAmbientOcclusion = m_pAmbientOcclusion;
		sceneTextures.pColorTarget = GetCurrentRenderTarget();
		sceneTextures.pDepth = GetDepthStencil();
		sceneTextures.pNormalsTarget = m_pNormals;
		sceneTextures.pRoughnessTarget = m_pRoughness;
		sceneTextures.pPreviousColorTarget = m_pPreviousColor;
		sceneTextures.pVelocity = m_pVelocity;

		if (Tweakables::g_RaytracedAO)
		{
			m_pRTAO->Execute(graph, m_SceneData, sceneTextures);
		}
		else
		{
			m_pSSAO->Execute(graph, m_SceneData, m_pAmbientOcclusion, GetDepthStencil());
		}

		if (m_RenderPath == RenderPath::Tiled)
		{
			m_pTiledForward->Execute(graph, m_SceneData, sceneTextures);
		}
		else if (m_RenderPath == RenderPath::Clustered)
		{
			m_pClusteredForward->Execute(graph, m_SceneData, sceneTextures);
		}
		else if (m_RenderPath == RenderPath::Visibility)
		{
			RGPassBuilder visibilityShading = graph.AddPass("Visibility Shading");
			visibilityShading.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
				{
					renderContext.InsertResourceBarrier(m_pVisibilityTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(sceneTextures.pAmbientOcclusion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(sceneTextures.pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(sceneTextures.pPreviousColorTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(sceneTextures.pColorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					renderContext.InsertResourceBarrier(sceneTextures.pNormalsTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					renderContext.InsertResourceBarrier(sceneTextures.pRoughnessTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					renderContext.SetComputeRootSignature(m_pCommonRS);
					renderContext.SetPipelineState(m_pVisibilityShadingPSO);

					renderContext.SetRootCBV(1, GetViewUniforms(m_SceneData, GetCurrentRenderTarget()));
					renderContext.BindResources(2, {
						sceneTextures.pColorTarget->GetUAV(),
						sceneTextures.pNormalsTarget->GetUAV(),
						sceneTextures.pRoughnessTarget->GetUAV(),
						});
					renderContext.BindResources(3, {
						m_pVisibilityTexture->GetSRV(),
						sceneTextures.pAmbientOcclusion->GetSRV(),
						sceneTextures.pDepth->GetSRV(),
						sceneTextures.pPreviousColorTarget->GetSRV(),
						});
					renderContext.Dispatch(ComputeUtils::GetNumThreadGroups(sceneTextures.pColorTarget->GetWidth(), 8, sceneTextures.pColorTarget->GetHeight(), 8));
					renderContext.InsertUavBarrier();
				});
		}

		m_pParticles->Render(graph, m_SceneData, GetCurrentRenderTarget(), GetDepthStencil());

		if (Tweakables::g_RenderTerrain.GetBool())
		{
			m_pCBTTessellation->Execute(graph, m_SceneData, sceneTextures);
		}

		RGPassBuilder renderSky = graph.AddPass("Render Sky");
		renderSky.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pDepthStencil = GetDepthStencil();
				context.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.InsertResourceBarrier(m_pSkyTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_Store, false);

				context.BeginRenderPass(info);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pSkyboxPSO);

				context.SetRootCBV(1, GetViewUniforms(m_SceneData, GetCurrentRenderTarget()));
				context.Draw(0, 36);

				context.EndRenderPass();
			});

		DebugRenderer::Get()->Render(graph, m_SceneData, GetCurrentRenderTarget(), GetDepthStencil());
	}
	else if (m_RenderPath == RenderPath::PathTracing)
	{
		m_pPathTracing->Render(graph, m_SceneData, GetCurrentRenderTarget());
	}

	RGPassBuilder resolve = graph.AddPass("Color Resolve");
	resolve.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			if (m_pHDRRenderTarget->GetDesc().SampleCount > 1)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				Texture* pTarget = Tweakables::g_TAA.Get() ? m_pTAASource : m_pHDRRenderTarget;
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, pTarget, 0, pTarget->GetFormat());
			}

			if (!Tweakables::g_TAA.Get())
			{
				context.CopyTexture(m_pHDRRenderTarget, m_pPreviousColor);
			}
			else
			{
				context.CopyTexture(m_pHDRRenderTarget, m_pTAASource);
			}
		});

	if (m_RenderPath != RenderPath::PathTracing)
	{
		if (Tweakables::g_RaytracedReflections)
		{
			SceneTextures params;
			params.pAmbientOcclusion = m_pAmbientOcclusion;
			params.pColorTarget = Tweakables::g_TAA.Get() ? m_pTAASource : m_pHDRRenderTarget;
			params.pDepth = GetDepthStencil();
			params.pNormalsTarget = m_pNormals;
			params.pRoughnessTarget = m_pRoughness;
			params.pPreviousColorTarget = m_pHDRRenderTarget;

			m_pRTReflections->Execute(graph, m_SceneData, params);
		}

		if (Tweakables::g_TAA.Get())
		{
			RGPassBuilder temporalResolve = graph.AddPass("Temporal Resolve");
			temporalResolve.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					context.InsertResourceBarrier(m_pTAASource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(m_pHDRRenderTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pVelocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pPreviousColor, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pTemporalResolvePSO);

					context.SetRootCBV(1, GetViewUniforms(m_SceneData, m_pHDRRenderTarget));

					context.BindResources(2, m_pHDRRenderTarget->GetUAV());
					context.BindResources(3,
						{
							m_pVelocity->GetSRV(),
							m_pPreviousColor->GetSRV(),
							m_pTAASource->GetSRV(),
							GetDepthStencil()->GetSRV(),
						});

					context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pHDRRenderTarget->GetWidth(), 8, m_pHDRRenderTarget->GetHeight(), 8));

					context.CopyTexture(m_pHDRRenderTarget, m_pPreviousColor);
				});
		}
	}

	if (Tweakables::g_SDSM)
	{
		RGPassBuilder depthReduce = graph.AddPass("Depth Reduce");
		depthReduce.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pSource = GetDepthStencil();
				Texture* pTarget = m_ReductionTargets[0];

				context.InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(pSource->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO : m_pPrepareReduceDepthPSO);

				context.SetRootCBV(1, GetViewUniforms(m_SceneData, pTarget));

				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, pSource->GetSRV());

				context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());

				context.SetPipelineState(m_pReduceDepthPSO);
				for (size_t i = 1; i < m_ReductionTargets.size(); ++i)
				{
					pSource = pTarget;
					pTarget = m_ReductionTargets[i];

					context.InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.BindResources(2, pTarget->GetUAV());
					context.BindResources(3, pSource->GetSRV());

					context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());
				}

				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_COPY_SOURCE);
				context.FlushResourceBarriers();

				context.CopyTexture(pTarget, m_ReductionReadbackTargets[m_Frame % FRAME_COUNT], CD3DX12_BOX(0, 1));
			});
	}

	{
		RG_GRAPH_SCOPE("Eye Adaptation", graph);
		Texture* pToneMapInput = m_pDownscaledColor;

		RGPassBuilder colorDownsample = graph.AddPass("Downsample Color");
		colorDownsample.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pHDRRenderTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pGenerateMipsPSO);

				struct
				{
					IntVector2 TargetDimensions;
					Vector2 TargetDimensionsInv;
				} parameters;
				parameters.TargetDimensions.x = pToneMapInput->GetWidth();
				parameters.TargetDimensions.y = pToneMapInput->GetHeight();
				parameters.TargetDimensionsInv = Vector2(1.0f / pToneMapInput->GetWidth(), 1.0f / pToneMapInput->GetHeight());

				context.SetRootConstants(0, parameters);
				context.BindResources(2, pToneMapInput->GetUAV());
				context.BindResources(3, m_pHDRRenderTarget->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(parameters.TargetDimensions.x, 8, parameters.TargetDimensions.y, 8));
			});

		RGPassBuilder histogram = graph.AddPass("Luminance Histogram");
		histogram.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				context.ClearUavUInt(m_pLuminanceHistogram, m_pLuminanceHistogram->GetUAV());

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pLuminanceHistogramPSO);

				struct
				{
					uint32 Width;
					uint32 Height;
					float MinLogLuminance;
					float OneOverLogLuminanceRange;
				} parameters;
				parameters.Width = pToneMapInput->GetWidth();
				parameters.Height = pToneMapInput->GetHeight();
				parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
				parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get());

				context.SetRootConstants(0, parameters);
				context.BindResources(2, m_pLuminanceHistogram->GetUAV());
				context.BindResources(3, pToneMapInput->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pToneMapInput->GetWidth(), 16, pToneMapInput->GetHeight(), 16));
			});

		RGPassBuilder avgLuminance = graph.AddPass("Average Luminance");
		avgLuminance.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAverageLuminance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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

				parameters.PixelCount = pToneMapInput->GetWidth() * pToneMapInput->GetHeight();
				parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
				parameters.LogLuminanceRange = Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get();
				parameters.TimeDelta = Time::DeltaTime();
				parameters.Tau = Tweakables::g_Tau.Get();

				context.SetRootConstants(0, parameters);
				context.BindResources(2, m_pAverageLuminance->GetUAV());
				context.BindResources(3, m_pLuminanceHistogram->GetSRV());

				context.Dispatch(1);
			});
	}
	{
		if (Tweakables::g_Bloom.Get())
		{
			RG_GRAPH_SCOPE("Bloom", graph);

			RGPassBuilder bloomSeparate = graph.AddPass("Separate Bloom");
			bloomSeparate.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					Texture* pTarget = m_pBloomTexture;

					RefCountPtr<UnorderedAccessView>* pTargetUAVs = m_pBloomUAVs.data();

					context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pAverageLuminance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBloomSeparatePSO);

					struct
					{
						float Threshold;
						float BrightnessClamp;
					} parameters;

					parameters.Threshold = Tweakables::g_BloomThreshold;
					parameters.BrightnessClamp = Tweakables::g_BloomMaxBrightness;

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, GetViewUniforms(m_SceneData));

					context.BindResources(2, {
						pTargetUAVs[0]
						});
					context.BindResources(3, {
						GetCurrentRenderTarget()->GetSRV(),
						m_pAverageLuminance->GetSRV(),
						});;

					context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
				});

			RGPassBuilder bloomMipChain = graph.AddPass("Bloom Mip Chain");
			bloomMipChain.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					Texture* pSource = m_pBloomTexture;
					Texture* pTarget = m_pBloomIntermediateTexture;

					RefCountPtr<UnorderedAccessView>* pSourceUAVs = m_pBloomUAVs.data();
					RefCountPtr<UnorderedAccessView>* pTargetUAVs = m_pBloomIntermediateUAVs.data();

					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBloomMipChainPSO);

					context.SetRootCBV(1, GetViewUniforms(m_SceneData));

					uint32 width = pTarget->GetWidth() / 2;
					uint32 height = pTarget->GetHeight() / 2;

					const uint32 numMips = pTarget->GetMipLevels();
					constexpr uint32 ThreadGroupSize = 128;

					for (uint32 i = 1; i < numMips; ++i)
					{
						struct
						{
							uint32 SourceMip;
							Vector2 TargetDimensionsInv;
							uint32 Horizontal;
						} parameters;

						parameters.TargetDimensionsInv = Vector2(1.0f / width, 1.0f / height);

						for (uint32 direction = 0; direction < 2; ++direction)
						{
							context.InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
							context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

							parameters.SourceMip = direction == 0 ? i - 1 : i;
							parameters.Horizontal = direction;

							context.SetRootConstants(0, parameters);
							context.BindResources(2, pTargetUAVs[i].Get());
							context.BindResources(3, pSource->GetSRV());

							IntVector3 numThreadGroups = direction == 0 ?
								ComputeUtils::GetNumThreadGroups(width, 1, height, ThreadGroupSize) :
								ComputeUtils::GetNumThreadGroups(width, ThreadGroupSize, height, 1);
							context.Dispatch(numThreadGroups);

							std::swap(pSource, pTarget);
							std::swap(pSourceUAVs, pTargetUAVs);
						}

						width /= 2;
						height /= 2;
					}
				});
		}
	}

	RGPassBuilder tonemap = graph.AddPass("Tonemap");
	tonemap.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			struct
			{
				float WhitePoint;
				uint32 Tonemapper;
			} constBuffer;
			constBuffer.WhitePoint = Tweakables::g_WhitePoint.Get();
			constBuffer.Tonemapper = Tweakables::g_ToneMapper.Get();

			context.InsertResourceBarrier(m_pTonemapTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAverageLuminance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pHDRRenderTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pBloomTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			context.SetPipelineState(m_pToneMapPSO);
			context.SetComputeRootSignature(m_pCommonRS);

			context.SetRootConstants(0, constBuffer);
			context.SetRootCBV(1, GetViewUniforms(m_SceneData, m_pTonemapTarget));
			context.BindResources(2, m_pTonemapTarget->GetUAV());
			context.BindResources(3, {
				m_pHDRRenderTarget->GetSRV(),
				m_pAverageLuminance->GetSRV(),
				Tweakables::g_Bloom.Get() ? m_pBloomTexture->GetSRV() : GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D)->GetSRV(),
				});
			context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pHDRRenderTarget->GetWidth(), 16, m_pHDRRenderTarget->GetHeight(), 16));
		});

	if (Tweakables::g_DrawHistogram.Get())
	{
		RGPassBuilder drawHistogram = graph.AddPass("Draw Histogram");
		drawHistogram.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAverageLuminance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pDebugHistogramTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
				parameters.InvTextureDimensions.x = 1.0f / m_pDebugHistogramTexture->GetWidth();
				parameters.InvTextureDimensions.y = 1.0f / m_pDebugHistogramTexture->GetHeight();

				context.SetRootConstants(0, parameters);
				context.BindResources(2, m_pDebugHistogramTexture->GetUAV());
				context.BindResources(3, {
					m_pLuminanceHistogram->GetSRV(),
					m_pAverageLuminance->GetSRV(),
					});
				context.ClearUavUInt(m_pDebugHistogramTexture, m_pDebugHistogramTexture->GetUAV());

				context.Dispatch(1, m_pLuminanceHistogram->GetNumElements());
			});
	}

	if (Tweakables::g_VisualizeLightDensity)
	{
		if (m_RenderPath == RenderPath::Clustered)
		{
			m_pClusteredForward->VisualizeLightDensity(graph, m_SceneData, m_pTonemapTarget, GetDepthStencil());
		}
		else
		{
			m_pTiledForward->VisualizeLightDensity(graph, m_pDevice, m_SceneData, m_pTonemapTarget, GetDepthStencil());
		}
	}

	if (Tweakables::g_VisualizeDDGI)
	{
		for (uint32 i = 0; i < m_DDGIVolumes.size(); ++i)
		{
			const DDGIVolume& ddgi = m_DDGIVolumes[i];
			RGPassBuilder ddgiVisualizeRays = graph.AddPass("DDGI Visualize");
			ddgiVisualizeRays.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					context.InsertResourceBarrier(ddgi.pRayBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
					context.InsertResourceBarrier(ddgi.pIrradiance[0], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pTonemapTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
					context.InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
					context.SetGraphicsRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pDDGIVisualizePSO);
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					context.BeginRenderPass(RenderPassInfo(m_pTonemapTarget, RenderPassAccess::Load_Store, GetDepthStencil(), RenderPassAccess::Load_Store, true));

					struct
					{
						uint32 VolumeIndex;
					} parameters;
					parameters.VolumeIndex = i;

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, GetViewUniforms(m_SceneData));
					context.BindResources(3, {
						ddgi.pRayBuffer->GetSRV(),
						});

					context.Draw(0, 2880, ddgi.NumProbes.x * ddgi.NumProbes.y * ddgi.NumProbes.z);

					context.EndRenderPass();
				});
		}
	}

	//UI
	Texture* pBackbuffer = m_pSwapchain->GetBackBuffer();
	m_pImGuiRenderer->Render(graph, m_SceneData, pBackbuffer);

	graph.Compile();
	if (Tweakables::g_DumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		Tweakables::g_DumpRenderGraph = false;
	}
	graph.Execute();

	PROFILE_END();

	Present();
}

void DemoApp::Present()
{
	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	pContext->InsertResourceBarrier(m_pSwapchain->GetBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
	pContext->Execute(false);

	//PRESENT
	//	- Set fence for the currently queued frame
	//	- Present the frame buffer
	//	- Wait for the next frame to be finished to start queueing work for it
	Profiler::Get()->Resolve(m_pSwapchain, m_pDevice, m_Frame);
	m_pDevice->TickFrame();
	m_pSwapchain->Present();
	++m_Frame;

	if (m_CapturePix)
	{
		D3D::EnqueuePIXCapture();
		m_CapturePix = false;
	}
}

void DemoApp::OnResize(int width, int height)
{
	E_LOG(Info, "Window resized: %dx%d", width, height);

	m_pDevice->IdleGPU();
	m_pSwapchain->OnResize(width, height);
}

void DemoApp::OnResizeViewport(int width, int height)
{
	E_LOG(Info, "Viewport resized: %dx%d", width, height);

	m_pDepthStencil = m_pDevice->CreateTexture(TextureDesc::CreateDepth(width, height, DXGI_FORMAT_D32_FLOAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)), "Depth Stencil");
	m_pNormals = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess, 1, ClearBinding(Colors::Black)), "Normals");
	m_pRoughness = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess, 1, ClearBinding(Colors::Black)), "Roughness");
	m_pHDRRenderTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "HDR Target");
	m_pPreviousColor = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource), "Previous Color");
	m_pTonemapTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, m_pSwapchain->GetFormat(), TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "Tonemap Target");
	m_pDownscaledColor = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Downscaled HDR Target");
	m_pAmbientOcclusion = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "SSAO");
	m_pVelocity = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Velocity");
	m_pTAASource = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "TAA Target");
	m_pVisibilityTexture = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R32_UINT, TextureFlag::RenderTarget | TextureFlag::ShaderResource), "Visibility Buffer");

	m_pClusteredForward->OnResize(width, height);
	m_pTiledForward->OnResize(width, height);
	m_pSSAO->OnResize(width, height);
	m_pRTReflections->OnResize(width, height);
	m_pPathTracing->OnResize(width, height);

	m_ReductionTargets.clear();
	int w = width;
	int h = height;
	while (w > 1 || h > 1)
	{
		w = Math::DivideAndRoundUp(w, 16);
		h = Math::DivideAndRoundUp(h, 16);
		RefCountPtr<Texture> pTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(w, h, DXGI_FORMAT_R32G32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "SDSM Reduction Target");
		m_ReductionTargets.push_back(std::move(pTexture));
	}

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		RefCountPtr<Buffer> pBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateTyped(1, DXGI_FORMAT_R32G32_FLOAT, BufferFlag::Readback), "SDSM Reduction Readback Target");
		m_ReductionReadbackTargets.push_back(std::move(pBuffer));
	}

	uint32 mips = Math::Min(5u, (uint32)log2f((float)Math::Max(width, height)));
	TextureDesc bloomDesc = TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess, 1, mips);
	m_pBloomTexture = m_pDevice->CreateTexture(bloomDesc, "Bloom");
	m_pBloomIntermediateTexture = m_pDevice->CreateTexture(bloomDesc, "Bloom Intermediate");

	m_pBloomUAVs.resize(mips);
	m_pBloomIntermediateUAVs.resize(mips);
	for (uint32 i = 0; i < mips; ++i)
	{
		m_pBloomUAVs[i] = m_pDevice->CreateUAV(m_pBloomTexture, TextureUAVDesc((uint8)i));
		m_pBloomIntermediateUAVs[i] = m_pDevice->CreateUAV(m_pBloomIntermediateTexture, TextureUAVDesc((uint8)i));
	}

	m_pCamera->SetViewport(FloatRect(0, 0, (float)width, (float)height));
}

void DemoApp::InitializePipelines()
{
	// Common Root Signature - Make it 12 DWORDs as is often recommended by IHVs
	m_pCommonRS = new RootSignature(m_pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
	m_pCommonRS->Finalize("Common");

	//Shadow mapping - Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("DepthOnly.hlsl", "VSMain");
		psoDesc.SetRenderTargetFormats({}, DEPTH_STENCIL_SHADOW_FORMAT, 1);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthBias(-1, -5.0f, -4.0f);
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
		psoDesc.SetRenderTargetFormats({}, DXGI_FORMAT_D32_FLOAT, 1);
		psoDesc.SetName("Depth Prepass Opaque");
		m_pDepthPrepassOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetPixelShader("DepthOnly.hlsl", "PSMain");
		psoDesc.SetName("Depth Prepass Alpha Mask");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDepthPrepassAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	m_pLuminanceHistogramPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "LuminanceHistogram.hlsl", "CSMain");
	m_pLuminanceHistogram = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(sizeof(uint32) * 256), "Luminance Histogram");
	m_pAverageLuminance = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource), "Average Luminance");
	m_pDebugHistogramTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(m_pLuminanceHistogram->GetNumElements() * 4, m_pLuminanceHistogram->GetNumElements(), DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Debug Histogram");

	m_pDrawHistogramPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "DrawLuminanceHistogram.hlsl", "DrawLuminanceHistogram");
	m_pAverageLuminancePSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "AverageLuminance.hlsl", "CSMain");

	//Depth resolve
	m_pResolveDepthPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "ResolveDepth.hlsl", "CSMain", { "DEPTH_RESOLVE_MIN" });
	m_pPrepareReduceDepthPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "ReduceDepth.hlsl", "PrepareReduceDepth");
	m_pPrepareReduceDepthMsaaPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "ReduceDepth.hlsl", "PrepareReduceDepth", { "WITH_MSAA" });
	m_pReduceDepthPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "ReduceDepth.hlsl", "ReduceDepth");

	m_pToneMapPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "Tonemapping.hlsl", "CSMain");
	m_pCameraMotionPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "CameraMotionVectors.hlsl", "CSMain");
	m_pTemporalResolvePSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "TemporalResolve.hlsl", "CSMain");

	m_pGenerateMipsPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "GenerateMips.hlsl", "CSMain");

	//Sky
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("ProceduralSky.hlsl", "VSMain");
		psoDesc.SetPixelShader("ProceduralSky.hlsl", "PSMain");
		psoDesc.SetRenderTargetFormats(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pRenderSkyPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "ProceduralSky.hlsl", "ComputeSkyCS");
		m_pSkyTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(64, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Sky");
	}

	//Bloom
	m_pBloomSeparatePSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "Bloom.hlsl", "SeparateBloomCS");
	m_pBloomMipChainPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "Bloom.hlsl", "BloomMipChainCS");

	//Visibility Rendering
	if (m_pDevice->GetCapabilities().SupportsMeshShading())
	{
		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetAmplificationShader("VisibilityRendering.hlsl", "ASMain");
		psoDesc.SetMeshShader("VisibilityRendering.hlsl", "MSMain");
		psoDesc.SetPixelShader("VisibilityRendering.hlsl", "PSMain");
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(DXGI_FORMAT_R32_UINT, DXGI_FORMAT_D32_FLOAT, 1);
		psoDesc.SetName("Visibility Rendering");
		m_pVisibilityRenderingPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetPixelShader("VisibilityRendering.hlsl", "PSMain", { "ALPHA_TEST" });
		psoDesc.SetName("Visibility Rendering Masked");
		m_pVisibilityRenderingMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

		//Visibility Shading
		m_pVisibilityShadingPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "VisibilityShading.hlsl", "CSMain");
	}

	// DDGI
	if (m_pDevice->GetCapabilities().SupportsRaytracing())
	{
		// Must match with shader!
		constexpr uint32 probeIrradianceTexels = 6;
		constexpr uint32 probeDepthTexel = 14;

		m_pDDGIUpdateIrradianceColorPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "RayTracing/DDGI.hlsl", "UpdateIrradianceCS");
		m_pDDGIUpdateIrradianceDepthPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "RayTracing/DDGI.hlsl", "UpdateDepthCS");
		m_pDDGIUpdateProbeStatesPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "RayTracing/DDGI.hlsl", "UpdateProbeStatesCS");

		StateObjectInitializer soDesc{};
		soDesc.Name = "DDGI Trace Rays";
		soDesc.MaxRecursion = 1;
		soDesc.MaxPayloadSize = 6 * sizeof(float);
		soDesc.MaxAttributeSize = 2 * sizeof(float);
		soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		soDesc.AddLibrary("RayTracing/DDGIRayTrace.hlsl", { "TraceRaysRGS" });
		soDesc.AddLibrary("RayTracing/SharedRaytracingLib.hlsl", { "OcclusionMS", "MaterialCHS", "MaterialAHS", "MaterialMS" });
		soDesc.AddHitGroup("MaterialHG", "MaterialCHS", "MaterialAHS");
		soDesc.AddMissShader("MaterialMS");
		soDesc.AddMissShader("OcclusionMiss");
		soDesc.pGlobalRootSignature = m_pCommonRS;
		m_pDDGITraceRaysSO = m_pDevice->CreateStateObject(soDesc);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("RayTracing/DDGI.hlsl", "VisualizeIrradianceVS");
		psoDesc.SetPixelShader("RayTracing/DDGI.hlsl", "VisualizeIrradiancePS");
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 1);
		psoDesc.SetName("Visualize Irradiance");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDDGIVisualizePSO = m_pDevice->CreatePipeline(psoDesc);

		DDGIVolume volume;
		volume.Origin = Vector3(-0.484151840f, 5.21196413f, 0.309524536f);
		volume.Extents = Vector3(14.8834171f, 6.22350454f, 9.15293312f);
		volume.NumProbes = IntVector3(16, 12, 14);
		volume.NumRays = 128;
		volume.MaxNumRays = 512;
		m_DDGIVolumes.push_back(volume);

		for (DDGIVolume& ddgi : m_DDGIVolumes)
		{
			uint32 numProbes = ddgi.NumProbes.x * ddgi.NumProbes.y * ddgi.NumProbes.z;
			ddgi.pProbeStates = m_pDevice->CreateBuffer(BufferDesc::CreateTyped(numProbes, DXGI_FORMAT_R8_UINT, BufferFlag::UnorderedAccess | BufferFlag::ShaderResource), "DDGI States Buffer");
			ddgi.pRayBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateTyped(numProbes * ddgi.MaxNumRays, DXGI_FORMAT_R16G16B16A16_FLOAT, BufferFlag::UnorderedAccess | BufferFlag::ShaderResource), "DDGI Ray Buffer");
			ddgi.pProbeOffset = m_pDevice->CreateBuffer(BufferDesc::CreateTyped(numProbes, DXGI_FORMAT_R16G16B16A16_FLOAT, BufferFlag::UnorderedAccess | BufferFlag::ShaderResource), "DDGI Probe Offset Buffer");
			{
				uint32 width = (1 + probeIrradianceTexels + 1) * ddgi.NumProbes.y * ddgi.NumProbes.x;
				uint32 height = (1 + probeIrradianceTexels + 1) * ddgi.NumProbes.z;
				TextureDesc ddgiIrradianceDesc = TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::UnorderedAccess);
				ddgi.pIrradiance[0] = m_pDevice->CreateTexture(ddgiIrradianceDesc, "DDGI Irradiance 0");
				ddgi.pIrradiance[1] = m_pDevice->CreateTexture(ddgiIrradianceDesc, "DDGI Irradiance 1");
			}
			{
				uint32 width = (1 + probeDepthTexel + 1) * ddgi.NumProbes.y * ddgi.NumProbes.x;
				uint32 height = (1 + probeDepthTexel + 1) * ddgi.NumProbes.z;
				TextureDesc ddgiDepthDesc = TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::UnorderedAccess);
				ddgi.pDepth[0] = m_pDevice->CreateTexture(ddgiDepthDesc, "DDGI Depth 0");
				ddgi.pDepth[1] = m_pDevice->CreateTexture(ddgiDepthDesc, "DDGI Depth 1");
			}
		}
	}
}

void DemoApp::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = Time::DeltaTime();

	static ImGuiConsole console;
	static bool showProfiler = false;
	static bool showImguiDemo = false;

	ImGuiViewport* pViewport = ImGui::GetMainViewport();
	ImGuiID dockspace = ImGui::DockSpaceOverViewport(pViewport);

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu(ICON_FA_FILE " File"))
		{
			if (ImGui::MenuItem(ICON_FA_FILE " Load Mesh", nullptr, nullptr))
			{
				OPENFILENAME ofn = { 0 };
				TCHAR szFile[260] = { 0 };
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = m_Window;
				ofn.lpstrFile = szFile;
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "Supported files (*.gltf;*.dat;*.ldr;*.mpd)\0*.gltf;*.dat;*.ldr;*.mpd\0All Files (*.*)\0*.*\0";;
				ofn.nFilterIndex = 1;
				ofn.lpstrFileTitle = NULL;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = NULL;
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameA(&ofn) == TRUE)
				{
					m_Meshes.clear();
					CommandContext* pContext = m_pDevice->AllocateCommandContext();
					LoadMesh(ofn.lpstrFile, *pContext);
					pContext->Execute(true);
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
				Tweakables::g_VisualizeShadowCascades.SetValue(!Tweakables::g_DrawHistogram.GetBool());
			}

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_WRENCH " Tools"))
		{
			if (ImGui::MenuItem("Dump RenderGraph"))
			{
				Tweakables::g_DumpRenderGraph = true;
			}
			if (ImGui::MenuItem("Screenshot"))
			{
				Tweakables::g_Screenshot = true;
			}
			if (ImGui::MenuItem("Pix Capture"))
			{
				m_CapturePix = true;
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

	ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
	ImGui::Begin("Viewport", 0, ImGuiWindowFlags_NoScrollbar);
	float widthDelta = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
	float heightDelta = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;
	uint32 width = (uint32)Math::Max(16.0f, widthDelta);
	uint32 height = (uint32)Math::Max(16.0f, heightDelta);

	if (width != m_pTonemapTarget->GetWidth() || height != m_pTonemapTarget->GetHeight())
	{
		OnResizeViewport(width, height);
	}
	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, (float)width, (float)height);
	ImGui::Image(m_pTonemapTarget, ImVec2((float)width, (float)height));
	ImGui::End();

	if (Tweakables::g_VisualizeLightDensity)
	{
		//Render Color Legend
		ImGui::SetNextWindowSize(ImVec2(60, 255));
		ImGui::SetNextWindowPos(ImVec2((float)pViewport->Size.x - 65, (float)pViewport->Size.x - 280));
		ImGui::Begin("Visualize Light Density", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
		ImGui::SetWindowFontScale(1.2f);
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
		static uint32 DEBUG_COLORS[] = {
			IM_COL32(0,4,141, 255),
			IM_COL32(5,10,255, 255),
			IM_COL32(0,164,255, 255),
			IM_COL32(0,255,189, 255),
			IM_COL32(0,255,41, 255),
			IM_COL32(117,254,1, 255),
			IM_COL32(255,239,0, 255),
			IM_COL32(255,86,0, 255),
			IM_COL32(204,3,0, 255),
			IM_COL32(65,0,1, 255),
		};

		for (uint32 i = 0; i < ARRAYSIZE(DEBUG_COLORS); ++i)
		{
			char number[16];
			FormatString(number, ARRAYSIZE(number), "%d", i);
			ImGui::PushStyleColor(ImGuiCol_Button, DEBUG_COLORS[i]);
			ImGui::Button(number, ImVec2(40, 20));
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleColor();
		ImGui::End();
	}

	console.Update(ImVec2(300, (float)pViewport->Size.x), ImVec2((float)pViewport->Size.x - 300 * 2, 250));

	if (showImguiDemo)
	{
		ImGui::ShowDemoWindow();
	}

	if (Tweakables::g_DrawHistogram)
	{
		ImGui::Begin("Luminance Histogram");
		ImVec2 cursor = ImGui::GetCursorPos();
		ImGui::ImageAutoSize(m_pDebugHistogramTexture, ImVec2((float)m_pDebugHistogramTexture->GetWidth(), (float)m_pDebugHistogramTexture->GetHeight()));
		ImGui::GetWindowDrawList()->AddText(cursor, IM_COL32(255, 255, 255, 255), Sprintf("%.2f", Tweakables::g_MinLogLuminance.Get()).c_str());
		ImGui::End();
	}

	if (m_pVisualizeTexture)
	{
		if (ImGui::Begin("Visualize Texture"))
		{
			ImGui::Text("Resolution: %dx%d", m_pVisualizeTexture->GetWidth(), m_pVisualizeTexture->GetHeight());
			ImGui::ImageAutoSize(m_pVisualizeTexture, ImVec2((float)m_pVisualizeTexture->GetWidth(), (float)m_pVisualizeTexture->GetHeight()));
		}
		ImGui::End();
	}

	if (Tweakables::g_VisualizeShadowCascades)
	{
		if (m_ShadowMaps.size() >= 4)
		{
			float imageSize = 230;
			if (ImGui::Begin("Shadow Cascades"))
			{
				const Light& sunLight = m_Lights[0];
				for (int i = 0; i < Tweakables::g_ShadowCascades; ++i)
				{
					ImGui::Image(m_ShadowMaps[sunLight.ShadowIndex + i], ImVec2(imageSize, imageSize));
					ImGui::SameLine();
				}
			}
			ImGui::End();
		}
	}

	if (showProfiler)
	{
		if (ImGui::Begin("Profiler", &showProfiler))
		{
			ImGui::Text("MS: %4.2f | FPS: %4.2f | %d x %d", Time::DeltaTime() * 1000.0f, 1.0f / Time::DeltaTime(), m_pHDRRenderTarget->GetWidth(), m_pHDRRenderTarget->GetHeight());
			ImGui::PlotLines("", m_FrameTimes.data(), (int)m_FrameTimes.size(), m_Frame % m_FrameTimes.size(), 0, 0.0f, 0.03f, ImVec2(ImGui::GetContentRegionAvail().x, 100));

			if (ImGui::TreeNodeEx("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ProfileNode* pRootNode = Profiler::Get()->GetRootNode();
				pRootNode->RenderImGui(m_Frame);
				ImGui::TreePop();
			}
		}
		ImGui::End();
	}

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Global"))
		{
			ImGui::Combo("Render Path", (int*)&m_RenderPath, [](void* /*data*/, int index, const char** outText)
				{
					RenderPath p = (RenderPath)index;
					switch (p)
					{
					case RenderPath::Tiled:
						*outText = "Tiled";
						break;
					case RenderPath::Clustered:
						*outText = "Clustered";
						break;
					case RenderPath::PathTracing:
						*outText = "Path Tracing";
						break;
					case RenderPath::Visibility:
						*outText = "Visibility";
						break;
					default:
						noEntry();
						break;
					}
					return true;
				}, nullptr, (int)RenderPath::MAX);

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

		if (ImGui::CollapsingHeader("Sky"))
		{
			ImGui::SliderFloat("Sun Orientation", &Tweakables::g_SunOrientation, -Math::PI, Math::PI);
			ImGui::SliderFloat("Sun Inclination", &Tweakables::g_SunInclination, 0, 1);
			ImGui::SliderFloat("Sun Temperature", &Tweakables::g_SunTemperature, 1000, 15000);
			ImGui::SliderFloat("Sun Intensity", &Tweakables::g_SunIntensity, 0, 30);
			ImGui::Checkbox("Volumetric Fog", &Tweakables::g_VolumetricFog.Get());
		}

		if (ImGui::CollapsingHeader("Shadows"))
		{
			ImGui::SliderInt("Shadow Cascades", &Tweakables::g_ShadowCascades.Get(), 1, 4);
			ImGui::Checkbox("SDSM", &Tweakables::g_SDSM.Get());
			ImGui::SliderFloat("PSSM Factor", &Tweakables::g_PSSMFactor.Get(), 0, 1);
			ImGui::Checkbox("Visualize Cascades", &Tweakables::g_VisualizeShadowCascades.Get());
		}
		if (ImGui::CollapsingHeader("Bloom"))
		{
			ImGui::Checkbox("Enabled", &Tweakables::g_Bloom.Get());
			ImGui::SliderFloat("Brightness Threshold", &Tweakables::g_BloomThreshold.Get(), 0, 5);
			ImGui::SliderFloat("Max Brightness", &Tweakables::g_BloomMaxBrightness.Get(), 1, 100);
		}
		if (ImGui::CollapsingHeader("Exposure/Tonemapping"))
		{
			ImGui::DragFloatRange2("Log Luminance", &Tweakables::g_MinLogLuminance.Get(), &Tweakables::g_MaxLogLuminance.Get(), 1.0f, -100, 50);
			ImGui::Checkbox("Draw Exposure Histogram", &Tweakables::g_DrawHistogram.Get());
			ImGui::SliderFloat("White Point", &Tweakables::g_WhitePoint.Get(), 0, 20);
			ImGui::SliderFloat("Tau", &Tweakables::g_Tau.Get(), 0, 5);

			ImGui::Combo("Tonemapper", (int*)&Tweakables::g_ToneMapper.Get(), [](void* /*data*/, int index, const char** outText)
				{
					constexpr static const char* tonemappers[] = {
						"Reinhard",
						"Reinhard Extended",
						"ACES Fast",
						"Unreal 3",
						"Uncharted 2",
					};

					if (index < (int)ARRAYSIZE(tonemappers))
					{
						*outText = tonemappers[index];
						return true;
					}
					noEntry();
					return false;
				}, nullptr, 5);
		}

		if (ImGui::CollapsingHeader("Misc"))
		{
			ImGui::Checkbox("TAA", &Tweakables::g_TAA.Get());
			ImGui::Checkbox("Debug Render Lights", &Tweakables::g_VisualizeLights.Get());
			ImGui::Checkbox("Visualize Light Density", &Tweakables::g_VisualizeLightDensity.Get());
			extern bool g_VisualizeClusters;
			ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);
			ImGui::SliderInt("SSR Samples", &Tweakables::g_SsrSamples.Get(), 0, 32);
			ImGui::Checkbox("Object Bounds", &Tweakables::g_RenderObjectBounds.Get());
			ImGui::Checkbox("Render Terrain", &Tweakables::g_RenderTerrain.Get());
			ImGui::Checkbox("Freeze Cluster Culling", &Tweakables::g_FreezeClusterCulling.Get());
		}

		if (ImGui::CollapsingHeader("Raytracing"))
		{
			if (m_pDevice->GetCapabilities().SupportsRaytracing())
			{
				ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO.Get());
				ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections.Get());
				ImGui::Checkbox("DDGI", &Tweakables::g_EnableDDGI.Get());
				if(m_DDGIVolumes.size() > 0)
					ImGui::SliderInt("DDGI RayCount", &m_DDGIVolumes.front().NumRays, 1, m_DDGIVolumes.front().MaxNumRays);
				ImGui::Checkbox("Visualize DDGI", &Tweakables::g_VisualizeDDGI.Get());
				ImGui::SliderAngle("TLAS Bounds Threshold", &Tweakables::g_TLASBoundsThreshold.Get(), 0, 40);
			}
		}
	}
	ImGui::End();
}

void DemoApp::UpdateTLAS(CommandContext& context)
{
	if (m_pDevice->GetCapabilities().SupportsRaytracing())
	{
		ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

		for (auto& pMesh : m_Meshes)
		{
			for (int i = 0; i < pMesh->GetMeshCount(); ++i)
			{
				SubMesh& subMesh = pMesh->GetMesh(i);
				if (!subMesh.pBLAS)
				{
					const Material& material = pMesh->GetMaterial(subMesh.MaterialId);
					D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
					geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
					geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
					if (material.AlphaMode == MaterialAlphaMode::Opaque)
					{
						geometryDesc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
					}
					geometryDesc.Triangles.IndexBuffer = subMesh.IndicesLocation.Location;
					geometryDesc.Triangles.IndexCount = subMesh.IndicesLocation.Elements;
					geometryDesc.Triangles.IndexFormat = subMesh.IndicesLocation.Format;
					geometryDesc.Triangles.Transform3x4 = 0;
					geometryDesc.Triangles.VertexBuffer.StartAddress = subMesh.PositionStreamLocation.Location;
					geometryDesc.Triangles.VertexBuffer.StrideInBytes = subMesh.PositionStreamLocation.Stride;
					geometryDesc.Triangles.VertexCount = subMesh.PositionStreamLocation.Elements;
					geometryDesc.Triangles.VertexFormat = subMesh.PositionsFormat;

					D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
					prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
					prebuildInfo.Flags =
						D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
						| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
					prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
					prebuildInfo.NumDescs = 1;
					prebuildInfo.pGeometryDescs = &geometryDesc;

					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
					m_pDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

					RefCountPtr<Buffer> pBLASScratch = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::NoBindless), "BLAS Scratch Buffer");
					RefCountPtr<Buffer> pBLAS = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::AccelerationStructure | BufferFlag::NoBindless), "BLAS Buffer");

					D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
					asDesc.Inputs = prebuildInfo;
					asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
					asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
					asDesc.SourceAccelerationStructureData = 0;

					pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
					context.InsertUavBarrier(subMesh.pBLAS);

					subMesh.pBLAS = pBLAS.Detach();
					subMesh.pBLASScratch = pBLASScratch.Detach();
				}
			}
		}

		context.FlushResourceBarriers();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
		for (uint32 instanceIndex = 0; instanceIndex < (uint32)m_SceneData.Batches.size(); ++instanceIndex)
		{
			const Batch& batch = m_SceneData.Batches[instanceIndex];

			if (m_RenderPath != RenderPath::PathTracing)
			{
				// Cull object that are small to the viewer - Deligiannis2019
				Vector3 cameraVec = (batch.Bounds.Center - m_pCamera->GetPosition());
				float angle = tanf(batch.Radius / cameraVec.Length());
				if (angle < Tweakables::g_TLASBoundsThreshold && cameraVec.Length() > batch.Radius)
				{
					continue;
				}
			}

			const SubMesh& subMesh = *batch.pMesh;

			if (!subMesh.pBLAS)
			{
				continue;
			}

			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
			instanceDesc.AccelerationStructure = subMesh.pBLAS->GetGpuHandle();
			instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			instanceDesc.InstanceContributionToHitGroupIndex = 0;
			instanceDesc.InstanceID = batch.InstanceData.World;
			instanceDesc.InstanceMask = 0xFF;

			// Hack
			if (batch.WorldMatrix.Determinant() < 0)
			{
				instanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
			}

			//The layout of Transform is a transpose of how affine matrices are typically stored in memory. Instead of four 3-vectors, Transform is laid out as three 4-vectors.
			auto ApplyTransform = [](const Matrix& m, D3D12_RAYTRACING_INSTANCE_DESC& desc)
			{
				Matrix transpose = m.Transpose();
				memcpy(&desc.Transform, &transpose, sizeof(float) * 12);
			};

			ApplyTransform(batch.WorldMatrix, instanceDesc);
			instanceDescs.push_back(instanceDesc);
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
		prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildInfo.Flags = buildFlags;
		prebuildInfo.NumDescs = (uint32)instanceDescs.size();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		m_pDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		if (!m_pTLAS || m_pTLAS->GetSize() < info.ResultDataMaxSizeInBytes)
		{
			m_pTLASScratch = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)), "TLAS Scratch");
			m_pTLAS = m_pDevice->CreateBuffer(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)), "TLAS");
		}

		DynamicAllocation allocation = context.AllocateTransientMemory(instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
		memcpy(allocation.pMappedMemory, instanceDescs.data(), instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
		asDesc.DestAccelerationStructureData = m_pTLAS->GetGpuHandle();
		asDesc.ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle();
		asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		asDesc.Inputs.Flags = buildFlags;
		asDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		asDesc.Inputs.InstanceDescs = allocation.GpuHandle;
		asDesc.Inputs.NumDescs = (uint32)instanceDescs.size();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		context.InsertUavBarrier(m_pTLAS);
	}
}

void DemoApp::LoadMesh(const std::string& filePath, CommandContext& context)
{
	std::unique_ptr<Mesh> pMesh = std::make_unique<Mesh>();
	pMesh->Load(filePath.c_str(), m_pDevice, &context, 1.0f);
	m_Meshes.push_back(std::move(pMesh));
}

void DemoApp::CreateShadowViews()
{
	PROFILE_SCOPE("Shadow Setup");

	float minPoint = 0;
	float maxPoint = 1;

	const uint32 numCascades = Tweakables::g_ShadowCascades;
	const float pssmLambda = Tweakables::g_PSSMFactor;
	m_SceneData.NumShadowCascades = numCascades;
	m_SceneData.ShadowViews.clear();

	if (Tweakables::g_SDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % FRAME_COUNT];
		Vector2* pData = (Vector2*)pSourceBuffer->GetMappedData();
		minPoint = pData->x;
		maxPoint = pData->y;
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

	const Matrix vpInverse = m_pCamera->GetProjectionInverse() * m_pCamera->GetViewInverse();
	for (size_t lightIndex = 0; lightIndex < m_Lights.size(); ++lightIndex)
	{
		Light& light = m_Lights[lightIndex];
		if (!light.CastShadows)
		{
			continue;
		}
		uint32 shadowIndex = (uint32)m_SceneData.ShadowViews.size();
		light.ShadowIndex = shadowIndex;
		if (light.Type == LightType::Directional)
		{
			// Frustum corners in world space
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
			const Matrix lightView = Math::CreateLookToMatrix(Vector3::Zero, light.Direction, Vector3::Up);

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
				{
					center += corner;
				}
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
				minExtents = Math::VectorFloor(minExtents / texelSize) * texelSize;
				maxExtents = Math::VectorFloor(maxExtents / texelSize) * texelSize;
				center = (minExtents + maxExtents) * 0.5f;

				// Extent the Z bounds
				float extentsZ = fabs(center.z - minExtents.z);
				extentsZ = Math::Max(extentsZ, Math::Min(1500.0f, farPlane) * 0.5f);
				minExtents.z = center.z - extentsZ;
				maxExtents.z = center.z + extentsZ;

				Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z);

				ShadowView shadowView;
				shadowView.IsPerspective = false;
				shadowView.ViewProjection = lightView * projectionMatrix;

				static_cast<float*>(&m_SceneData.ShadowCascadeDepths.x)[i] = nearPlane + currentCascadeSplit * (farPlane - nearPlane);
				m_SceneData.ShadowViews.push_back(shadowView);
			}
		}
		else if (light.Type == LightType::Spot)
		{
			Matrix projection = Math::CreatePerspectiveMatrix(light.UmbraAngleDegrees * Math::DegreesToRadians, 1.0f, light.Range, 1.0f);
			Matrix view = Math::CreateLookToMatrix(light.Position, light.Direction, light.Direction == Vector3::Up ? Vector3::Right : Vector3::Up);

			ShadowView shadowView;
			shadowView.IsPerspective = true;
			shadowView.ViewProjection = view * projection;
			BoundingFrustum::CreateFromMatrix(shadowView.PerspectiveFrustum, projection);
			shadowView.PerspectiveFrustum.Transform(shadowView.PerspectiveFrustum, view.Invert());
			m_SceneData.ShadowViews.push_back(shadowView);
		}
		else if (light.Type == LightType::Point)
		{
			Matrix viewMatrices[] = {
				Math::CreateLookToMatrix(light.Position, Vector3::Left, Vector3::Up),
				Math::CreateLookToMatrix(light.Position, Vector3::Right, Vector3::Up),
				Math::CreateLookToMatrix(light.Position, Vector3::Down, Vector3::Backward),
				Math::CreateLookToMatrix(light.Position, Vector3::Up, Vector3::Forward),
				Math::CreateLookToMatrix(light.Position, Vector3::Backward, Vector3::Up),
				Math::CreateLookToMatrix(light.Position, Vector3::Forward, Vector3::Up),
			};
			Matrix projection = Math::CreatePerspectiveMatrix(Math::PIDIV2, 1, light.Range, 1.0f);

			for (int i = 0; i < 6; ++i)
			{
				ShadowView shadowView;
				shadowView.IsPerspective = true;
				shadowView.ViewProjection = viewMatrices[i] * projection;
				BoundingFrustum::CreateFromMatrix(shadowView.PerspectiveFrustum, projection);
				shadowView.PerspectiveFrustum.Transform(shadowView.PerspectiveFrustum, viewMatrices[i].Invert());
				m_SceneData.ShadowViews.push_back(shadowView);
			}
		}
	}

	if (m_SceneData.ShadowViews.size() > (int)m_ShadowMaps.size())
	{
		m_ShadowMaps.resize(m_SceneData.ShadowViews.size());
		int i = 0;
		for (auto& pShadowMap : m_ShadowMaps)
		{
			int size = i < 4 ? 2048 : 512;
			pShadowMap = m_pDevice->CreateTexture(TextureDesc::CreateDepth(size, size, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)), "Shadow Map");
			++i;
		}
	}

	for (Light& light : m_Lights)
	{
		if (light.ShadowIndex >= 0)
		{
			light.ShadowMapSize = m_ShadowMaps[light.ShadowIndex]->GetWidth();
		}
	}
	m_SceneData.ShadowMapOffset = m_ShadowMaps[0]->GetSRVIndex();

		
}

void DemoApp::UploadSceneData(CommandContext& context)
{
	std::vector<ShaderInterop::MaterialData> materials;
	std::vector<ShaderInterop::MeshData> meshes;
	std::vector<ShaderInterop::MeshInstance> meshInstances;
	std::vector<Batch> sceneBatches;
	std::vector<Matrix> transforms;

	for (const auto& pMesh : m_Meshes)
	{
		for (const SubMeshInstance& node : pMesh->GetMeshInstances())
		{
			const SubMesh& parentMesh = pMesh->GetMesh(node.MeshIndex);
			const Material& meshMaterial = pMesh->GetMaterial(parentMesh.MaterialId);
			ShaderInterop::MeshInstance meshInstance;
			meshInstance.Mesh = (uint32)meshes.size() + node.MeshIndex;
			meshInstance.Material = (uint32)materials.size() + parentMesh.MaterialId;
			meshInstance.World = (uint32)transforms.size();
			meshInstances.push_back(meshInstance);

			transforms.push_back(node.Transform);

			auto GetBlendMode = [](MaterialAlphaMode mode) {
				switch (mode)
				{
				case MaterialAlphaMode::Blend: return Batch::Blending::AlphaBlend;
				case MaterialAlphaMode::Opaque: return Batch::Blending::Opaque;
				case MaterialAlphaMode::Masked: return Batch::Blending::AlphaMask;
				}
				return Batch::Blending::Opaque;
			};

			Batch batch;
			batch.InstanceData = meshInstance;
			batch.LocalBounds = parentMesh.Bounds;
			batch.pMesh = &parentMesh;
			batch.BlendMode = GetBlendMode(meshMaterial.AlphaMode);
			batch.WorldMatrix = node.Transform;
			batch.LocalBounds.Transform(batch.Bounds, batch.WorldMatrix);
			batch.Radius = Vector3(batch.Bounds.Extents).Length();
			sceneBatches.push_back(batch);
		}

		for (const SubMesh& subMesh : pMesh->GetMeshes())
		{
			ShaderInterop::MeshData mesh;
			mesh.BufferIndex = pMesh->GetData()->GetSRVIndex();
			mesh.IndexByteSize = subMesh.IndicesLocation.Stride();
			mesh.IndicesOffset = (uint32)subMesh.IndicesLocation.OffsetFromStart;
			mesh.PositionsOffset = (uint32)subMesh.PositionStreamLocation.OffsetFromStart;
			mesh.NormalsOffset = (uint32)subMesh.NormalStreamLocation.OffsetFromStart;
			mesh.ColorsOffset = (uint32)subMesh.ColorsStreamLocation.OffsetFromStart;
			mesh.UVsOffset = (uint32)subMesh.UVStreamLocation.OffsetFromStart;
			mesh.MeshletOffset = subMesh.MeshletsLocation;
			mesh.MeshletVertexOffset = subMesh.MeshletVerticesLocation;
			mesh.MeshletTriangleOffset = subMesh.MeshletTrianglesLocation;
			mesh.MeshletBoundsOffset = subMesh.MeshletBoundsLocation;
			mesh.MeshletCount = subMesh.NumMeshlets;
			meshes.push_back(mesh);
		}

		for (const Material& material : pMesh->GetMaterials())
		{
			ShaderInterop::MaterialData materialData;
			materialData.Diffuse = material.pDiffuseTexture ? material.pDiffuseTexture->GetSRVIndex() : -1;
			materialData.Normal = material.pNormalTexture ? material.pNormalTexture->GetSRVIndex() : -1;
			materialData.RoughnessMetalness = material.pRoughnessMetalnessTexture ? material.pRoughnessMetalnessTexture->GetSRVIndex() : -1;
			materialData.Emissive = material.pEmissiveTexture ? material.pEmissiveTexture->GetSRVIndex() : -1;
			materialData.BaseColorFactor = material.BaseColorFactor;
			materialData.MetalnessFactor = material.MetalnessFactor;
			materialData.RoughnessFactor = material.RoughnessFactor;
			materialData.EmissiveFactor = material.EmissiveFactor;
			materialData.AlphaCutoff = material.AlphaCutoff;
			materials.push_back(materialData);
		}
	}

	std::vector<ShaderInterop::DDGIVolume> ddgiVolumes;
	if (Tweakables::g_EnableDDGI)
	{
		for (DDGIVolume& ddgiVolume : m_DDGIVolumes)
		{
			ShaderInterop::DDGIVolume ddgi{};
			ddgi.BoundsMin = ddgiVolume.Origin - ddgiVolume.Extents;
			ddgi.ProbeSize = 2 * ddgiVolume.Extents / (Vector3((float)ddgiVolume.NumProbes.x, (float)ddgiVolume.NumProbes.y, (float)ddgiVolume.NumProbes.z) - Vector3::One);
			ddgi.ProbeVolumeDimensions = TIntVector3<uint32>(ddgiVolume.NumProbes.x, ddgiVolume.NumProbes.y, ddgiVolume.NumProbes.z);
			ddgi.IrradianceIndex = ddgiVolume.pIrradiance[0] ? ddgiVolume.pIrradiance[0]->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
			ddgi.DepthIndex = ddgiVolume.pDepth[0] ? ddgiVolume.pDepth[0]->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
			ddgi.ProbeOffsetIndex = ddgiVolume.pProbeOffset ? ddgiVolume.pProbeOffset->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
			ddgi.ProbeStatesIndex = ddgiVolume.pProbeStates ? ddgiVolume.pProbeStates->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
			ddgi.NumRaysPerProbe = ddgiVolume.NumRays;
			ddgi.MaxRaysPerProbe = ddgiVolume.MaxNumRays;
			ddgiVolumes.push_back(ddgi);
		}
	}
	m_SceneData.NumDDGIVolumes = (uint32)ddgiVolumes.size();

	sceneBatches.swap(m_SceneData.Batches);

	if (!m_pDDGIVolumesBuffer || ddgiVolumes.size() > m_DDGIVolumes.size())
	{
		m_pDDGIVolumesBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)ddgiVolumes.size()), sizeof(ShaderInterop::DDGIVolume), BufferFlag::ShaderResource), "DDGI Volumes");
	}
	context.WriteBuffer(m_pDDGIVolumesBuffer, ddgiVolumes.data(), ddgiVolumes.size() * sizeof(ShaderInterop::DDGIVolume));

	if (!m_pMeshBuffer || meshes.size() > m_pMeshBuffer->GetNumElements())
	{
		m_pMeshBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)meshes.size()), sizeof(ShaderInterop::MeshData), BufferFlag::ShaderResource), "Meshes");
	}
	context.WriteBuffer(m_pMeshBuffer, meshes.data(), meshes.size() * sizeof(ShaderInterop::MeshData));

	if (!m_pMeshInstanceBuffer || meshInstances.size() > m_pMeshInstanceBuffer->GetNumElements())
	{
		m_pMeshInstanceBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)meshInstances.size()), sizeof(ShaderInterop::MeshInstance), BufferFlag::ShaderResource), "Mesh Instances");
	}
	context.WriteBuffer(m_pMeshInstanceBuffer, meshInstances.data(), meshInstances.size() * sizeof(ShaderInterop::MeshInstance));

	if (!m_pMaterialBuffer || materials.size() > m_pMaterialBuffer->GetNumElements())
	{
		m_pMaterialBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)materials.size()), sizeof(ShaderInterop::MaterialData), BufferFlag::ShaderResource), "Materials");
	}
	context.WriteBuffer(m_pMaterialBuffer, materials.data(), materials.size() * sizeof(ShaderInterop::MaterialData));

	if (!m_pTransformsBuffer || transforms.size() > m_pTransformsBuffer->GetNumElements())
	{
		m_pTransformsBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)transforms.size()), sizeof(Matrix), BufferFlag::ShaderResource), "Transforms");
	}
	context.WriteBuffer(m_pTransformsBuffer, transforms.data(), transforms.size() * sizeof(Matrix));

	std::vector<ShaderInterop::Light> lightData;
	Utils::Transform(m_Lights, lightData, [](const Light& light) { return light.GetData(); });

	if (!m_pLightBuffer || lightData.size() > m_pLightBuffer->GetNumElements())
	{
		m_pLightBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)lightData.size()), sizeof(ShaderInterop::Light), BufferFlag::ShaderResource), "Lights");
	}
	context.WriteBuffer(m_pLightBuffer, lightData.data(), lightData.size() * sizeof(ShaderInterop::Light));

	UpdateTLAS(context);

	m_SceneData.pLightBuffer = m_pLightBuffer;
	m_SceneData.pMaterialBuffer = m_pMaterialBuffer;
	m_SceneData.pMeshBuffer = m_pMeshBuffer;
	m_SceneData.pTransformsBuffer = m_pTransformsBuffer;
	m_SceneData.pMeshInstanceBuffer = m_pMeshInstanceBuffer;
	m_SceneData.pSceneTLAS = m_pTLAS;
	m_SceneData.pSky = m_pSkyTexture;
	m_SceneData.pDDGIVolumesBuffer = m_pDDGIVolumesBuffer;
}
