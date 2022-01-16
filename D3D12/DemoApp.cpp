#include "stdafx.h"
#include "DemoApp.h"
#include "Scene/Camera.h"
#include "ImGuizmo.h"
#include "Content/Image.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/Profiler.h"
#include "Graphics/Mesh.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
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
#include "Graphics/MaterialGraph/MaterialGraph.h"
#include "Graphics/MaterialGraph/Expressions.h"

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

	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	Matrix view = camera.GetView();
	Matrix projection = camera.GetProjection();
	Math::ReverseZProjection(projection);
	ImGuizmo::Manipulate(&view.m[0][0], &projection.m[0][0], mCurrentGizmoOperation, mCurrentGizmoMode, &matrix.m[0][0], NULL, pSnapValue);
}

namespace Tweakables
{
	// Post processing
	ConsoleVariable g_WhitePoint("r.Exposure.WhitePoint", 1.0f);
	ConsoleVariable g_MinLogLuminance("r.Exposure.MinLogLuminance", -10.0f);
	ConsoleVariable g_MaxLogLuminance("r.Exposure.MaxLogLuminance", 20.0f);
	ConsoleVariable g_Tau("r.Exposure.Tau", 2.0f);
	ConsoleVariable g_DrawHistogram("vis.Histogram", false);
	ConsoleVariable g_ToneMapper("r.Tonemapper", 1);
	ConsoleVariable g_TAA("r.Taa", true);

	// Shadows
	ConsoleVariable g_SDSM("r.Shadows.SDSM", false);
	ConsoleVariable g_StabilizeCascades("r.Shadows.StabilizeCascades", true);
	ConsoleVariable g_VisualizeShadowCascades("vis.ShadowCascades", false);
	ConsoleVariable g_ShadowCascades("r.Shadows.CascadeCount", 4);
	ConsoleVariable g_PSSMFactor("r.Shadow.PSSMFactor", 1.0f);

	// Bloom
	ConsoleVariable g_Bloom("r.Bloom", true);
	ConsoleVariable g_BloomThreshold("r.Bloom.Threshold", 1.0f);
	ConsoleVariable g_BloomMaxBrightness("r.Bloom.MaxBrightness", 10.0f);

	// Misc Lighting
	ConsoleVariable g_VolumetricFog("r.VolumetricFog", true);
	ConsoleVariable g_RaytracedAO("r.Raytracing.AO", false);
	ConsoleVariable g_VisualizeLights("vis.Lights", false);
	ConsoleVariable g_VisualizeLightDensity("vis.LightDensity", false);
	ConsoleVariable g_RenderObjectBounds("r.vis.ObjectBounds", false);

	ConsoleVariable g_RaytracedReflections("r.Raytracing.Reflections", true);
	ConsoleVariable g_TLASBoundsThreshold("r.Raytracing.TLASBoundsThreshold", 5.0f * Math::DegreesToRadians);
	ConsoleVariable g_SsrSamples("r.SSRSamples", 8);
	ConsoleVariable g_RenderTerrain("r.Terrain", false);

	// Misc
	bool g_DumpRenderGraph = false;
	DelegateConsoleCommand<> gDumpRenderGraph("DumpRenderGraph", []() { g_DumpRenderGraph = true; });
	bool g_Screenshot = false;
	DelegateConsoleCommand<> gScreenshot("Screenshot", []() { g_Screenshot = true; });

	// Lighting
	float g_SunInclination = 0.579f;
	float g_SunOrientation = -3.055f;
	float g_SunTemperature = 5900.0f;
	float g_SunIntensity = 3.0f;
}

DemoApp::DemoApp(WindowHandle window, const IntVector2& windowRect, int sampleCount /*= 1*/)
	: m_SampleCount(sampleCount), m_Window(window)
{
	// #todo fixup MSAA :(
	checkf(sampleCount == 1, "I broke MSAA! TODO");

	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetNearPlane(80.0f);
	m_pCamera->SetFarPlane(0.1f);

	E_LOG(Info, "Graphics::InitD3D()");

	GraphicsInstanceFlags instanceFlags = GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("d3ddebug") ? GraphicsInstanceFlags::DebugDevice : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("dred") ? GraphicsInstanceFlags::DRED : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("gpuvalidation") ? GraphicsInstanceFlags::GpuValidation : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("pix") ? GraphicsInstanceFlags::Pix : GraphicsInstanceFlags::None;
	std::unique_ptr<GraphicsInstance> instance = GraphicsInstance::CreateInstance(instanceFlags);

	ComPtr<IDXGIAdapter4> pAdapter = instance->EnumerateAdapter(CommandLine::GetBool("warp"));
	m_pDevice = instance->CreateDevice(pAdapter.Get());
	m_pSwapchain = instance->CreateSwapchain(m_pDevice.get(), window, windowRect.x, windowRect.y, FRAME_COUNT, true);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(m_pDevice.get(), window, FRAME_COUNT);

	m_pClusteredForward = std::make_unique<ClusteredForward>(m_pDevice.get());
	m_pTiledForward = std::make_unique<TiledForward>(m_pDevice.get());
	m_pRTReflections = std::make_unique<RTReflections>(m_pDevice.get());
	m_pRTAO = std::make_unique<RTAO>(m_pDevice.get());
	m_pSSAO = std::make_unique<SSAO>(m_pDevice.get());
	m_pParticles = std::make_unique<GpuParticles>(m_pDevice.get());
	m_pPathTracing = std::make_unique<PathTracing>(m_pDevice.get());
	m_pCBTTessellation = std::make_unique<CBTTessellation>(m_pDevice.get());

	Profiler::Get()->Initialize(m_pDevice.get(), FRAME_COUNT);
	DebugRenderer::Get()->Initialize(m_pDevice.get());

	OnResize(windowRect.x, windowRect.y);
	OnResizeViewport(windowRect.x, windowRect.y);

	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	InitializePipelines();
	SetupScene(*pContext);
	pContext->Execute(true);

	Tweakables::g_RaytracedAO = m_pDevice->GetCapabilities().SupportsRaytracing() ? Tweakables::g_RaytracedAO : false;
	Tweakables::g_RaytracedReflections = m_pDevice->GetCapabilities().SupportsRaytracing() ? Tweakables::g_RaytracedReflections : false;
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
#elif 0
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

	{
		Vector3 Position(-1, 1, 0);
		Vector3 Direction;
		Position.Normalize(Direction);
		Light sunLight = Light::Point(Position, 2, 30);
		sunLight.VolumetricLighting = true;
		m_Lights.push_back(sunLight);
	}

#if 0
	for (int i = 0; i < 50; ++i)
	{
		Vector3 loc(
			Math::RandomRange(-10.0f, 10.0f),
			Math::RandomRange(-4.0f, 5.0f),
			Math::RandomRange(-10.0f, 10.0f)
		);
		Light spotLight = Light::Spot(loc, 100, Vector3(0, 1, 0), 65, 50, 1000, Color(1.0f, 0.7f, 0.3f, 1.0f));
		//spotLight.CastShadows = true;
		//spotLight.LightTexture = m_pDevice->RegisterBindlessResource(m_pLightCookie.get(), GetDefaultTexture(DefaultTexture::White2D));
		spotLight.VolumetricLighting = true;
		m_Lights.push_back(spotLight);
	}
#endif
}

void DemoApp::Update()
{
	PROFILE_BEGIN("Update");
	m_pImGuiRenderer->NewFrame(m_WindowWidth, m_WindowHeight);
	m_pDevice->GetShaderManager()->ConditionallyReloadShaders();
	UpdateImGui();
	m_pCamera->Update();

#if 0
	static int selectedBatch = -1;
	Ray camRay = m_pCamera->GetMouseRay(m_WindowWidth, m_WindowHeight);
	float minDist = FLT_MAX;

	if (Input::Instance().IsMousePressed(0))
	{
		if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
		{
			selectedBatch = -1;
			for (Batch& b : m_SceneData.Batches)
			{
				float distance = 0;
				if (!b.Bounds.Contains(camRay.position) && camRay.Intersects(b.Bounds, distance))
				{
					distance = Vector3::Distance(camRay.position + distance * camRay.direction, b.Bounds.Center);
					if (distance < minDist)
					{
						selectedBatch = b.Index;
						minDist = distance;
					}
				}
			}
		}
	}

	if (selectedBatch >= 0)
	{
		Batch& b = m_SceneData.Batches[selectedBatch];
		EditTransform(*m_pCamera, b.WorldMatrix);
		DebugRenderer::Get()->AddBoundingBox(b.Bounds, Color(1, 0, 1, 1));
	}
#endif

	if (Input::Instance().IsKeyPressed('H'))
	{
		m_RenderPath = m_RenderPath == RenderPath::PathTracing ? RenderPath::Clustered : RenderPath::PathTracing;
	}
	if (m_RenderPath == RenderPath::PathTracing && !m_pPathTracing->IsSupported())
	{
		m_RenderPath = RenderPath::Clustered;
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

	if (Tweakables::g_VisualizeLights)
	{
		for (const Light& light : m_Lights)
		{
			DebugRenderer::Get()->AddLight(light);
		}
	}

	// SHADOW MAP PARTITIONING
	/////////////////////////////////////////

	ShadowData shadowData;
	int shadowIndex = 0;

	{
		PROFILE_SCOPE("Shadow Setup");

		float minPoint = 0;
		float maxPoint = 1;

		shadowData.NumCascades = Tweakables::g_ShadowCascades.Get();

		if (Tweakables::g_SDSM)
		{
			Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % FRAME_COUNT].get();
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

		for (int i = 0; i < Tweakables::g_ShadowCascades; ++i)
		{
			float p = (i + 1) / (float)Tweakables::g_ShadowCascades;
			float log = minZ * std::pow(maxZ / minZ, p);
			float uniform = minZ + (maxZ - minZ) * p;
			float d = Tweakables::g_PSSMFactor * (log - uniform) + uniform;
			cascadeSplits[i] = d - nearPlane;
		}

		Matrix vpInverse = m_pCamera->GetProjectionInverse() * m_pCamera->GetViewInverse();

		for (size_t lightIndex = 0; lightIndex < m_Lights.size(); ++lightIndex)
		{
			Light& light = m_Lights[lightIndex];
			if (!light.CastShadows)
			{
				continue;
			}
			light.ShadowIndex = shadowIndex;
			if (light.Type == LightType::Directional)
			{
				for (int i = 0; i < Tweakables::g_ShadowCascades; ++i)
				{
					float previousCascadeSplit = i == 0 ? minPoint : cascadeSplits[i - 1];
					float currentCascadeSplit = cascadeSplits[i];

					Vector3 frustumCorners[] = {
						//near
						Vector3(-1, -1, 1),
						Vector3(-1, 1, 1),
						Vector3(1, 1, 1),
						Vector3(1, -1, 1),

						//far
						Vector3(-1, -1, 0),
						Vector3(-1, 1, 0),
						Vector3(1, 1, 0),
						Vector3(1, -1, 0),
					};

					//Retrieve frustum corners in world space
					for (Vector3& corner : frustumCorners)
					{
						corner = Vector3::Transform(corner, vpInverse);
					}

					//Adjust frustum corners based on cascade splits
					for (int j = 0; j < 4; ++j)
					{
						Vector3 cornerRay = (frustumCorners[j + 4] - frustumCorners[j]);
						cornerRay.Normalize();
						Vector3 nearPoint = previousCascadeSplit * cornerRay;
						Vector3 farPoint = currentCascadeSplit * cornerRay;
						frustumCorners[j + 4] = frustumCorners[j] + farPoint;
						frustumCorners[j] = frustumCorners[j] + nearPoint;
					}

					Vector3 center = Vector3::Zero;
					for (const Vector3& corner : frustumCorners)
					{
						center += corner;
					}
					center /= 8;

					Vector3 minExtents(FLT_MAX);
					Vector3 maxExtents(-FLT_MAX);

					//Create a bounding sphere to maintain aspect in projection to avoid flickering when rotating
					if (Tweakables::g_StabilizeCascades)
					{
						float radius = 0;
						for (const Vector3& corner : frustumCorners)
						{
							float dist = Vector3::Distance(center, corner);
							radius = Math::Max(dist, radius);
						}
						maxExtents = Vector3(radius, radius, radius);
						minExtents = -maxExtents;
					}
					else
					{
						Matrix lightView = Math::CreateLookToMatrix(center, light.Direction, Vector3::Up);
						for (const Vector3& corner : frustumCorners)
						{
							Vector3 p = Vector3::Transform(corner, lightView);
							minExtents = Vector3::Min(minExtents, p);
							maxExtents = Vector3::Max(maxExtents, p);
						}
					}

					Matrix shadowView = Math::CreateLookToMatrix(center + light.Direction * -100, light.Direction, Vector3::Up);

					Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z + 200, 0);

					Matrix lightViewProjection = shadowView * projectionMatrix;

					//Snap projection to shadowmap texels to avoid flickering edges
					if (Tweakables::g_StabilizeCascades)
					{
						float shadowMapSize = 2048;
						Vector4 shadowOrigin = Vector4::Transform(Vector4(0, 0, 0, 1), lightViewProjection);
						shadowOrigin *= shadowMapSize / 2.0f;
						Vector4 rounded = XMVectorRound(shadowOrigin);
						Vector4 roundedOffset = rounded - shadowOrigin;
						roundedOffset *= 2.0f / shadowMapSize;
						roundedOffset.z = 0;
						roundedOffset.w = 0;

						projectionMatrix *= Matrix::CreateTranslation(Vector3(roundedOffset));
						lightViewProjection = shadowView * projectionMatrix;
					}
					static_cast<float*>(&shadowData.CascadeDepths.x)[shadowIndex] = currentCascadeSplit;
					shadowData.LightViewProjections[shadowIndex++] = lightViewProjection;
				}
			}
			else if (light.Type == LightType::Spot)
			{
				Matrix projection = Math::CreatePerspectiveMatrix(light.UmbraAngleDegrees * Math::DegreesToRadians, 1.0f, light.Range, 1.0f);
				shadowData.LightViewProjections[shadowIndex++] = Math::CreateLookToMatrix(light.Position, light.Direction, light.Direction == Vector3::Up ? Vector3::Right : Vector3::Up) * projection;
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
					shadowData.LightViewProjections[shadowIndex] = viewMatrices[i] * projection;
					++shadowIndex;
				}
			}
		}

		if (shadowIndex > (int)m_ShadowMaps.size())
		{
			m_ShadowMaps.resize(shadowIndex);
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
		shadowData.ShadowMapOffset = m_ShadowMaps[0]->GetSRVIndex();
	}

	{
		PROFILE_SCOPE("Frustum Culling");
		BoundingFrustum frustum = m_pCamera->GetFrustum();
		for (const Batch& b : m_SceneData.Batches)
		{
			m_SceneData.VisibilityMask.AssignBit(b.InstanceData.World, frustum.Contains(b.Bounds));
		}
	}

	m_SceneData.View = m_pCamera->GetViewTransform();
	m_SceneData.ShadowData = shadowData;
	m_SceneData.FrameIndex = m_Frame;

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////

	if (Tweakables::g_Screenshot)
	{
		Tweakables::g_Screenshot = false;

		CommandContext* pContext = m_pDevice->AllocateCommandContext();
		Texture* pSource = m_pTonemapTarget.get();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
		D3D12_RESOURCE_DESC resourceDesc = m_pTonemapTarget->GetResource()->GetDesc();
		m_pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
		std::unique_ptr<Buffer> pScreenshotBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
		pScreenshotBuffer->Map();
		pContext->InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		pContext->InsertResourceBarrier(pScreenshotBuffer.get(), D3D12_RESOURCE_STATE_COPY_DEST);
		pContext->CopyTexture(m_pTonemapTarget.get(), pScreenshotBuffer.get(), CD3DX12_BOX(0, 0, m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight()));

		ScreenshotRequest request;
		request.Width = pSource->GetWidth();
		request.Height = pSource->GetHeight();
		request.RowPitch = textureFootprint.Footprint.RowPitch;
		request.pBuffer = pScreenshotBuffer.release();
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
		RGGraph graph(m_pDevice.get());
		RGPassBuilder updateScenePass = graph.AddPass("Update GPU Scene");
		updateScenePass.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				UploadSceneData(context);
			});
		graph.Compile();
		graph.Execute();
	}

	RGGraph graph(m_pDevice.get());
	struct MainData
	{
		RGResourceHandle DepthStencil;
		RGResourceHandle DepthStencilResolved;
	};
	MainData Data;
	Data.DepthStencil = graph.ImportTexture("Depth Stencil", GetDepthStencil());
	Data.DepthStencilResolved = graph.ImportTexture("Resolved Depth Stencil", GetResolvedDepthStencil());

	if (m_RenderPath != RenderPath::PathTracing)
	{
		// PARTICLES GPU SIM
		m_pParticles->Simulate(graph, m_SceneData, GetResolvedDepthStencil());

		// SHADOWS
		RGPassBuilder shadows = graph.AddPass("Shadow Mapping");
		shadows.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pShadowsRS.get());

				// hack - copy the main viewport and then just modify the viewproj
				SceneView view = m_SceneData;

				for (int i = 0; i < shadowIndex; ++i)
				{
					GPU_PROFILE_SCOPE("Light View", &context);
					Texture* pShadowmap = m_ShadowMaps[i].get();
					context.InsertResourceBarrier(pShadowmap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
					context.BeginRenderPass(RenderPassInfo(pShadowmap, RenderPassAccess::Clear_Store));

					view.View.ViewProjection = shadowData.LightViewProjections[i];
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
						DrawScene(context, m_SceneData, mask, Batch::Blending::AlphaMask);
					}
					context.EndRenderPass();
				}
			});

		//DEPTH PREPASS
		// - Depth only pass that renders the entire scene
		// - Optimization that prevents wasteful lighting calculations during the base pass
		// - Required for light culling
		RGPassBuilder prepass = graph.AddPass("Depth Prepass");
		Data.DepthStencil = prepass.Write(Data.DepthStencil);
		prepass.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
				context.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

				context.BeginRenderPass(RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store));
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				context.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

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

		//[WITH MSAA] DEPTH RESOLVE
		// - If MSAA is enabled, run a compute shader to resolve the depth buffer
		if (m_SampleCount > 1)
		{
			RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
			Data.DepthStencil = depthResolve.Read(Data.DepthStencil);
			Data.DepthStencilResolved = depthResolve.Write(Data.DepthStencilResolved);
			depthResolve.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					Texture* pDepthTexture = resources.GetTexture(Data.DepthStencil);
					Texture* pResolvedDepthTexture = resources.GetTexture(Data.DepthStencilResolved);
					context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(pResolvedDepthTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetComputeRootSignature(m_pResolveDepthRS.get());
					context.SetPipelineState(m_pResolveDepthPSO);

					context.BindResource(0, 0, pResolvedDepthTexture->GetUAV());
					context.BindResource(1, 0, resources.GetTexture(Data.DepthStencil)->GetSRV());

					context.Dispatch(ComputeUtils::GetNumThreadGroups(pDepthTexture->GetWidth(), 16, pDepthTexture->GetHeight(), 16));

					context.InsertResourceBarrier(pResolvedDepthTexture, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
					context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_READ);
					context.FlushResourceBarriers();
				});
		}
		else
		{
			RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
			depthResolve.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					context.CopyTexture(GetDepthStencil(), GetResolvedDepthStencil());
				});
		}

		RGPassBuilder cameraMotion = graph.AddPass("Camera Motion");
		cameraMotion.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(GetResolvedDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pCameraMotionRS.get());
				context.SetPipelineState(m_pCameraMotionPSO);

				context.SetRootCBV(0, GetViewUniforms(m_SceneData, m_pVelocity.get()));

				context.BindResource(1, 0, m_pVelocity->GetUAV());
				context.BindResource(2, 0, GetResolvedDepthStencil()->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pVelocity->GetWidth(), 8, m_pVelocity->GetHeight(), 8));
			});

		if (Tweakables::g_RaytracedAO)
		{
			m_pRTAO->Execute(graph, m_SceneData, m_pAmbientOcclusion.get(), GetResolvedDepthStencil());
		}
		else
		{
			m_pSSAO->Execute(graph, m_SceneData, m_pAmbientOcclusion.get(), GetResolvedDepthStencil());
		}

		if (m_RenderPath == RenderPath::Tiled)
		{
			TiledForwardParameters params;
			params.pAmbientOcclusion = m_pAmbientOcclusion.get();
			params.pColorTarget = GetCurrentRenderTarget();
			params.pDepth = GetDepthStencil();
			params.pResolvedDepth = GetResolvedDepthStencil();
			params.pNormalsTarget = m_pNormals ? m_pNormals.get() : m_pResolvedNormals.get();
			params.pResolvedNormalsTarget = m_pResolvedNormals.get();
			params.pPreviousColorTarget = m_pPreviousColor.get();
			m_pTiledForward->Execute(graph, m_SceneData, params);
		}
		else if (m_RenderPath == RenderPath::Clustered)
		{
			ClusteredForwardParameters params;
			params.pAmbientOcclusion = m_pAmbientOcclusion.get();
			params.pColorTarget = GetCurrentRenderTarget();
			params.pDepth = GetDepthStencil();
			params.pResolvedDepth = GetResolvedDepthStencil();
			params.pNormalsTarget = m_pNormals ? m_pNormals.get() : m_pResolvedNormals.get();
			params.pResolvedNormalsTarget = m_pResolvedNormals.get();
			params.pPreviousColorTarget = m_pPreviousColor.get();
			m_pClusteredForward->Execute(graph, m_SceneData, params);
		}

		m_pParticles->Render(graph, m_SceneData, GetCurrentRenderTarget(), GetDepthStencil());

		if (Tweakables::g_RenderTerrain.GetBool())
		{
			m_pCBTTessellation->Execute(graph, GetCurrentRenderTarget(), GetDepthStencil(), m_SceneData);
		}

		RGPassBuilder sky = graph.AddPass("Sky");
		sky.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pDepthStencil = GetDepthStencil();
				context.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

				RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_Store, false);

				context.BeginRenderPass(info);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pSkyboxRS.get());
				context.SetPipelineState(m_pSkyboxPSO);

				context.SetRootCBV(0, GetViewUniforms(m_SceneData, GetCurrentRenderTarget()));

				context.Draw(0, 36);

				context.EndRenderPass();
			});

		DebugRenderer::Get()->Render(graph, m_SceneData, GetCurrentRenderTarget(), GetDepthStencil());
	}
	else
	{
		m_pPathTracing->Render(graph, m_SceneData, GetCurrentRenderTarget());
	}

	RGPassBuilder resolve = graph.AddPass("Resolve");
	resolve.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			if (m_SampleCount > 1)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				Texture* pTarget = Tweakables::g_TAA.Get() ? m_pTAASource.get() : m_pHDRRenderTarget.get();
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, pTarget, 0, pTarget->GetFormat());
			}

			if (!Tweakables::g_TAA.Get())
			{
				context.CopyTexture(m_pHDRRenderTarget.get(), m_pPreviousColor.get());
			}
			else
			{
				context.CopyTexture(m_pHDRRenderTarget.get(), m_pTAASource.get());
			}
		});

	if (m_RenderPath != RenderPath::PathTracing)
	{
		if (Tweakables::g_RaytracedReflections)
		{
			Texture* pTarget = Tweakables::g_TAA.Get() ? m_pTAASource.get() : m_pHDRRenderTarget.get();
			m_pRTReflections->Execute(graph, m_SceneData, pTarget, m_pResolvedNormals.get(), GetResolvedDepthStencil());
		}

		if (Tweakables::g_TAA.Get())
		{
			RGPassBuilder temporalResolve = graph.AddPass("Temporal Resolve");
			temporalResolve.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					context.InsertResourceBarrier(m_pTAASource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pPreviousColor.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

					context.SetComputeRootSignature(m_pTemporalResolveRS.get());
					context.SetPipelineState(m_pTemporalResolvePSO);

					context.SetRootCBV(0, GetViewUniforms(m_SceneData, m_pHDRRenderTarget.get()));

					context.BindResource(1, 0, m_pHDRRenderTarget->GetUAV());
					context.BindResource(2, 0, m_pVelocity->GetSRV());
					context.BindResource(2, 1, m_pPreviousColor->GetSRV());
					context.BindResource(2, 2, m_pTAASource->GetSRV());
					context.BindResource(2, 3, GetResolvedDepthStencil()->GetSRV());

					context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pHDRRenderTarget->GetWidth(), 8, m_pHDRRenderTarget->GetHeight(), 8));

					context.CopyTexture(m_pHDRRenderTarget.get(), m_pPreviousColor.get());
				});
		}
	}

	if (Tweakables::g_SDSM)
	{
		RGPassBuilder depthReduce = graph.AddPass("Depth Reduce");
		Data.DepthStencil = depthReduce.Write(Data.DepthStencil);
		depthReduce.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
				Texture* pSource = pDepthStencil;
				Texture* pTarget = m_ReductionTargets[0].get();

				context.InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pReduceDepthRS.get());
				context.SetPipelineState(pSource->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO : m_pPrepareReduceDepthPSO);

				context.SetRootCBV(0, GetViewUniforms(m_SceneData, pTarget));

				context.BindResource(1, 0, pTarget->GetUAV());
				context.BindResource(2, 0, pSource->GetSRV());

				context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());

				context.SetPipelineState(m_pReduceDepthPSO);
				for (size_t i = 1; i < m_ReductionTargets.size(); ++i)
				{
					pSource = pTarget;
					pTarget = m_ReductionTargets[i].get();

					context.InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.BindResource(1, 0, pTarget->GetUAV());
					context.BindResource(2, 0, pSource->GetSRV());

					context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());
				}

				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_COPY_SOURCE);
				context.FlushResourceBarriers();

				context.CopyTexture(pTarget, m_ReductionReadbackTargets[m_Frame % FRAME_COUNT].get(), CD3DX12_BOX(0, 1));
			});
	}

	{
		RG_GRAPH_SCOPE("Eye Adaptation", graph);
		Texture* pToneMapInput = m_pDownscaledColor.get();

		RGPassBuilder colorDownsample = graph.AddPass("Downsample Color");
		colorDownsample.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				context.SetPipelineState(m_pGenerateMipsPSO);
				context.SetComputeRootSignature(m_pGenerateMipsRS.get());

				struct DownscaleParameters
				{
					IntVector2 TargetDimensions;
					Vector2 TargetDimensionsInv;
				} Parameters{};
				Parameters.TargetDimensions.x = pToneMapInput->GetWidth();
				Parameters.TargetDimensions.y = pToneMapInput->GetHeight();
				Parameters.TargetDimensionsInv = Vector2(1.0f / pToneMapInput->GetWidth(), 1.0f / pToneMapInput->GetHeight());

				context.SetRootCBV(0, Parameters);
				context.BindResource(1, 0, pToneMapInput->GetUAV());
				context.BindResource(2, 0, m_pHDRRenderTarget->GetSRV());

				context.Dispatch(
					Math::DivideAndRoundUp(Parameters.TargetDimensions.x, 8),
					Math::DivideAndRoundUp(Parameters.TargetDimensions.y, 8)
				);
			});

		RGPassBuilder histogram = graph.AddPass("Luminance Histogram");
		histogram.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				context.ClearUavUInt(m_pLuminanceHistogram.get(), m_pLuminanceHistogram->GetUAV());

				context.SetPipelineState(m_pLuminanceHistogramPSO);
				context.SetComputeRootSignature(m_pLuminanceHistogramRS.get());

				struct HistogramParameters
				{
					uint32 Width;
					uint32 Height;
					float MinLogLuminance;
					float OneOverLogLuminanceRange;
				} Parameters;
				Parameters.Width = pToneMapInput->GetWidth();
				Parameters.Height = pToneMapInput->GetHeight();
				Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
				Parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get());

				context.SetRootCBV(0, Parameters);
				context.BindResource(1, 0, m_pLuminanceHistogram->GetUAV());
				context.BindResource(2, 0, pToneMapInput->GetSRV());

				context.Dispatch(
					Math::DivideAndRoundUp(pToneMapInput->GetWidth(), 16),
					Math::DivideAndRoundUp(pToneMapInput->GetHeight(), 16)
				);
			});

		RGPassBuilder avgLuminance = graph.AddPass("Average Luminance");
		avgLuminance.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pAverageLuminanceRS.get());
				context.SetPipelineState(m_pAverageLuminancePSO);

				struct Parameters
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

				context.SetRootCBV(0, parameters);
				context.BindResource(1, 0, m_pAverageLuminance->GetUAV());
				context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());

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
					Texture* pTarget = m_pBloomTexture.get();

					UnorderedAccessView** pTargetUAVs = m_pBloomUAVs.data();

					context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					context.SetComputeRootSignature(m_pBloomRS.get());
					context.SetPipelineState(m_pBloomSeparatePSO);

					struct Parameters
					{
						float Threshold;
						float BrightnessClamp;
					} parameters;

					parameters.Threshold = Tweakables::g_BloomThreshold;
					parameters.BrightnessClamp = Tweakables::g_BloomMaxBrightness;

					context.SetRootCBV(0, parameters);
					context.SetRootCBV(1, GetViewUniforms(m_SceneData));

					D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
						GetCurrentRenderTarget()->GetSRV()->GetDescriptor(),
						m_pAverageLuminance->GetSRV()->GetDescriptor(),
					};

					D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
						pTargetUAVs[0]->GetDescriptor()
					};

					context.BindResources(2, 0, uavs, ARRAYSIZE(uavs));
					context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

					context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
				});

			RGPassBuilder bloomMipChain = graph.AddPass("Bloom Mip Chain");
			bloomMipChain.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
				{
					Texture* pSource = m_pBloomTexture.get();
					Texture* pTarget = m_pBloomIntermediateTexture.get();

					UnorderedAccessView** pSourceUAVs = m_pBloomUAVs.data();
					UnorderedAccessView** pTargetUAVs = m_pBloomIntermediateUAVs.data();

					context.SetComputeRootSignature(m_pBloomRS.get());
					context.SetPipelineState(m_pBloomMipChainPSO);

					context.SetRootCBV(1, GetViewUniforms(m_SceneData));

					uint32 width = pTarget->GetWidth() / 2;
					uint32 height = pTarget->GetHeight() / 2;

					const uint32 numMips = pTarget->GetMipLevels();
					constexpr uint32 ThreadGroupSize = 128;

					for (uint32 i = 1; i < numMips; ++i)
					{
						struct Parameters
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

							context.SetRootCBV(0, parameters);
							context.BindResource(2, 0, pTargetUAVs[i]);
							context.BindResource(3, 0, pSource->GetSRV());

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
			struct Parameters
			{
				float WhitePoint;
				uint32 Tonemapper;
			} constBuffer;
			constBuffer.WhitePoint = Tweakables::g_WhitePoint.Get();
			constBuffer.Tonemapper = Tweakables::g_ToneMapper.Get();

			context.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pBloomTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			context.SetPipelineState(m_pToneMapPSO);
			context.SetComputeRootSignature(m_pToneMapRS.get());

			context.SetRootCBV(0, constBuffer);
			context.SetRootCBV(1, GetViewUniforms(m_SceneData, m_pTonemapTarget.get()));
			context.BindResource(2, 0, m_pTonemapTarget->GetUAV());
			context.BindResource(3, 0, m_pHDRRenderTarget->GetSRV());
			context.BindResource(3, 1, m_pAverageLuminance->GetSRV());
			context.BindResource(3, 2, Tweakables::g_Bloom.Get() ? m_pBloomTexture->GetSRV() : GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D)->GetSRV());

			context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pHDRRenderTarget->GetWidth(), 16, m_pHDRRenderTarget->GetHeight(), 16));
		});

	if (Tweakables::g_DrawHistogram.Get())
	{
		RGPassBuilder drawHistogram = graph.AddPass("Draw Histogram");
		drawHistogram.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pDebugHistogramTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pDrawHistogramPSO);
				context.SetComputeRootSignature(m_pDrawHistogramRS.get());

				struct AverageParameters
				{
					float MinLogLuminance;
					float InverseLogLuminanceRange;
					Vector2 InvTextureDimensions;
				} Parameters;

				Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
				Parameters.InverseLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get());
				Parameters.InvTextureDimensions.x = 1.0f / m_pDebugHistogramTexture->GetWidth();
				Parameters.InvTextureDimensions.y = 1.0f / m_pDebugHistogramTexture->GetHeight();

				context.SetRootCBV(0, Parameters);
				context.BindResource(1, 0, m_pDebugHistogramTexture->GetUAV());
				context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());
				context.BindResource(2, 1, m_pAverageLuminance->GetSRV());

				context.ClearUavUInt(m_pDebugHistogramTexture.get(), m_pDebugHistogramTexture->GetUAV());

				context.Dispatch(1, m_pLuminanceHistogram->GetNumElements());
			});
	}

	if (Tweakables::g_VisualizeLightDensity)
	{
		if (m_RenderPath == RenderPath::Clustered)
		{
			m_pClusteredForward->VisualizeLightDensity(graph, m_SceneData, m_pTonemapTarget.get(), GetResolvedDepthStencil());
		}
		else
		{
			m_pTiledForward->VisualizeLightDensity(graph, m_pDevice.get(), m_SceneData, m_pTonemapTarget.get(), GetResolvedDepthStencil());
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
	Profiler::Get()->Resolve(m_pSwapchain.get(), m_pDevice.get(), m_Frame);
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
	m_WindowWidth = width;
	m_WindowHeight = height;

	m_pDevice->IdleGPU();
	m_pSwapchain->OnResize(width, height);
}

void DemoApp::OnResizeViewport(int width, int height)
{
	E_LOG(Info, "Viewport resized: %dx%d", width, height);

	m_pDepthStencil = m_pDevice->CreateTexture(TextureDesc::CreateDepth(width, height, DXGI_FORMAT_D32_FLOAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)), "Depth Stencil");
	m_pResolvedDepthStencil = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Resolved Depth Stencil");

	if (m_SampleCount > 1)
	{
		m_pMultiSampleRenderTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Colors::Black)), "MSAA Target");
		m_pNormals = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Colors::Black)), "MSAA Normals");
	}

	m_pResolvedNormals = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, 1, ClearBinding(Colors::Black)), "Normals");
	m_pHDRRenderTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "HDR Target");
	m_pPreviousColor = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource), "Previous Color");
	m_pTonemapTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, m_pSwapchain->GetFormat(), TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "Tonemap Target");
	m_pDownscaledColor = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Downscaled HDR Target");
	m_pAmbientOcclusion = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "SSAO");
	m_pVelocity = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Velocity");
	m_pTAASource = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "TAA Target");

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
		std::unique_ptr<Texture> pTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(w, h, DXGI_FORMAT_R32G32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "SDSM Reduction Target");
		m_ReductionTargets.push_back(std::move(pTexture));
	}

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		std::unique_ptr<Buffer> pBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateTyped(1, DXGI_FORMAT_R32G32_FLOAT, BufferFlag::Readback), "SDSM Reduction Readback Target");
		pBuffer->Map();
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
		m_pBloomUAVs[i] = nullptr;
		m_pBloomTexture->CreateUAV(&m_pBloomUAVs[i], TextureUAVDesc((uint8)i));
		m_pBloomIntermediateUAVs[i] = nullptr;
		m_pBloomIntermediateTexture->CreateUAV(&m_pBloomIntermediateUAVs[i], TextureUAVDesc((uint8)i));
	}

	m_pCamera->SetViewport(FloatRect(0, 0, (float)width, (float)height));
}

void DemoApp::InitializePipelines()
{
	//Shadow mapping - Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader* pVertexShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
			Shader* pAlphaClipShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

			m_pShadowsRS = std::make_unique<RootSignature>(m_pDevice.get());
			m_pShadowsRS->FinalizeFromShader("Shadow Mapping (Opaque)", pVertexShader);

			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pShadowsRS->GetRootSignature());
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1);
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			psoDesc.SetDepthBias(-1, -5.0f, -4.0f);
			psoDesc.SetName("Shadow Mapping Opaque");
			m_pShadowsOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

			psoDesc.SetPixelShader(pAlphaClipShader);
			psoDesc.SetName("Shadow Mapping Alpha Mask");
			m_pShadowsAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
		}
	}

	//Depth prepass - Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader* pVertexShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		m_pDepthPrepassRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", pVertexShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(nullptr, 0, DXGI_FORMAT_D32_FLOAT, m_SampleCount);
		psoDesc.SetName("Depth Prepass Opaque");
		m_pDepthPrepassOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetName("Depth Prepass Alpha Mask");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDepthPrepassAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Luminance Historgram
	{
		Shader* pComputeShader = m_pDevice->GetShader("LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		m_pLuminanceHistogramRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Historgram", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Luminance Historgram");
		m_pLuminanceHistogramPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pLuminanceHistogram = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(sizeof(uint32) * 256), "Luminance Histogram");
		m_pAverageLuminance = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource), "Average Luminance");
		m_pDebugHistogramTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(m_pLuminanceHistogram->GetNumElements() * 4, m_pLuminanceHistogram->GetNumElements(), DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Debug Histogram");
	}

	//Debug Draw Histogram
	{
		Shader* pComputeShader = m_pDevice->GetShader("DrawLuminanceHistogram.hlsl", ShaderType::Compute, "DrawLuminanceHistogram");
		m_pDrawHistogramRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pDrawHistogramRS->FinalizeFromShader("Draw Luminance Historgram", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDrawHistogramRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Draw Luminance Historgram");
		m_pDrawHistogramPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Average Luminance
	{
		Shader* pComputeShader = m_pDevice->GetShader("AverageLuminance.hlsl", ShaderType::Compute, "CSMain");

		m_pAverageLuminanceRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Average Luminance");
		m_pAverageLuminancePSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Camera motion
	{
		Shader* pComputeShader = m_pDevice->GetShader("CameraMotionVectors.hlsl", ShaderType::Compute, "CSMain");

		m_pCameraMotionRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pCameraMotionRS->FinalizeFromShader("Camera Motion", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCameraMotionRS->GetRootSignature());
		psoDesc.SetName("Camera Motion");
		m_pCameraMotionPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Tonemapping
	{
		Shader* pComputeShader = m_pDevice->GetShader("Tonemapping.hlsl", ShaderType::Compute, "CSMain");

		m_pToneMapRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pToneMapRS->FinalizeFromShader("Tonemapping", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pToneMapRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Tone mapping Pipeline");
		m_pToneMapPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Depth resolve
	//Resolves a multisampled depth buffer to a normal depth buffer
	//Only required when the sample count > 1
	{
		Shader* pComputeShader = m_pDevice->GetShader("ResolveDepth.hlsl", ShaderType::Compute, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pResolveDepthRS->FinalizeFromShader("Depth Resolve", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		psoDesc.SetName("Resolve Depth Pipeline");
		m_pResolveDepthPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Depth reduce
	{
		Shader* pPrepareReduceShader = m_pDevice->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { });
		Shader* pPrepareReduceShaderMSAA = m_pDevice->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader* pReduceShader = m_pDevice->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "ReduceDepth", { });

		m_pReduceDepthRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pReduceDepthRS->FinalizeFromShader("Depth Reduce", pPrepareReduceShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pPrepareReduceShader);
		psoDesc.SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		psoDesc.SetName("Prepare Reduce Depth Pipeline");
		m_pPrepareReduceDepthPSO = m_pDevice->CreatePipeline(psoDesc);
		psoDesc.SetComputeShader(pPrepareReduceShaderMSAA);
		psoDesc.SetName("Prepare Reduce Depth Pipeline MSAA");
		m_pPrepareReduceDepthMsaaPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(pReduceShader);
		psoDesc.SetName("Reduce Depth Pipeline");
		m_pReduceDepthPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//TAA
	{
		Shader* pComputeShader = m_pDevice->GetShader("TemporalResolve.hlsl", ShaderType::Compute, "CSMain");
		m_pTemporalResolveRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pTemporalResolveRS->FinalizeFromShader("Temporal Resolve", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pTemporalResolveRS->GetRootSignature());
		psoDesc.SetName("Temporal Resolve");
		m_pTemporalResolvePSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Mip generation
	{
		Shader* pComputeShader = m_pDevice->GetShader("GenerateMips.hlsl", ShaderType::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		psoDesc.SetName("Generate Mips");
		m_pGenerateMipsPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Sky
	{
		Shader* pVertexShader = m_pDevice->GetShader("ProceduralSky.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = m_pDevice->GetShader("ProceduralSky.hlsl", ShaderType::Pixel, "PSMain");

		m_pSkyboxRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pSkyboxRS->AddConstantBufferView(100);
		m_pSkyboxRS->Finalize("Skybox");

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pSkyboxRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, m_SampleCount);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Bloom
	{
		m_pBloomRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pBloomRS->AddConstantBufferView(0);
		m_pBloomRS->AddConstantBufferView(100);
		m_pBloomRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
		m_pBloomRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
		m_pBloomRS->Finalize("Generate Mips");

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(m_pDevice->GetShader("Bloom.hlsl", ShaderType::Compute, "SeparateBloomCS"));
		psoDesc.SetRootSignature(m_pBloomRS->GetRootSignature());
		psoDesc.SetName("Separate Bloom");
		m_pBloomSeparatePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(m_pDevice->GetShader("Bloom.hlsl", ShaderType::Compute, "BloomMipChainCS"));
		psoDesc.SetName("Bloom Mips");
		m_pBloomMipChainPSO = m_pDevice->CreatePipeline(psoDesc);
	}
}

std::vector<std::unique_ptr<ShaderGraph::Expression>> nodes;
ShaderGraph::Expression* pTargetExpression;

template<typename T>
T* NewExpression()
{
	nodes.push_back(std::make_unique<T>());
	return (T*)nodes.back().get();
}

void DemoApp::UpdateImGui()
{
	ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

	using namespace ShaderGraph;

	ImNodesStyle& style = ImNodes::GetStyle();
	style.Flags = ImNodesStyleFlags_None;
	//style.NodeCornerRounding = 0;

	style.Colors[ImNodesCol_NodeBackground] = IM_COL32(50, 50, 50, 255);
	style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(65, 65, 65, 255);
	style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(65, 65, 65, 255);
	style.Colors[ImNodesCol_NodeOutline] = IM_COL32(20, 20, 20, 255);
	style.Colors[ImNodesCol_TitleBar] = IM_COL32(65, 65, 65, 255);
	style.Colors[ImNodesCol_TitleBarHovered] = IM_COL32(80, 80, 80, 255);
	style.Colors[ImNodesCol_TitleBarSelected] = IM_COL32(80, 80, 80, 255);
	style.Colors[ImNodesCol_Link] = IM_COL32(170, 175, 110, 200);
	style.Colors[ImNodesCol_LinkHovered] = IM_COL32(190, 195, 130, 255);
	style.Colors[ImNodesCol_LinkSelected] = IM_COL32(150, 155, 900, 255);
	style.Colors[ImNodesCol_Pin] = IM_COL32(53, 150, 250, 180);
	style.Colors[ImNodesCol_PinHovered] = IM_COL32(53, 150, 250, 255);

	style.Colors[ImNodesCol_BoxSelector] = IM_COL32(61, 133, 224, 30);
	style.Colors[ImNodesCol_BoxSelectorOutline] = IM_COL32(61, 133, 224, 150);

	style.Colors[ImNodesCol_GridBackground] = IM_COL32(15, 15, 15, 255);
	style.Colors[ImNodesCol_GridLine] = IM_COL32(200, 200, 200, 40);

	style.Colors[ImNodesCol_MiniMapBackground] = IM_COL32(25, 25, 25, 150);
	style.Colors[ImNodesCol_MiniMapBackgroundHovered] = IM_COL32(25, 25, 25, 200);
	style.Colors[ImNodesCol_MiniMapOutline] = IM_COL32(150, 150, 150, 100);
	style.Colors[ImNodesCol_MiniMapOutlineHovered] = IM_COL32(150, 150, 150, 200);
	style.Colors[ImNodesCol_MiniMapNodeBackground] = IM_COL32(200, 200, 200, 100);
	style.Colors[ImNodesCol_MiniMapNodeBackgroundHovered] = IM_COL32(200, 200, 200, 255);
	style.Colors[ImNodesCol_MiniMapNodeBackgroundSelected] =
		style.Colors[ImNodesCol_MiniMapNodeBackgroundHovered];
	style.Colors[ImNodesCol_MiniMapNodeOutline] = IM_COL32(200, 200, 200, 100);
	style.Colors[ImNodesCol_MiniMapLink] = style.Colors[ImNodesCol_Link];
	style.Colors[ImNodesCol_MiniMapLinkSelected] =
		style.Colors[ImNodesCol_LinkSelected];
	style.Colors[ImNodesCol_MiniMapCanvas] = IM_COL32(200, 200, 200, 25);
	style.Colors[ImNodesCol_MiniMapCanvasOutline] = IM_COL32(200, 200, 200, 200);

	static bool initOnce = false;
	static std::vector<std::pair<int, int>> links;
	if (!initOnce)
	{
		ShaderGraph::RegisterExpression<ConstantFloatExpression>("Constant Float");
		ShaderGraph::RegisterExpression<AddExpression>("Add");
		ShaderGraph::RegisterExpression<PowerExpression>("Power");
		ShaderGraph::RegisterExpression<TextureExpression>("Texture");
		ShaderGraph::RegisterExpression<Sample2DExpression>("Sample2D");
		ShaderGraph::RegisterExpression<SwizzleExpression>("Swizzle");
		ShaderGraph::RegisterExpression<VertexAttributeExpression>("Vertex Attribute");
		ShaderGraph::RegisterExpression<ViewUniformExpression>("View Uniform");

		ImNodes::LoadCurrentEditorStateFromIniFile("save_load.ini");
		static GraphTexture tex;
		tex.pName = "tFoo";

		VertexAttributeExpression* attributeExpression = NewExpression<VertexAttributeExpression>();
		attributeExpression->AddVertexAttribute("UV");

		TextureExpression* textureExpression = NewExpression<TextureExpression>();
		textureExpression->pTexture = &tex;

		Sample2DExpression* sampleExpression = NewExpression<Sample2DExpression>();
		sampleExpression->TextureInput.Connect(textureExpression);
		sampleExpression->UVInput.Connect(attributeExpression);

		ConstantFloatExpression* nodeB = NewExpression<ConstantFloatExpression>();
		nodeB->Value = 7;

		SwizzleExpression* swizzle = NewExpression<SwizzleExpression>();
		swizzle->Input.Connect(sampleExpression);
		swizzle->SetSwizzle("x");

		AddExpression* add = NewExpression<AddExpression>();
		add->InputA.Connect(swizzle);
		add->InputB.Connect(nodeB);

		PowerExpression* pow = NewExpression<PowerExpression>();
		pow->InputA.Connect(add);
		pow->InputB.Connect(swizzle);

		pTargetExpression = pow;

		initOnce = true;

		for (auto& node : nodes)
		{
			for (const ExpressionInput* input : node->GetInputs())
			{
				if (input->IsConnected())
				{
					links.emplace_back(input->pConnectedExpression->GetOutputs()[0].ID, input->ID);
				}
			}
		}
		
	}

	Compiler c;
	std::string msg;

	std::vector<Compiler::CompileError> errors;
	if (pTargetExpression->Compile(c, 0) == INVALID_INDEX)
	{
		errors = c.GetErrors();
		for (const Compiler::CompileError& error : errors)
		{
			msg += Sprintf("%s\n", error.Message.c_str());
		}
	}
	else
	{
		msg = c.GetSource();
	}

	auto GetNodeError = [&](Expression* pExpression) {
		for (const Compiler::CompileError& error : errors)
		{
			if (error.Expression.pExpression == pExpression)
			{
				return error.Message.c_str();
			}
		}
		return (const char*)nullptr;
	};

	ImGui::Begin("Compile Result");
	ImGui::InputTextMultiline("Output", &msg[0], msg.length());
	ImGui::End();

	ImGui::Begin("Node Editor");
	{
		ImNodes::BeginNodeEditor();

		const bool openPopup = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			ImNodes::IsEditorHovered() &&
			ImGui::IsMouseReleased(1);

		if (!ImGui::IsAnyItemHovered() && openPopup)
		{
			ImGui::OpenPopup("AddNode");
		}

		if (ImGui::BeginPopup("AddNode"))
		{
			const ImVec2 click_pos = ImGui::GetMousePosOnOpeningCurrentPopup();

			for (auto& factory : gFactories)
			{
				if (ImGui::MenuItem(factory.first))
				{
					nodes.push_back(std::unique_ptr<Expression>(factory.second.Callback()));
					ImNodes::SetNodeScreenSpacePos(nodes.back()->ID, click_pos);
				}
			}
			ImGui::EndPopup();
		}

		for (auto& node : nodes)
		{
			const char* pError = GetNodeError(node.get());
			if (pError)
				ImNodes::PushColorStyle(ImNodesCol_TitleBar, ImColor(150, 20, 20, 255));
			else if(node.get() == pTargetExpression)
				ImNodes::PushColorStyle(ImNodesCol_TitleBar, ImColor(112, 64, 35, 255));
			else
				ImNodes::PushColorStyle(ImNodesCol_TitleBar, ImColor(65, 65, 65, 255));

			node->Render();

			ImNodes::PopColorStyle();
		}

		for (int i = 0; i < links.size(); ++i)
		{
			const std::pair<int, int> p = links[i];
			ImNodes::Link(i, p.first, p.second);
		}


		ImNodes::MiniMap(0.2f);
		ImNodes::EndNodeEditor();

		int hovered;
		if (ImGui::IsKeyReleased('M') && ImNodes::IsNodeHovered(&hovered))
		{
			for (auto& node : nodes)
			{
				if (node->ID == hovered)
				{
					pTargetExpression = node.get();
				}
			}
		}

		auto findInput = [&](int id) -> ExpressionInput* {
			for (auto& node : nodes)
			{
				for (ExpressionInput* input : node->GetInputs())
				{
					if (input->ID == id)
						return input;
				}
			}
			return nullptr;
		};

		auto findOutput = [&](int id, Expression** pExpression, int* outputIndex) {
			for (auto& node : nodes)
			{
				const auto& outputs = node->GetOutputs();
				for (size_t i = 0; i < outputs.size(); ++i)
				{
					if (outputs[i].ID == id)
					{
						*pExpression = node.get();
						*outputIndex = (int)i;
						return;
					}
				}
			}
		};

		int start_attr, end_attr;
		if (ImNodes::IsLinkCreated(&start_attr, &end_attr))
		{
			links.push_back(std::make_pair(start_attr, end_attr));

			ExpressionInput* pInput = findInput(end_attr);

			int outputIndex = 0;
			Expression* pOutputExpression;
			findOutput(start_attr, &pOutputExpression, &outputIndex);

			pInput->Connect(pOutputExpression, outputIndex);
		}

		{
			const int num_selected = ImNodes::NumSelectedLinks();
			if (num_selected > 0 && ImGui::IsKeyReleased(VK_DELETE))
			{
				static std::vector<int> selected_links;
				selected_links.resize(static_cast<size_t>(num_selected));
				ImNodes::GetSelectedLinks(selected_links.data());
				for(size_t i = 0; i < selected_links.size(); ++i)
				{
					int edge_id = selected_links[i];
					ExpressionInput* pInput = findInput(links[edge_id].second);
					pInput->pConnectedExpression = nullptr;
					std::swap(links[edge_id], links[links.size() - 1 - i]);
				}
				links.resize(links.size() - num_selected);
			}
		}	

		ImNodes::SaveCurrentEditorStateToIniFile("save_load.ini");
	}
	ImGui::End();


	m_FrameTimes[m_Frame % m_FrameTimes.size()] = Time::DeltaTime();

	static ImGuiConsole console;
	static bool showProfiler = false;
	static bool showImguiDemo = false;

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
				ofn.lpstrFilter = "GLTF Files (*.gltf)\0*.gltf\0All Files (*.*)\0*.*\0";;
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

	ImGui::Begin("Viewport", 0, ImGuiWindowFlags_NoScrollbar);
	float widthDelta = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
	float heightDelta = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;
	uint32 width = (uint32)Math::Max(4.0f, widthDelta);
	uint32 height = (uint32)Math::Max(4.0f, heightDelta);

	if (width != m_pTonemapTarget->GetWidth() || height != m_pTonemapTarget->GetHeight())
	{
		OnResizeViewport(width, height);
	}
	ImGui::Image(m_pTonemapTarget.get(), ImVec2((float)width, (float)height));
	ImGui::End();

	if (Tweakables::g_VisualizeLightDensity)
	{
		//Render Color Legend
		ImGui::SetNextWindowSize(ImVec2(60, 255));
		ImGui::SetNextWindowPos(ImVec2((float)m_WindowWidth - 65, (float)m_WindowHeight - 280));
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

	console.Update(ImVec2(300, (float)m_WindowHeight), ImVec2((float)m_WindowWidth - 300 * 2, 250));

	if (showImguiDemo)
	{
		ImGui::ShowDemoWindow();
	}

	if (Tweakables::g_DrawHistogram)
	{
		ImGui::Begin("Luminance Histogram");
		ImVec2 cursor = ImGui::GetCursorPos();
		ImGui::ImageAutoSize(m_pDebugHistogramTexture.get(), ImVec2((float)m_pDebugHistogramTexture->GetWidth(), (float)m_pDebugHistogramTexture->GetHeight()));
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
					ImGui::Image(m_ShadowMaps[sunLight.ShadowIndex + i].get(), ImVec2(imageSize, imageSize));
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
			ImGui::Text("MS: %4.2f | FPS: %4.2f | %d x %d", Time::DeltaTime() * 1000.0f, 1.0f / Time::DeltaTime(), m_WindowWidth, m_WindowHeight);
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
			ImGui::Checkbox("Stabilize Cascades", &Tweakables::g_StabilizeCascades.Get());
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
		}

		if (ImGui::CollapsingHeader("Raytracing"))
		{
			if (m_pDevice->GetCapabilities().SupportsRaytracing())
			{
				ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO.Get());
				ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections.Get());
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

					std::unique_ptr<Buffer> pBLASScratch = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess), "BLAS Scratch Buffer");
					std::unique_ptr<Buffer> pBLAS = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::AccelerationStructure), "BLAS Buffer");

					D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
					asDesc.Inputs = prebuildInfo;
					asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
					asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
					asDesc.SourceAccelerationStructureData = 0;

					pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
					context.InsertUavBarrier(subMesh.pBLAS);

					subMesh.pBLAS = pBLAS.release();
					subMesh.pBLASScratch = pBLASScratch.release();
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
			m_pTLASScratch = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None), "TLAS Scratch");
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
		context.InsertUavBarrier(m_pTLAS.get());
	}
}

void DemoApp::LoadMesh(const std::string& filePath, CommandContext& context)
{
	std::unique_ptr<Mesh> pMesh = std::make_unique<Mesh>();
	pMesh->Load(filePath.c_str(), m_pDevice.get(), &context, 1.0f);
	m_Meshes.push_back(std::move(pMesh));
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
		for (const SubMesh& subMesh : pMesh->GetMeshes())
		{
			ShaderInterop::MeshData mesh;
			mesh.BufferIndex = pMesh->GetData()->GetSRVIndex();
			mesh.IndexByteSize = subMesh.IndicesLocation.Stride();
			mesh.IndicesOffset = (uint32)subMesh.IndicesLocation.OffsetFromStart;
			mesh.PositionsOffset = (uint32)subMesh.PositionStreamLocation.OffsetFromStart;
			mesh.NormalsOffset = (uint32)subMesh.NormalStreamLocation.OffsetFromStart;
			mesh.UVsOffset = (uint32)subMesh.UVStreamLocation.OffsetFromStart;
			mesh.MeshletOffset = subMesh.MeshletsLocation;
			mesh.MeshletVertexOffset = subMesh.MeshletVerticesLocation;
			mesh.MeshletTriangleOffset = subMesh.MeshletTrianglesLocation;
			mesh.MeshletBoundsOffset = subMesh.MeshletBoundsLocation;
			mesh.MeshletCount = subMesh.NumMeshlets;
			meshes.push_back(mesh);
		}

		for (const SubMeshInstance& node : pMesh->GetMeshInstances())
		{
			const SubMesh& parentMesh = pMesh->GetMesh(node.MeshIndex);
			const Material& meshMaterial = pMesh->GetMaterial(parentMesh.MaterialId);
			ShaderInterop::MeshInstance meshInstance;
			meshInstance.Mesh = node.MeshIndex;
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

		ImGui::Begin("Materials");
		static int selectedMaterial = 0;
		ImGui::ListBox("Materials", &selectedMaterial, [](void* pData, int index, const char** pOutText)
			{
				Material* pMat = (Material*)pData;
				*pOutText = pMat[index].Name.c_str();
				return true;
			}, pMesh->GetMaterials().data(), (int)pMesh->GetMaterials().size());
		if (selectedMaterial >= 0 && selectedMaterial < pMesh->GetMaterials().size())
		{
			Material& material = pMesh->GetMaterials()[selectedMaterial];
			ImGui::ColorEdit3("Base Color", &material.BaseColorFactor.x);
			ImGui::ColorEdit3("Emissive", &material.EmissiveFactor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			ImGui::SliderFloat("Metalness", &material.MetalnessFactor, 0, 1);
			ImGui::SliderFloat("Roughness", &material.RoughnessFactor, 0, 1);
			if (ImGui::Button("Base Color Texture"))
			{
				material.pDiffuseTexture = GraphicsCommon::GetDefaultTexture(DefaultTexture::ColorNoise256);
			}
		}
		ImGui::End();

		for (Material& material : pMesh->GetMaterials())
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

	sceneBatches.swap(m_SceneData.Batches);

	if (!m_pMeshBuffer || meshes.size() > m_pMeshBuffer->GetNumElements())
	{
		m_pMeshBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)meshes.size()), sizeof(ShaderInterop::MeshData), BufferFlag::ShaderResource), "Meshes");
	}
	context.InitializeBuffer(m_pMeshBuffer.get(), meshes.data(), meshes.size() * sizeof(ShaderInterop::MeshData));

	if (!m_pMeshInstanceBuffer || meshInstances.size() > m_pMeshInstanceBuffer->GetNumElements())
	{
		m_pMeshInstanceBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)meshInstances.size()), sizeof(ShaderInterop::MeshInstance), BufferFlag::ShaderResource), "Meshes");
	}
	context.InitializeBuffer(m_pMeshInstanceBuffer.get(), meshInstances.data(), meshInstances.size() * sizeof(ShaderInterop::MeshInstance));

	if (!m_pMaterialBuffer || materials.size() > m_pMaterialBuffer->GetNumElements())
	{
		m_pMaterialBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)materials.size()), sizeof(ShaderInterop::MaterialData), BufferFlag::ShaderResource), "Materials");
	}
	context.InitializeBuffer(m_pMaterialBuffer.get(), materials.data(), materials.size() * sizeof(ShaderInterop::MaterialData));

	if (!m_pTransformsBuffer || transforms.size() > m_pTransformsBuffer->GetNumElements())
	{
		m_pTransformsBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)transforms.size()), sizeof(Matrix), BufferFlag::ShaderResource), "Transforms");
	}
	context.InitializeBuffer(m_pTransformsBuffer.get(), transforms.data(), transforms.size() * sizeof(Matrix));

	std::vector<ShaderInterop::Light> lightData;
	Utils::Transform(m_Lights, lightData, [](const Light& light) { return light.GetData(); });

	if (!m_pLightBuffer || lightData.size() > m_pLightBuffer->GetNumElements())
	{
		m_pLightBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1, (int)lightData.size()), sizeof(ShaderInterop::Light), BufferFlag::ShaderResource), "Lights");
	}
	context.InitializeBuffer(m_pLightBuffer.get(), lightData.data(), lightData.size() * sizeof(ShaderInterop::Light));

	UpdateTLAS(context);

	m_SceneData.pLightBuffer = m_pLightBuffer.get();
	m_SceneData.pMaterialBuffer = m_pMaterialBuffer.get();
	m_SceneData.pMeshBuffer = m_pMeshBuffer.get();
	m_SceneData.pTransformsBuffer = m_pTransformsBuffer.get();
	m_SceneData.pMeshInstanceBuffer = m_pMeshInstanceBuffer.get();
	m_SceneData.pSceneTLAS = m_pTLAS.get();
}
