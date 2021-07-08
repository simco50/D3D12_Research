#include "stdafx.h"
#include "DemoApp.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Scene/Camera.h"
#include "ImGuizmo/ImGuizmo.h"
#include "Graphics/DebugRenderer.h"
#include "Core/Input.h"
#include "Graphics/Core/Texture.h"
#include "Core/TaskQueue.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Profiler.h"
#include "Graphics/Techniques/GpuParticles.h"
#include "Graphics/Techniques/RTAO.h"
#include "Graphics/Techniques/TiledForward.h"
#include "Graphics/Techniques/ClusteredForward.h"
#include "Graphics/Techniques/RTReflections.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/Mesh.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Techniques/SSAO.h"
#include "Core/CommandLine.h"
#include "Core/Paths.h"
#include "Content/Image.h"
#include "Core/ConsoleVariables.h"

static const int32 FRAME_COUNT = 3;
static const DXGI_FORMAT SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;

void DrawScene(CommandContext& context, const SceneData& scene, Batch::Blending blendModes)
{
	DrawScene(context, scene, scene.VisibilityMask, blendModes);
}

void DrawScene(CommandContext& context, const SceneData& scene, const VisibilityMask& visibility, Batch::Blending blendModes)
{
	std::vector<const Batch*> meshes;
	for (const Batch& b : scene.Batches)
	{
		if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.Index))
		{
			meshes.push_back(&b);
		}
	}

	auto CompareSort = [&scene, blendModes](const Batch* a, const Batch* b)
	{
		float aDist = Vector3::DistanceSquared(a->pMesh->Bounds.Center, scene.pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b->pMesh->Bounds.Center, scene.pCamera->GetPosition());
		return EnumHasAnyFlags(blendModes, Batch::Blending::AlphaBlend) ? bDist < aDist : aDist < bDist;
	};
	std::sort(meshes.begin(), meshes.end(), CompareSort);

	struct PerObjectData
	{
		uint32 Mesh;
		uint32 Material;
	} ObjectData;

	for (const Batch* b : meshes)
	{
		ObjectData.Material = b->Material;
		ObjectData.Mesh = b->Index;
		context.SetGraphicsRootConstants(0, ObjectData);
		context.SetIndexBuffer(b->pMesh->IndicesLocation);
		context.DrawIndexed(b->pMesh->IndicesLocation.Elements, 0, 0);
	}
}

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

	// Misc Lighting
	ConsoleVariable g_RaytracedAO("r.Raytracing.AO", false);
	ConsoleVariable g_VisualizeLights("vis.Lights", false);
	ConsoleVariable g_VisualizeLightDensity("vis.LightDensity", false);
	ConsoleVariable g_RenderObjectBounds("r.vis.ObjectBounds", false);

	ConsoleVariable g_RaytracedReflections("r.Raytracing.Reflections", true);
	ConsoleVariable g_TLASBoundsThreshold("r.Raytracing.TLASBoundsThreshold", 5.0f * Math::DegreesToRadians);
	ConsoleVariable g_SsrSamples("r.SSRSamples", 8);

	// Misc
	bool g_DumpRenderGraph = false;
	DelegateConsoleCommand<> gDumpRenderGraph("DumpRenderGraph", []() { g_DumpRenderGraph = true; });
	bool g_Screenshot = false;
	DelegateConsoleCommand<> gScreenshot("Screenshot", []() { g_Screenshot = true; });
	bool g_EnableUI = true;

	// Lighting
	float g_SunInclination = 0.579f;
	float g_SunOrientation = -3.055f;
	float g_SunTemperature = 5900.0f;
	float g_SunIntensity = 3.0f;
}

DemoApp::DemoApp(WindowHandle window, const IntVector2& windowRect, int sampleCount /*= 1*/)
	: m_SampleCount(sampleCount)
{
	// #todo fixup MSAA :(
	checkf(sampleCount == 1, "I broke MSAA! TODO");

	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetPosition(Vector3(-30, 35, 48));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(3 * Math::PIDIV4, Math::PIDIV4 * 0.3f, 0));
	m_pCamera->SetNearPlane(300.0f);
	m_pCamera->SetFarPlane(1.0f);

	E_LOG(Info, "Graphics::InitD3D()");

	GraphicsInstanceFlags instanceFlags = GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("d3ddebug") ? GraphicsInstanceFlags::DebugDevice : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("dred") ? GraphicsInstanceFlags::DRED : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("gpuvalidation") ? GraphicsInstanceFlags::GpuValidation : GraphicsInstanceFlags::None;
	instanceFlags |= CommandLine::GetBool("pix") ? GraphicsInstanceFlags::Pix : GraphicsInstanceFlags::None;
	std::unique_ptr<GraphicsInstance> instance = GraphicsInstance::CreateInstance(instanceFlags);

	ComPtr<IDXGIAdapter4> pAdapter = instance->EnumerateAdapter(CommandLine::GetBool("warp"));
	m_pDevice = instance->CreateDevice(pAdapter.Get());
	m_pSwapchain = instance->CreateSwapchain(m_pDevice.get(), window, SWAPCHAIN_FORMAT, windowRect.x, windowRect.y, FRAME_COUNT, true);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(m_pDevice.get());

	m_pClusteredForward = std::make_unique<ClusteredForward>(m_pDevice.get());
	m_pTiledForward = std::make_unique<TiledForward>(m_pDevice.get());
	m_pRTReflections = std::make_unique<RTReflections>(m_pDevice.get());
	m_pRTAO = std::make_unique<RTAO>(m_pDevice.get());
	m_pSSAO = std::make_unique<SSAO>(m_pDevice.get());
	m_pParticles = std::make_unique<GpuParticles>(m_pDevice.get());

	Profiler::Get()->Initialize(m_pDevice.get(), FRAME_COUNT);
	DebugRenderer::Get()->Initialize(m_pDevice.get());

	m_SceneData.GlobalSRVHeapHandle = m_pDevice->GetViewHeapHandle();

	OnResize(windowRect.x, windowRect.y);

	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	InitializePipelines();
	InitializeAssets(*pContext);
	SetupScene(*pContext);
	UpdateTLAS(*pContext);
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

void DemoApp::InitializeAssets(CommandContext& context)
{
	auto RegisterDefaultTexture = [this, &context](DefaultTexture type, const char* pName, const TextureDesc& desc, uint32* pData)
	{
		m_DefaultTextures[(int)type] = std::make_unique<Texture>(m_pDevice.get(), pName);
		m_DefaultTextures[(int)type]->Create(&context, desc, pData);
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

	m_DefaultTextures[(int)DefaultTexture::ColorNoise256] = std::make_unique<Texture>(m_pDevice.get(), "Color Noise 256px");
	m_DefaultTextures[(int)DefaultTexture::ColorNoise256]->Create(&context, "Resources/Textures/Noise.png", false);
	m_DefaultTextures[(int)DefaultTexture::BlueNoise512] = std::make_unique<Texture>(m_pDevice.get(), "Blue Noise 512px");
	m_DefaultTextures[(int)DefaultTexture::BlueNoise512]->Create(&context, "Resources/Textures/BlueNoise.dds", false);
}

void DemoApp::SetupScene(CommandContext& context)
{
	m_pLightCookie = std::make_unique<Texture>(m_pDevice.get(), "Light Cookie");
	m_pLightCookie->Create(&context, "Resources/Textures/LightProjector.png", false);

	{
		std::unique_ptr<Mesh> pMesh = std::make_unique<Mesh>();
		pMesh->Load("Resources/Sponza/Sponza.gltf", m_pDevice.get(), &context, 10.0f);
		m_Meshes.push_back(std::move(pMesh));
	}

	std::vector<ShaderInterop::MaterialData> materials;
	std::vector<ShaderInterop::MeshData> meshes;

	for (const auto& pMesh : m_Meshes)
	{
		for (const SubMeshInstance& node : pMesh->GetMeshInstances())
		{
			const SubMesh& subMesh = pMesh->GetMesh(node.MeshIndex);
			const Material& material = pMesh->GetMaterial(subMesh.MaterialId);
			Batch batch;
			batch.Index = (int)m_SceneData.Batches.size();
			batch.LocalBounds = subMesh.Bounds;
			batch.pMesh = &subMesh;
			batch.BlendMode = material.IsTransparent ? Batch::Blending::AlphaMask : Batch::Blending::Opaque;
			batch.WorldMatrix = node.Transform;
			batch.Material = (uint32)materials.size() + subMesh.MaterialId;
			m_SceneData.Batches.push_back(batch);

			ShaderInterop::MeshData mesh;
			mesh.IndexBuffer = m_pDevice->RegisterBindlessResource(subMesh.pIndexSRV);
			mesh.VertexBuffer = m_pDevice->RegisterBindlessResource(subMesh.pVertexSRV);
			mesh.Material = (uint32)materials.size() + subMesh.MaterialId;
			mesh.World = node.Transform;
			meshes.push_back(mesh);
		}
		for (const Material& material : pMesh->GetMaterials())
		{
			ShaderInterop::MaterialData materialData;
			materialData.Diffuse = m_pDevice->RegisterBindlessResource(material.pDiffuseTexture);
			materialData.Normal = m_pDevice->RegisterBindlessResource(material.pNormalTexture);
			materialData.RoughnessMetalness = m_pDevice->RegisterBindlessResource(material.pRoughnessMetalnessTexture);
			materialData.Emissive = m_pDevice->RegisterBindlessResource(material.pEmissiveTexture);
			materialData.BaseColorFactor = material.BaseColorFactor;
			materialData.MetalnessFactor = material.MetalnessFactor;
			materialData.RoughnessFactor = material.RoughnessFactor;
			materialData.EmissiveFactor = material.EmissiveFactor;
			materialData.AlphaCutoff = material.AlphaCutoff;
			materials.push_back(materialData);
		}
	}

	m_pMeshBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured((int)meshes.size(), sizeof(ShaderInterop::MeshData), BufferFlag::ShaderResource), "Meshes");
	m_pMeshBuffer->SetData(&context, meshes.data(), meshes.size() * sizeof(ShaderInterop::MeshData));

	m_pMaterialBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured((int)materials.size(), sizeof(ShaderInterop::MaterialData), BufferFlag::ShaderResource), "Materials");
	m_pMaterialBuffer->SetData(&context, materials.data(), materials.size() * sizeof(ShaderInterop::MaterialData));

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
		Light spotLight = Light::Spot(Vector3(-5, 16, 16), 800, Vector3(0, 1, 0), 90, 70, 1000, Color(1.0f, 0.7f, 0.3f, 1.0f));
		spotLight.CastShadows = true;
		spotLight.LightTexture = m_pDevice->RegisterBindlessResource(m_pLightCookie.get(), GetDefaultTexture(DefaultTexture::White2D));
		spotLight.VolumetricLighting = true;
		m_Lights.push_back(spotLight);
	}
	m_pLightBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateStructured((int)m_Lights.size(), sizeof(Light), BufferFlag::ShaderResource), "Lights");
}

void DemoApp::Update()
{
	PROFILE_BEGIN("Update");
	m_pImGuiRenderer->NewFrame(m_WindowWidth, m_WindowHeight);

	UpdateImGui();

	PROFILE_BEGIN("Update Game State");
	m_pDevice->GetShaderManager()->ConditionallyReloadShaders();

	for (Batch& b : m_SceneData.Batches)
	{
		b.LocalBounds.Transform(b.Bounds, b.WorldMatrix);
		b.Radius = Vector3(b.Bounds.Extents).Length();
	}

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

#if 0
	Vector3 pos = m_pCamera->GetPosition();
	pos.x = 48;
	pos.y = sin(5 * Time::TotalTime()) * 4 + 84;
	pos.z = -2.6f;
	m_pCamera->SetPosition(pos);
#endif
	m_pCamera->Update();

	if (Input::Instance().IsKeyPressed('U'))
	{
		Tweakables::g_EnableUI = !Tweakables::g_EnableUI;
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

	ShaderInterop::ShadowData shadowData;
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
			cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
		}

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
						corner = Vector3::Transform(corner, m_pCamera->GetProjectionInverse());
						corner = Vector3::Transform(corner, m_pCamera->GetViewInverse());
					}

					//Adjust frustum corners based on cascade splits
					for (int j = 0; j < 4; ++j)
					{
						Vector3 cornerRay = frustumCorners[j + 4] - frustumCorners[j];
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

					Matrix shadowView = Math::CreateLookToMatrix(center + light.Direction * -400, light.Direction, Vector3::Up);

					Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z + 400, 0);
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

					float* values = &shadowData.CascadeDepths.x;
					values[shadowIndex] = currentCascadeSplit * (farPlane - nearPlane) + nearPlane;
					shadowData.LightViewProjections[shadowIndex++] = lightViewProjection;
				}
			}
			else if (light.Type == LightType::Spot)
			{
				Matrix projection = Math::CreatePerspectiveMatrix(light.UmbraAngleDegrees * Math::DegreesToRadians, 1.0f, light.Range, 1.0f);
				shadowData.LightViewProjections[shadowIndex++] = Math::CreateLookToMatrix(light.Position, light.Direction, Vector3::Up) * projection;
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
				m_pDevice->RegisterBindlessResource(pShadowMap.get(), nullptr);
			}
		}


		for (Light& light : m_Lights)
		{
			if (light.ShadowIndex >= 0)
			{
				light.ShadowMapSize = m_ShadowMaps[light.ShadowIndex]->GetWidth();
			}
		}
		shadowData.ShadowMapOffset = m_pDevice->RegisterBindlessResource(m_ShadowMaps[0].get());
	}

	{
		PROFILE_SCOPE("Frustum Culling");
		BoundingFrustum frustum = m_pCamera->GetFrustum();
		for (const Batch& b : m_SceneData.Batches)
		{
			m_SceneData.VisibilityMask.AssignBit(b.Index, frustum.Contains(b.Bounds));
		}
	}

	m_SceneData.pDepthBuffer = GetDepthStencil();
	m_SceneData.pResolvedDepth = GetResolvedDepthStencil();
	m_SceneData.pRenderTarget = GetCurrentRenderTarget();
	m_SceneData.pLightBuffer = m_pLightBuffer.get();
	m_SceneData.pMaterialBuffer = m_pMaterialBuffer.get();
	m_SceneData.pMeshBuffer = m_pMeshBuffer.get();
	m_SceneData.pCamera = m_pCamera.get();
	m_SceneData.pShadowData = &shadowData;
	m_SceneData.pAO = m_pAmbientOcclusion.get();
	m_SceneData.FrameIndex = m_Frame;
	m_SceneData.pPreviousColor = m_pPreviousColor.get();
	m_SceneData.SceneTLAS = m_pDevice->RegisterBindlessResource(m_pTLAS->GetSRV());
	m_SceneData.pNormals = m_pNormals.get();
	m_SceneData.pResolvedNormals = m_pResolvedNormals.get();
	m_SceneData.pResolvedTarget = Tweakables::g_TAA.Get() ? m_pTAASource.get() : m_pHDRRenderTarget.get();

	PROFILE_END();

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////

	if (m_CapturePix)
	{
		D3D::BeginPixCapture();
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

	uint64 nextFenceValue = 0;

	if (Tweakables::g_Screenshot && m_ScreenshotDelay < 0)
	{
		RGPassBuilder screenshot = graph.AddPass("Take Screenshot");
		screenshot.Bind([=](CommandContext& renderContext, const RGPassResources& /*resources*/)
			{
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
				D3D12_RESOURCE_DESC resourceDesc = m_pTonemapTarget->GetResource()->GetDesc();
				m_pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
				m_pScreenshotBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
				m_pScreenshotBuffer->Map();
				renderContext.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				renderContext.InsertResourceBarrier(m_pScreenshotBuffer.get(), D3D12_RESOURCE_STATE_COPY_DEST);
				renderContext.CopyTexture(m_pTonemapTarget.get(), m_pScreenshotBuffer.get(), CD3DX12_BOX(0, 0, m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight()));
				m_ScreenshotRowPitch = textureFootprint.Footprint.RowPitch;
			});
		m_ScreenshotDelay = 4;
		Tweakables::g_Screenshot = false;
	}

	if (m_pScreenshotBuffer)
	{
		if (m_ScreenshotDelay == 0)
		{
			TaskContext taskContext;
			TaskQueue::Execute([&](uint32) {
				char* pData = (char*)m_pScreenshotBuffer->GetMappedData();
				Image img;
				img.SetSize(m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight(), 4);
				uint32 imageRowPitch = m_pTonemapTarget->GetWidth() * 4;
				uint32 targetOffset = 0;
				for (int i = 0; i < m_pTonemapTarget->GetHeight(); ++i)
				{
					img.SetData((uint32*)pData, targetOffset, imageRowPitch);
					pData += m_ScreenshotRowPitch;
					targetOffset += imageRowPitch;
				}

				SYSTEMTIME time;
				GetSystemTime(&time);
				Paths::CreateDirectoryTree(Paths::ScreenshotDir());
				char filePath[128];
				FormatString(filePath, ARRAYSIZE(filePath), "%sScreenshot_%d_%02d_%02d__%02d_%02d_%02d.jpg",
					Paths::ScreenshotDir().c_str(),
					time.wYear, time.wMonth, time.wDay,
					time.wHour, time.wMinute, time.wSecond);
				img.Save(filePath);
				m_pScreenshotBuffer.reset();
				}, taskContext);
			m_ScreenshotDelay = -1;
		}
		else
		{
			m_ScreenshotDelay--;
		}
	}

	RGPassBuilder updateTLAS = graph.AddPass("Update TLAS");
	updateTLAS.Bind([=](CommandContext& renderContext, const RGPassResources& /*resources*/)
		{
			UpdateTLAS(renderContext);
		}
	);


	RGPassBuilder setupLights = graph.AddPass("Setup Lights");
	Data.DepthStencil = setupLights.Write(Data.DepthStencil);
	setupLights.Bind([=](CommandContext& renderContext, const RGPassResources& /*resources*/)
		{
			DynamicAllocation allocation = renderContext.AllocateTransientMemory(m_Lights.size() * sizeof(ShaderInterop::Light));
			ShaderInterop::Light* pTarget = (ShaderInterop::Light*)allocation.pMappedMemory;
			for (const Light& light : m_Lights)
			{
				*pTarget = light.GetData();
				++pTarget;
			}
			renderContext.InsertResourceBarrier(m_pLightBuffer.get(), D3D12_RESOURCE_STATE_COPY_DEST);
			renderContext.FlushResourceBarriers();
			renderContext.CopyBuffer(allocation.pBackingResource, m_pLightBuffer.get(), (uint32)m_pLightBuffer->GetSize(), (uint32)allocation.Offset, 0);
		});

	//DEPTH PREPASS
	// - Depth only pass that renders the entire scene
	// - Optimization that prevents wasteful lighting calculations during the base pass
	// - Required for light culling
	RGPassBuilder prepass = graph.AddPass("Depth Prepass");
	Data.DepthStencil = prepass.Write(Data.DepthStencil);
	prepass.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
		{
			Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			RenderPassInfo info = RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store);

			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

			const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				m_SceneData.pMaterialBuffer->GetSRV()->GetDescriptor(),
				m_SceneData.pMeshBuffer->GetSRV()->GetDescriptor(),
			};

			renderContext.BindResources(2, 0, srvs, ARRAYSIZE(srvs));
			renderContext.BindResourceTable(3, m_SceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

			struct ViewData
			{
				Matrix ViewProjection;
			} viewData;
			viewData.ViewProjection = m_pCamera->GetViewProjection();
			renderContext.SetGraphicsDynamicConstantBufferView(1, viewData);

			{
				GPU_PROFILE_SCOPE("Opaque", &renderContext);
				renderContext.SetPipelineState(m_pDepthPrepassOpaquePSO);
				DrawScene(renderContext, m_SceneData, Batch::Blending::Opaque);
			}
			{
				GPU_PROFILE_SCOPE("Masked", &renderContext);
				renderContext.SetPipelineState(m_pDepthPrepassAlphaMaskPSO);
				DrawScene(renderContext, m_SceneData, Batch::Blending::AlphaMask);
			}

			renderContext.EndRenderPass();
		});

	//[WITH MSAA] DEPTH RESOLVE
	// - If MSAA is enabled, run a compute shader to resolve the depth buffer
	if (m_SampleCount > 1)
	{
		RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
		Data.DepthStencil = depthResolve.Read(Data.DepthStencil);
		Data.DepthStencilResolved = depthResolve.Write(Data.DepthStencilResolved);
		depthResolve.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
			{
				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencil), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencilResolved), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetComputeRootSignature(m_pResolveDepthRS.get());
				renderContext.SetPipelineState(m_pResolveDepthPSO);

				renderContext.BindResource(0, 0, resources.GetTexture(Data.DepthStencilResolved)->GetUAV());
				renderContext.BindResource(1, 0, resources.GetTexture(Data.DepthStencil)->GetSRV());

				int dispatchGroupsX = Math::DivideAndRoundUp(m_WindowWidth, 16);
				int dispatchGroupsY = Math::DivideAndRoundUp(m_WindowHeight, 16);
				renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);

				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencilResolved), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencil), D3D12_RESOURCE_STATE_DEPTH_READ);
				renderContext.FlushResourceBarriers();
			});
	}
	else
	{
		RGPassBuilder depthResolve = graph.AddPass("Depth Resolve");
		depthResolve.Bind([=](CommandContext& renderContext, const RGPassResources& /*resources*/)
			{
				renderContext.CopyTexture(GetDepthStencil(), GetResolvedDepthStencil());
			});
	}

	// Camera velocity
	if (Tweakables::g_TAA.Get())
	{
		RGPassBuilder cameraMotion = graph.AddPass("Camera Motion");
		cameraMotion.Bind([=](CommandContext& renderContext, const RGPassResources& /*resources*/)
			{
				renderContext.InsertResourceBarrier(GetResolvedDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetComputeRootSignature(m_pCameraMotionRS.get());
				renderContext.SetPipelineState(m_pCameraMotionPSO);

				struct Parameters
				{
					Matrix ReprojectionMatrix;
					Vector2 InvScreenDimensions;
				} parameters;

				Matrix preMult = Matrix(
					Vector4(2.0f, 0.0f, 0.0f, 0.0f),
					Vector4(0.0f, -2.0f, 0.0f, 0.0f),
					Vector4(0.0f, 0.0f, 1.0f, 0.0f),
					Vector4(-1.0f, 1.0f, 0.0f, 1.0f)
				);

				Matrix postMult = Matrix(
					Vector4(1.0f / 2.0f, 0.0f, 0.0f, 0.0f),
					Vector4(0.0f, -1.0f / 2.0f, 0.0f, 0.0f),
					Vector4(0.0f, 0.0f, 1.0f, 0.0f),
					Vector4(1.0f / 2.0f, 1.0f / 2.0f, 0.0f, 1.0f));

				parameters.ReprojectionMatrix = preMult * m_pCamera->GetViewProjection().Invert() * m_pCamera->GetPreviousViewProjection() * postMult;
				parameters.InvScreenDimensions = Vector2(1.0f / m_WindowWidth, 1.0f / m_WindowHeight);

				renderContext.SetComputeDynamicConstantBufferView(0, parameters);

				renderContext.BindResource(1, 0, m_pVelocity->GetUAV());
				renderContext.BindResource(2, 0, GetResolvedDepthStencil()->GetSRV());

				int dispatchGroupsX = Math::DivideAndRoundUp(m_WindowWidth, 8);
				int dispatchGroupsY = Math::DivideAndRoundUp(m_WindowHeight, 8);
				renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);
			});
	}

	m_pParticles->Simulate(graph, GetResolvedDepthStencil(), *m_pCamera);

	if (Tweakables::g_RaytracedAO)
	{
		m_pRTAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolvedDepthStencil(), m_SceneData, *m_pCamera);
	}
	else
	{
		m_pSSAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolvedDepthStencil(), *m_pCamera);
	}

	//SHADOW MAPPING
	// - Renders the scene depth onto a separate depth buffer from the light's view
	if (shadowIndex > 0)
	{
		if (Tweakables::g_SDSM)
		{
			RGPassBuilder depthReduce = graph.AddPass("Depth Reduce");
			Data.DepthStencil = depthReduce.Write(Data.DepthStencil);
			depthReduce.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
				{
					Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(m_ReductionTargets[0].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					renderContext.SetComputeRootSignature(m_pReduceDepthRS.get());
					renderContext.SetPipelineState(pDepthStencil->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO : m_pPrepareReduceDepthPSO);

					struct ShaderParameters
					{
						float Near;
						float Far;
					} parameters;
					parameters.Near = m_pCamera->GetNear();
					parameters.Far = m_pCamera->GetFar();

					renderContext.SetComputeDynamicConstantBufferView(0, parameters);
					renderContext.BindResource(1, 0, m_ReductionTargets[0]->GetUAV());
					renderContext.BindResource(2, 0, pDepthStencil->GetSRV());

					renderContext.Dispatch(m_ReductionTargets[0]->GetWidth(), m_ReductionTargets[0]->GetHeight());

					renderContext.SetPipelineState(m_pReduceDepthPSO);
					for (size_t i = 1; i < m_ReductionTargets.size(); ++i)
					{
						renderContext.InsertResourceBarrier(m_ReductionTargets[i - 1].get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						renderContext.InsertResourceBarrier(m_ReductionTargets[i].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

						renderContext.BindResource(1, 0, m_ReductionTargets[i]->GetUAV());
						renderContext.BindResource(2, 0, m_ReductionTargets[i - 1]->GetSRV());

						renderContext.Dispatch(m_ReductionTargets[i]->GetWidth(), m_ReductionTargets[i]->GetHeight());
					}

					renderContext.InsertResourceBarrier(m_ReductionTargets.back().get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
					renderContext.FlushResourceBarriers();

					renderContext.CopyTexture(m_ReductionTargets.back().get(), m_ReductionReadbackTargets[m_Frame % FRAME_COUNT].get(), CD3DX12_BOX(0, 1));
				});
		}

		RGPassBuilder shadows = graph.AddPass("Shadow Mapping");
		shadows.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
			{
				for (auto& pShadowmap : m_ShadowMaps)
				{
					context.InsertResourceBarrier(pShadowmap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
				}

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pShadowsRS.get());

				struct ViewData
				{
					Matrix ViewProjection;
				} viewData;

				for (int i = 0; i < shadowIndex; ++i)
				{
					GPU_PROFILE_SCOPE("Light View", &context);
					Texture* pShadowmap = m_ShadowMaps[i].get();
					context.BeginRenderPass(RenderPassInfo(pShadowmap, RenderPassAccess::Clear_Store));

					viewData.ViewProjection = shadowData.LightViewProjections[i];
					context.SetGraphicsDynamicConstantBufferView(1, viewData);

					const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
						m_SceneData.pMaterialBuffer->GetSRV()->GetDescriptor(),
						m_SceneData.pMeshBuffer->GetSRV()->GetDescriptor(),
					};
					context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));
					context.BindResourceTable(3, m_SceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

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
	}

	if (m_RenderPath == RenderPath::Tiled)
	{
		m_pTiledForward->Execute(graph, m_SceneData);
	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		m_pClusteredForward->Execute(graph, m_SceneData);
	}

	m_pParticles->Render(graph, GetCurrentRenderTarget(), GetDepthStencil(), *m_pCamera);

	RGPassBuilder sky = graph.AddPass("Sky");
	Data.DepthStencil = sky.Read(Data.DepthStencil);
	sky.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
		{
			Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			renderContext.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_Store, false);

			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			renderContext.SetPipelineState(m_pSkyboxPSO);
			renderContext.SetGraphicsRootSignature(m_pSkyboxRS.get());

			struct Parameters
			{
				Matrix View;
				Matrix Projection;
				Vector3 Bias;
				float padding1;
				Vector3 SunDirection;
				float padding2;
			} constBuffer;

			constBuffer.View = m_pCamera->GetView();
			constBuffer.Projection = m_pCamera->GetProjection();
			constBuffer.Bias = Vector3::One;
			constBuffer.SunDirection = -m_Lights[0].Direction;
			constBuffer.SunDirection.Normalize();

			renderContext.SetGraphicsDynamicConstantBufferView(0, constBuffer);

			renderContext.Draw(0, 36);

			renderContext.EndRenderPass();
		});

	DebugRenderer::Get()->Render(graph, m_pCamera->GetViewProjection(), GetCurrentRenderTarget(), GetDepthStencil());

	RGPassBuilder resolve = graph.AddPass("Resolve");
	resolve.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			if (m_SampleCount > 1)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				Texture* pTarget = Tweakables::g_TAA.Get() ? m_pTAASource.get() : m_pHDRRenderTarget.get();
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, pTarget, 0, GraphicsDevice::RENDER_TARGET_FORMAT);
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

	if (Tweakables::g_RaytracedReflections)
	{
		m_pRTReflections->Execute(graph, m_SceneData);
	}

	if (Tweakables::g_TAA.Get())
	{
		RGPassBuilder temporalResolve = graph.AddPass("Temporal Resolve");
		temporalResolve.Bind([=](CommandContext& renderContext, const RGPassResources& /*resources*/)
			{
				renderContext.InsertResourceBarrier(m_pTAASource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				renderContext.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pVelocity.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pPreviousColor.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

				renderContext.SetComputeRootSignature(m_pTemporalResolveRS.get());
				renderContext.SetPipelineState(m_pTemporalResolvePSO);

				struct Parameters
				{
					Vector2 InvScreenDimensions;
					Vector2 Jitter;
				} parameters;

				parameters.InvScreenDimensions = Vector2(1.0f / m_WindowWidth, 1.0f / m_WindowHeight);
				parameters.Jitter.x = m_pCamera->GetPreviousJitter().x - m_pCamera->GetJitter().x;
				parameters.Jitter.y = -(m_pCamera->GetPreviousJitter().y - m_pCamera->GetJitter().y);
				renderContext.SetComputeDynamicConstantBufferView(0, parameters);

				renderContext.BindResource(1, 0, m_pHDRRenderTarget->GetUAV());
				renderContext.BindResource(2, 0, m_pVelocity->GetSRV());
				renderContext.BindResource(2, 1, m_pPreviousColor->GetSRV());
				renderContext.BindResource(2, 2, m_pTAASource->GetSRV());
				renderContext.BindResource(2, 3, GetResolvedDepthStencil()->GetSRV());

				int dispatchGroupsX = Math::DivideAndRoundUp(m_WindowWidth, 8);
				int dispatchGroupsY = Math::DivideAndRoundUp(m_WindowHeight, 8);
				renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);

				renderContext.CopyTexture(m_pHDRRenderTarget.get(), m_pPreviousColor.get());
			});
	}

	//Tonemapping
	{
		RG_GRAPH_SCOPE("Tonemapping", graph);
		bool downscaleTonemapInput = true;
		Texture* pToneMapInput = downscaleTonemapInput ? m_pDownscaledColor.get() : m_pHDRRenderTarget.get();
		RGResourceHandle toneMappingInput = graph.ImportTexture("Tonemap Input", pToneMapInput);

		if (downscaleTonemapInput)
		{
			RGPassBuilder colorDownsample = graph.AddPass("Downsample Color");
			toneMappingInput = colorDownsample.Write(toneMappingInput);
			colorDownsample.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					Texture* pToneMapInput = resources.GetTexture(toneMappingInput);
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

					context.SetComputeDynamicConstantBufferView(0, Parameters);
					context.BindResource(1, 0, pToneMapInput->GetUAV());
					context.BindResource(2, 0, m_pHDRRenderTarget->GetSRV());

					context.Dispatch(
						Math::DivideAndRoundUp(Parameters.TargetDimensions.x, 8),
						Math::DivideAndRoundUp(Parameters.TargetDimensions.y, 8)
					);
				});
		}

		RGPassBuilder histogram = graph.AddPass("Luminance Histogram");
		toneMappingInput = histogram.Read(toneMappingInput);
		histogram.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pToneMapInput = resources.GetTexture(toneMappingInput);

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

				context.SetComputeDynamicConstantBufferView(0, Parameters);
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

				context.SetPipelineState(m_pAverageLuminancePSO);
				context.SetComputeRootSignature(m_pAverageLuminanceRS.get());

				struct AverageParameters
				{
					int32 PixelCount;
					float MinLogLuminance;
					float LogLuminanceRange;
					float TimeDelta;
					float Tau;
				} Parameters;

				Parameters.PixelCount = pToneMapInput->GetWidth() * pToneMapInput->GetHeight();
				Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance.Get();
				Parameters.LogLuminanceRange = Tweakables::g_MaxLogLuminance.Get() - Tweakables::g_MinLogLuminance.Get();
				Parameters.TimeDelta = Time::DeltaTime();
				Parameters.Tau = Tweakables::g_Tau.Get();

				context.SetComputeDynamicConstantBufferView(0, Parameters);
				context.BindResource(1, 0, m_pAverageLuminance->GetUAV());
				context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());

				context.Dispatch(1);
			});

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

				context.SetPipelineState(m_pToneMapPSO);
				context.SetComputeRootSignature(m_pToneMapRS.get());

				context.SetComputeDynamicConstantBufferView(0, constBuffer);

				context.BindResource(1, 0, m_pTonemapTarget->GetUAV());
				context.BindResource(2, 0, m_pHDRRenderTarget->GetSRV());
				context.BindResource(2, 1, m_pAverageLuminance->GetSRV());

				context.Dispatch(
					Math::DivideAndRoundUp(m_pHDRRenderTarget->GetWidth(), 16),
					Math::DivideAndRoundUp(m_pHDRRenderTarget->GetHeight(), 16)
				);
			});

		if (Tweakables::g_EnableUI && Tweakables::g_DrawHistogram.Get())
		{
			if (!m_pDebugHistogramTexture)
			{
				m_pDebugHistogramTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(m_pLuminanceHistogram->GetNumElements() * 4, m_pLuminanceHistogram->GetNumElements(), DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Debug Histogram");
			}

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

					context.SetComputeDynamicConstantBufferView(0, Parameters);
					context.BindResource(1, 0, m_pDebugHistogramTexture->GetUAV());
					context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());
					context.BindResource(2, 1, m_pLuminanceHistogram->GetSRV());
					context.ClearUavUInt(m_pDebugHistogramTexture.get(), m_pDebugHistogramTexture->GetUAV());
					context.Dispatch(1, m_pLuminanceHistogram->GetNumElements());
				});

			ImGui::ImageAutoSize(m_pDebugHistogramTexture.get(), ImVec2((float)m_pDebugHistogramTexture->GetWidth(), (float)m_pDebugHistogramTexture->GetHeight()));
		}
	}

	if (Tweakables::g_VisualizeLightDensity)
	{
		if (m_RenderPath == RenderPath::Clustered)
		{
			m_pClusteredForward->VisualizeLightDensity(graph, *m_pCamera, m_pTonemapTarget.get(), GetResolvedDepthStencil());
		}
		else
		{
			m_pTiledForward->VisualizeLightDensity(graph, m_pDevice.get(), *m_pCamera, m_pTonemapTarget.get(), GetResolvedDepthStencil());
		}

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

	//UI
	// - ImGui render, pretty straight forward
	if (Tweakables::g_EnableUI)
	{
		m_pImGuiRenderer->Render(graph, m_SceneData, m_pTonemapTarget.get());
	}
	else
	{
		ImGui::Render();
	}

	RGPassBuilder temp = graph.AddPass("Temp Barriers");
	temp.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			context.CopyTexture(m_pTonemapTarget.get(), GetCurrentBackbuffer());
			context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
		});

	graph.Compile();
	if (Tweakables::g_DumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		Tweakables::g_DumpRenderGraph = false;
	}
	nextFenceValue = graph.Execute();
	PROFILE_END();

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
		D3D::EndPixCapture();
		m_CapturePix = false;
	}
}

void DemoApp::OnResize(int width, int height)
{
	E_LOG(Info, "Viewport resized: %dx%d", width, height);
	m_WindowWidth = width;
	m_WindowHeight = height;

	m_pDevice->IdleGPU();
	m_pSwapchain->OnResize(width, height);

	m_pDepthStencil = m_pDevice->CreateTexture(TextureDesc::CreateDepth(width, height, GraphicsDevice::DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)), "Depth Stencil");
	m_pResolvedDepthStencil = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Resolved Depth Stencil");

	if (m_SampleCount > 1)
	{
		m_pMultiSampleRenderTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, GraphicsDevice::RENDER_TARGET_FORMAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Colors::Black)), "MSAA Target");
	}

	m_pNormals = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Colors::Black)), "MSAA Normals");
	m_pResolvedNormals = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, 1, ClearBinding(Colors::Black)), "Normals");
	m_pHDRRenderTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, GraphicsDevice::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "HDR Target");
	m_pPreviousColor = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, GraphicsDevice::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource), "Previous Color");
	m_pTonemapTarget = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, SWAPCHAIN_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "Tonemap Target");
	m_pDownscaledColor = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), GraphicsDevice::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Downscaled HDR Target");
	m_pAmbientOcclusion = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "SSAO");
	m_pVelocity = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Velocity");
	m_pTAASource = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(width, height, GraphicsDevice::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess), "TAA Target");

	m_pClusteredForward->OnResize(width, height);
	m_pTiledForward->OnResize(width, height);
	m_pSSAO->OnResize(width, height);
	m_pRTReflections->OnResize(width, height);

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

	m_pCamera->SetViewport(FloatRect(0, 0, (float)width, (float)height));
}

void DemoApp::InitializePipelines()
{
	//Input layout
	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader* pVertexShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
			Shader* pAlphaClipShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

			//Rootsignature
			m_pShadowsRS = std::make_unique<RootSignature>(m_pDevice.get());
			m_pShadowsRS->FinalizeFromShader("Shadow Mapping (Opaque)", pVertexShader);

			//Pipeline state
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

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader* pVertexShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = m_pDevice->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", pVertexShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(nullptr, 0, GraphicsDevice::DEPTH_STENCIL_FORMAT, m_SampleCount);
		psoDesc.SetName("Depth Prepass Opaque");
		m_pDepthPrepassOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetName("Depth Prepass Alpha Mask");
		m_pDepthPrepassAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Luminance Historgram
	{
		Shader* pComputeShader = m_pDevice->GetShader("LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Historgram", pComputeShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Luminance Historgram");
		m_pLuminanceHistogramPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pLuminanceHistogram = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(sizeof(uint32) * 256), "Luminance Histogram");
		m_pAverageLuminance = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource), "Average Luminance");
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

		//Rootsignature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance", pComputeShader);

		//Pipeline state
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

		//Rootsignature
		m_pToneMapRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pToneMapRS->FinalizeFromShader("Tonemapping", pComputeShader);

		//Pipeline state
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

		//Rootsignature
		m_pSkyboxRS = std::make_unique<RootSignature>(m_pDevice.get());
		m_pSkyboxRS->FinalizeFromShader("Skybox", pVertexShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pSkyboxRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(GraphicsDevice::GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::GraphicsDevice::DEPTH_STENCIL_FORMAT, m_SampleCount);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = m_pDevice->CreatePipeline(psoDesc);
	}
}

void DemoApp::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = Time::DeltaTime();

	ImGui::ShowDemoWindow();

	if (m_pVisualizeTexture)
	{
		ImGui::Begin("Visualize Texture");
		ImGui::Text("Resolution: %dx%d", m_pVisualizeTexture->GetWidth(), m_pVisualizeTexture->GetHeight());
		ImGui::ImageAutoSize(m_pVisualizeTexture, ImVec2((float)m_pVisualizeTexture->GetWidth(), (float)m_pVisualizeTexture->GetHeight()));
		ImGui::End();
	}

	if (Tweakables::g_VisualizeShadowCascades)
	{
		if (m_ShadowMaps.size() >= 4)
		{
			float imageSize = 230;
			ImGui::SetNextWindowSize(ImVec2(imageSize, 1024));
			ImGui::SetNextWindowPos(ImVec2(m_WindowWidth - imageSize, 0));
			ImGui::Begin("Shadow Cascades", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
			const Light& sunLight = m_Lights[0];
			for (int i = 0; i < 4; ++i)
			{
				ImGui::Image(m_ShadowMaps[sunLight.ShadowIndex + i].get(), ImVec2(imageSize, imageSize));
			}
			ImGui::End();
		}
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %4.2f", Time::DeltaTime() * 1000.0f);
	ImGui::SameLine(100.0f);
	ImGui::Text("%d x %d", m_WindowWidth, m_WindowHeight);
	ImGui::SameLine(180.0f);
	ImGui::Text("%dx MSAA", m_SampleCount);
	ImGui::PlotLines("", m_FrameTimes.data(), (int)m_FrameTimes.size(), m_Frame % m_FrameTimes.size(), 0, 0.0f, 0.03f, ImVec2(ImGui::GetContentRegionAvail().x, 100));

	ImGui::Text("Camera: [%f, %f, %f]", m_pCamera->GetPosition().x, m_pCamera->GetPosition().y, m_pCamera->GetPosition().z);

	if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
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
				default:
					break;
				}
				return true;
			}, nullptr, 2);

		ImGui::Separator();

		if (ImGui::Button("Dump RenderGraph"))
		{
			Tweakables::g_DumpRenderGraph = true;
		}
		if (ImGui::Button("Screenshot"))
		{
			Tweakables::g_Screenshot = true;
		}
		if (ImGui::Button("Pix Capture"))
		{
			m_CapturePix = true;
		}

		ImGui::TreePop();
	}
	/*if (ImGui::TreeNodeEx("Descriptor Heaps", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Used CPU Descriptor Heaps");
		for (const auto& pAllocator : m_DescriptorHeaps)
		{
			switch (pAllocator->GetType())
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
				ImGui::TextWrapped("Constant/Shader/Unordered Access Views");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				ImGui::TextWrapped("Samplers");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				ImGui::TextWrapped("Render Target Views");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				ImGui::TextWrapped("Depth Stencil Views");
				break;
			default:
				break;
			}
			uint32 totalDescriptors = pAllocator->GetNumDescriptors();
			uint32 usedDescriptors = pAllocator->GetNumAllocatedDescriptors();
			std::string str = SPrintf("%d/%d", usedDescriptors, totalDescriptors);
			ImGui::ProgressBar((float)usedDescriptors / Math::Max(1u, totalDescriptors), ImVec2(-1, 0), str.c_str());
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Dynamic Upload Memory");
		ImGui::Text("%.2f MB", Math::BytesToMegaBytes * m_pDynamicAllocationManager->GetMemoryUsage());
		ImGui::TreePop();
	}*/
	if (ImGui::TreeNodeEx("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ProfileNode* pRootNode = Profiler::Get()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::TreePop();
	}

	ImGui::End();

	static ImGuiConsole console;
	console.Update(ImVec2(300, (float)m_WindowHeight), ImVec2((float)m_WindowWidth - 300 * 2, 250));


	ImGui::SetNextWindowPos(ImVec2((float)m_WindowWidth, 0), 0, ImVec2(1, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("Parameters", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

	ImGui::Text("Sky");
	ImGui::SliderFloat("Sun Orientation", &Tweakables::g_SunOrientation, -Math::PI, Math::PI);
	ImGui::SliderFloat("Sun Inclination", &Tweakables::g_SunInclination, 0, 1);
	ImGui::SliderFloat("Sun Temperature", &Tweakables::g_SunTemperature, 1000, 15000);
	ImGui::SliderFloat("Sun Intensity", &Tweakables::g_SunIntensity, 0, 30);

	ImGui::Text("Shadows");
	ImGui::SliderInt("Shadow Cascades", &Tweakables::g_ShadowCascades.Get(), 1, 4);
	ImGui::Checkbox("SDSM", &Tweakables::g_SDSM.Get());
	ImGui::Checkbox("Stabilize Cascades", &Tweakables::g_StabilizeCascades.Get());
	ImGui::SliderFloat("PSSM Factor", &Tweakables::g_PSSMFactor.Get(), 0, 1);
	ImGui::Checkbox("Visualize Cascades", &Tweakables::g_VisualizeShadowCascades.Get());

	ImGui::Text("Expose/Tonemapping");

	ImGui::DragFloatRange2("Log Luminance", &Tweakables::g_MinLogLuminance.Get(), &Tweakables::g_MaxLogLuminance.Get(), 1.0f, -100, 50);
	ImGui::Checkbox("Draw Exposure Histogram", &Tweakables::g_DrawHistogram.Get());
	ImGui::SliderFloat("White Point", &Tweakables::g_WhitePoint.Get(), 0, 20);

	constexpr static const char* tonemappers[] = {
		"Reinhard",
		"Reinhard Extended",
		"ACES Fast",
		"Unreal 3",
		"Uncharted 2",
	};

	ImGui::Combo("Tonemapper", (int*)&Tweakables::g_ToneMapper.Get(), [](void* /*data*/, int index, const char** outText)
		{
			if (index < ARRAYSIZE(tonemappers))
			{
				*outText = tonemappers[index];
				return true;
			}
			return false;
		}, nullptr, 5);

	ImGui::SliderFloat("Tau", &Tweakables::g_Tau.Get(), 0, 5);

	ImGui::Text("Misc");
	ImGui::Checkbox("Debug Render Lights", &Tweakables::g_VisualizeLights.Get());
	ImGui::Checkbox("Visualize Light Density", &Tweakables::g_VisualizeLightDensity.Get());
	extern bool g_VisualizeClusters;
	ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);
	ImGui::SliderInt("SSR Samples", &Tweakables::g_SsrSamples.Get(), 0, 32);
	ImGui::Checkbox("Object Bounds", &Tweakables::g_RenderObjectBounds.Get());

	if (m_pDevice->GetCapabilities().SupportsRaytracing())
	{
		ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO.Get());
		ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections.Get());
		ImGui::SliderAngle("TLAS Bounds Threshold", &Tweakables::g_TLASBoundsThreshold.Get(), 0, 40);
	}

	ImGui::Checkbox("TAA", &Tweakables::g_TAA.Get());

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
					if (material.IsTransparent == false)
					{
						geometryDesc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
					}
					geometryDesc.Triangles.IndexBuffer = subMesh.IndicesLocation.Location;
					geometryDesc.Triangles.IndexCount = subMesh.IndicesLocation.Elements;
					geometryDesc.Triangles.IndexFormat = subMesh.IndicesLocation.Format;
					geometryDesc.Triangles.Transform3x4 = 0;
					geometryDesc.Triangles.VertexBuffer.StartAddress = subMesh.VerticesLocation.Location;
					geometryDesc.Triangles.VertexBuffer.StrideInBytes = subMesh.VerticesLocation.Stride;
					geometryDesc.Triangles.VertexCount = subMesh.VerticesLocation.Elements;
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
					GetDevice()->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

					std::unique_ptr<Buffer> pBLASScratch = GetDevice()->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess), "BLAS Scratch Buffer");
					std::unique_ptr<Buffer> pBLAS = GetDevice()->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::AccelerationStructure), "BLAS Buffer");

					D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
					asDesc.Inputs = prebuildInfo;
					asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
					asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
					asDesc.SourceAccelerationStructureData = 0;

					pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
					context.InsertUavBarrier(subMesh.pBLAS);
					context.FlushResourceBarriers();

					subMesh.pBLAS = pBLAS.release();
					if (0) //#todo: Can delete scratch buffer if no upload is required
					{
						subMesh.pBLASScratch = pBLASScratch.release();
					}
				}
			}
		}

		bool isUpdate = m_pTLAS != nullptr;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
		for (uint32 instanceIndex = 0; instanceIndex < (uint32)m_SceneData.Batches.size(); ++instanceIndex)
		{
			const Batch& batch = m_SceneData.Batches[instanceIndex];

			// Cull object that are small to the viewer - Deligiannis2019
			Vector3 cameraVec = (batch.Bounds.Center - m_pCamera->GetPosition());
			float angle = tanf(batch.Radius / cameraVec.Length());
			if (angle < Tweakables::g_TLASBoundsThreshold && cameraVec.Length() > batch.Radius)
			{
				continue;
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
			instanceDesc.InstanceID = batch.Index;
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

		if (!isUpdate)
		{
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
			prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			prebuildInfo.Flags = buildFlags;
			prebuildInfo.NumDescs = (uint32)instanceDescs.size();

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
			m_pDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

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
