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
#include "Graphics/Mesh.h"
#include "Core/Input.h"
#include "Texture.h"
#include "Scene/Camera.h"
#include "ResourceViews.h"
#include "GraphicsBuffer.h"
#include "Graphics/Profiler.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/Blackboard.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/Techniques/ClusteredForward.h"
#include "Graphics/Techniques/TiledForward.h"
#include "Graphics/Techniques/RTAO.h"
#include "Graphics/Techniques/SSAO.h"
#include "Graphics/Techniques/RTReflections.h"
#include "Graphics/Techniques/GpuParticles.h"
#include "Core/CommandLine.h"
#include "Content/Image.h"
#include "Core/TaskQueue.h"
#include "StateObject.h"
#include "ImGuizmo/ImGuizmo.h"
#include "Core/Paths.h"

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 0
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

void DrawScene(CommandContext& context, const SceneData& scene, Batch::Blending blendModes)
{
	std::vector<const Batch*> meshes;
	for (const Batch& b : scene.Batches)
	{
		if (EnumHasAnyFlags(b.BlendMode, blendModes) && scene.VisibilityMask.GetBit(b.Index))
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
		Matrix World;
		MaterialData Material;
		uint32 VertexBuffer;
	} objectData;

	for (const Batch* b : meshes)
	{
		objectData.World = b->WorldMatrix;
		objectData.Material = b->Material;
		objectData.VertexBuffer = b->VertexBufferDescriptor;
		context.SetGraphicsDynamicConstantBufferView(0, objectData);
		context.SetIndexBuffer(b->pMesh->IndicesLocation);
		context.DrawIndexed(b->pMesh->IndicesLocation.Elements, 0, 0);
	}
}

const DXGI_FORMAT Graphics::DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
const DXGI_FORMAT Graphics::DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
const DXGI_FORMAT Graphics::RENDER_TARGET_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
const DXGI_FORMAT Graphics::SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

namespace Tweakables
{
	// Post processing
	float g_WhitePoint = 1;
	float g_MinLogLuminance = -10;
	float g_MaxLogLuminance = 20;
	float g_Tau = 2;
	bool g_DrawHistogram = false;
	int32 g_ToneMapper = 1;
	bool g_TAA = true;

	// Shadows
	bool g_SDSM = false;
	bool g_StabilizeCascades = true;
	bool g_VisualizeShadowCascades = false;
	int g_ShadowCascades = 4;
	float g_PSSMFactor = 1.0f;

	// Misc Lighting
	bool g_RaytracedAO = false;
	bool g_VisualizeLights = false;
	bool g_VisualizeLightDensity = false;

	// Lighting
	float g_SunInclination = 0.579f;
	float g_SunOrientation = -3.055f;
	float g_SunTemperature = 5900.0f;
	float g_SunIntensity = 3.0f;

	// Reflections
	bool g_RaytracedReflections = true;
	float g_TLASBoundsThreshold = 5.0f * Math::DegreesToRadians;
	int g_SsrSamples = 8;

	// Misc
	bool g_DumpRenderGraph = false;
	bool g_Screenshot = false;
	bool g_EnableUI = true;
	bool g_RenderObjectBounds = false;
}

Graphics::Graphics(uint32 width, uint32 height, int sampleCount /*= 1*/)
	: m_SampleCount(sampleCount), m_WindowWidth(width), m_WindowHeight(height)
{
}

Graphics::~Graphics()
{
}

void Graphics::Initialize(WindowHandle window)
{
	m_pWindow = window;

	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetPosition(Vector3(-30, 35, 48));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(3 * Math::PIDIV4, Math::PIDIV4 * 0.3f, 0));
	m_pCamera->SetNearPlane(300.0f);
	m_pCamera->SetFarPlane(1.0f);

	InitD3D();
	InitializePipelines();

	CommandContext* pContext = AllocateCommandContext();
	InitializeAssets(*pContext);
	SetupScene(*pContext);
	UpdateTLAS(*pContext);
	pContext->Execute(true);

	Tweakables::g_RaytracedAO = SupportsRayTracing() ? Tweakables::g_RaytracedAO : false;
	Tweakables::g_RaytracedReflections = SupportsRayTracing() ? Tweakables::g_RaytracedReflections : false;

	m_pDynamicAllocationManager->CollectGarbage();
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
	}

	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	Matrix view = camera.GetView();
	Matrix projection = camera.GetProjection();
	Math::ReverseZProjection(projection);
	ImGuizmo::Manipulate(&view.m[0][0], &projection.m[0][0], mCurrentGizmoOperation, mCurrentGizmoMode, &matrix.m[0][0], NULL, pSnapValue);
}

Matrix spotMatrix = Matrix::CreateScale(100.0f, 0.2f, 1) * Matrix::CreateFromYawPitchRoll(0.1f, 0, 0) * Matrix::CreateTranslation(0, 10, 0);

void Graphics::Update()
{
	PROFILE_BEGIN("Update");
	BeginFrame();
	m_pImGuiRenderer->Update();

	PROFILE_BEGIN("Update Game State");

	m_pShaderManager->ConditionallyReloadShaders();

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

#elif 0

	EditTransform(*m_pCamera, spotMatrix);
	Vector3 scale, position;
	Quaternion rotation;
	spotMatrix.Decompose(scale, rotation, position);
	m_Lights[1].Range = scale.x;
	m_Lights[1].Position = position;
	m_Lights[1].Direction = spotMatrix.Forward();

#endif

#if 0
	Vector3 pos = m_pCamera->GetPosition();
	pos.x = 48;
	pos.y = sin(5*Time::TotalTime()) * 4 + 84;
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

	ShadowData shadowData;

	float minPoint = 0;
	float maxPoint = 1;

	shadowData.NumCascades = Tweakables::g_ShadowCascades;

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

	int shadowIndex = 0;
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

				shadowData.CascadeDepths[shadowIndex] = currentCascadeSplit * (farPlane - nearPlane) + nearPlane;
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
			Matrix projection = Math::CreatePerspectiveMatrix(Math::PIDIV2, 1, light.Range, 1.0f);

			constexpr Vector3 cubemapDirections[] = {
				Vector3(-1.0f, 0.0f, 0.0f),
				Vector3(1.0f, 0.0f, 0.0f),
				Vector3(0.0f, -1.0f, 0.0f),
				Vector3(0.0f, 1.0f, 0.0f),
				Vector3(0.0f, 0.0f, -1.0f),
				Vector3(0.0f, 0.0f, 1.0f),
			};
			constexpr Vector3 cubemapUpDirections[] = {
				Vector3(0.0f, 1.0f, 0.0f),
				Vector3(0.0f, 1.0f, 0.0f),
				Vector3(0.0f, 0.0f, -1.0f),
				Vector3(0.0f, 0.0f, 1.0f),
				Vector3(0.0f, 1.0f, 0.0f),
				Vector3(0.0f, 1.0f, 0.0f),
			};

			for (int i = 0; i < 6; ++i)
			{
				shadowData.LightViewProjections[shadowIndex] = Matrix::CreateLookAt(light.Position, light.Position + cubemapDirections[i], cubemapUpDirections[i]) * projection;
				++shadowIndex;
			}
		}
	}

	if (shadowIndex > m_ShadowMaps.size())
	{
		m_ShadowMaps.resize(shadowIndex);
		int i = 0;
		for (auto& pShadowMap : m_ShadowMaps)
		{
			pShadowMap = std::make_unique<Texture>(this, "Shadow Map");
			if (i < 4)
				pShadowMap->Create(TextureDesc::CreateDepth(2048, 2048, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
			else
				pShadowMap->Create(TextureDesc::CreateDepth(512, 512, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
			++i;
			RegisterBindlessResource(pShadowMap.get(), nullptr);
		}
	}


	for (Light& light : m_Lights)
	{
		if (light.ShadowIndex >= 0)
		{
			light.ShadowMapSize = m_ShadowMaps[light.ShadowIndex]->GetWidth();
		}
	}

	shadowData.ShadowMapOffset = RegisterBindlessResource(m_ShadowMaps[0].get());
	m_SceneData.pDepthBuffer = GetDepthStencil();
	m_SceneData.pResolvedDepth = GetResolvedDepthStencil();
	m_SceneData.pRenderTarget = GetCurrentRenderTarget();
	m_SceneData.pLightBuffer = m_pLightBuffer.get();
	m_SceneData.pCamera = m_pCamera.get();
	m_SceneData.pShadowData = &shadowData;
	m_SceneData.pAO = m_pAmbientOcclusion.get();
	m_SceneData.FrameIndex = m_Frame;
	m_SceneData.pPreviousColor = m_pPreviousColor.get();
	m_SceneData.SceneTLAS = RegisterBindlessResource(m_pTLAS->GetSRV());
	m_SceneData.pNormals = m_pNormals.get();
	m_SceneData.pResolvedNormals = m_pResolvedNormals.get();
	m_SceneData.pResolvedTarget = Tweakables::g_TAA ? m_pTAASource.get() : m_pHDRRenderTarget.get();

	BoundingFrustum frustum = m_pCamera->GetFrustum();
	for (const Batch& b : m_SceneData.Batches)
	{
		m_SceneData.VisibilityMask.AssignBit(b.Index, frustum.Contains(b.Bounds));
	}

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////

	PROFILE_END();

	if (m_CapturePix)
	{
		D3D::BeginPixCapture();
	}

	RGGraph graph(this);
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
		screenshot.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
			{
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
				D3D12_RESOURCE_DESC resourceDesc = m_pTonemapTarget->GetResource()->GetDesc();
				m_pDevice->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
				m_pScreenshotBuffer = std::make_unique<Buffer>(this, "Screenshot Texture");
				m_pScreenshotBuffer->Create(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height));
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
				sprintf_s(filePath, "%sScreenshot_%d_%02d_%02d__%02d_%02d_%02d.jpg",
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
	updateTLAS.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
		{
			UpdateTLAS(renderContext);
		}
	);


	RGPassBuilder setupLights = graph.AddPass("Setup Lights");
	Data.DepthStencil = setupLights.Write(Data.DepthStencil);
	setupLights.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
		{
			DynamicAllocation allocation = renderContext.AllocateTransientMemory(m_Lights.size() * sizeof(Light::RenderData));
			Light::RenderData* pTarget = (Light::RenderData*)allocation.pMappedMemory;
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

			renderContext.BindResourceTable(2, m_SceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

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
		depthResolve.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
			{
				renderContext.CopyTexture(GetDepthStencil(), GetResolvedDepthStencil());
			});
	}

	// Camera velocity
	if (Tweakables::g_TAA)
	{
		RGPassBuilder cameraMotion = graph.AddPass("Camera Motion");
		cameraMotion.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
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
		shadows.Bind([=](CommandContext& context, const RGPassResources& resources)
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
					context.BindResourceTable(2, m_SceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pShadowsOpaquePSO);
						DrawScene(context, m_SceneData, Batch::Blending::Opaque);
					}
					{
						GPU_PROFILE_SCOPE("Masked", &context);
						context.SetPipelineState(m_pShadowsAlphaMaskPSO);
						DrawScene(context, m_SceneData, Batch::Blending::AlphaMask);
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
	resolve.Bind([=](CommandContext& context, const RGPassResources& resources)
		{
			if (m_SampleCount > 1)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				Texture* pTarget = Tweakables::g_TAA ? m_pTAASource.get() : m_pHDRRenderTarget.get();
				context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, pTarget, 0, RENDER_TARGET_FORMAT);
			}

			if (!Tweakables::g_TAA)
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

	if (Tweakables::g_TAA)
	{
		RGPassBuilder temporalResolve = graph.AddPass("Temporal Resolve");
		temporalResolve.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
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
				Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
				Parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance);

				context.SetComputeDynamicConstantBufferView(0, Parameters);
				context.BindResource(1, 0, m_pLuminanceHistogram->GetUAV());
				context.BindResource(2, 0, pToneMapInput->GetSRV());

				context.Dispatch(
					Math::DivideAndRoundUp(pToneMapInput->GetWidth(), 16),
					Math::DivideAndRoundUp(pToneMapInput->GetHeight(), 16)
				);
			});

		RGPassBuilder avgLuminance = graph.AddPass("Average Luminance");
		avgLuminance.Bind([=](CommandContext& context, const RGPassResources& resources)
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
				Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
				Parameters.LogLuminanceRange = Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance;
				Parameters.TimeDelta = Time::DeltaTime();
				Parameters.Tau = Tweakables::g_Tau;

				context.SetComputeDynamicConstantBufferView(0, Parameters);
				context.BindResource(1, 0, m_pAverageLuminance->GetUAV());
				context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());

				context.Dispatch(1);
			});

		RGPassBuilder tonemap = graph.AddPass("Tonemap");
		tonemap.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				struct Parameters
				{
					float WhitePoint;
					uint32 Tonemapper;
				} constBuffer;
				constBuffer.WhitePoint = Tweakables::g_WhitePoint;
				constBuffer.Tonemapper = Tweakables::g_ToneMapper;

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

		if (Tweakables::g_EnableUI && Tweakables::g_DrawHistogram)
		{
			RGPassBuilder drawHistogram = graph.AddPass("Draw Histogram");
			drawHistogram.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetPipelineState(m_pDrawHistogramPSO);
					context.SetComputeRootSignature(m_pDrawHistogramRS.get());

					struct AverageParameters
					{
						float MinLogLuminance;
						float InverseLogLuminanceRange;
					} Parameters;

					Parameters.MinLogLuminance = Tweakables::g_MinLogLuminance;
					Parameters.InverseLogLuminanceRange = 1.0f / (Tweakables::g_MaxLogLuminance - Tweakables::g_MinLogLuminance);

					context.SetComputeDynamicConstantBufferView(0, Parameters);
					context.BindResource(1, 0, m_pTonemapTarget->GetUAV());
					context.BindResource(2, 0, m_pLuminanceHistogram->GetSRV());
					context.BindResource(2, 1, m_pAverageLuminance->GetSRV());

					context.Dispatch(1, m_pLuminanceHistogram->GetNumElements());
				});
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
			m_pTiledForward->VisualizeLightDensity(graph, *m_pCamera, m_pTonemapTarget.get(), GetResolvedDepthStencil());
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

		for (int i = 0; i < ARRAYSIZE(DEBUG_COLORS); ++i)
		{
			char number[16];
			sprintf_s(number, "%d", i);
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
	temp.Bind([=](CommandContext& context, const RGPassResources& resources)
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
	EndFrame(nextFenceValue);

	if (m_CapturePix)
	{
		D3D::EndPixCapture();
		m_CapturePix = false;
	}
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	IdleGPU();
#if !PLATFORM_UWP
	UnregisterWait(m_DeviceRemovedEvent);
#endif

	m_pSwapchain->SetFullscreenState(false, nullptr);
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame(m_WindowWidth, m_WindowHeight);
}

void Graphics::EndFrame(uint64 fenceValue)
{
	Profiler::Get()->Resolve(this, m_Frame);

	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	++m_Frame;
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D()");

	if (CommandLine::GetBool("pix"))
	{
		if (GetModuleHandleA("WinPixGpuCapturer.dll") == 0)
		{
			std::string pixPath;
			if (D3D::GetLatestWinPixGpuCapturerPath(pixPath))
			{
				if (LoadLibraryA(pixPath.c_str()))
				{
					E_LOG(Warning, "Dynamically loaded PIX ('%s')", pixPath.c_str());
				}
			}
		}
	}

	bool debugD3D = CommandLine::GetBool("d3ddebug") || D3D_VALIDATION;
	bool gpuValidation = CommandLine::GetBool("gpuvalidation") || GPU_VALIDATION;

	UINT dxgiFactoryFlags = 0;
	if (debugD3D)
	{
		ComPtr<ID3D12Debug> pDebugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
		{
			pDebugController->EnableDebugLayer();
			E_LOG(Warning, "D3D12 Debug Layer Enabled");
		}

		if (gpuValidation)
		{
			ComPtr<ID3D12Debug1> pDebugController1;
			if (SUCCEEDED(pDebugController->QueryInterface(IID_PPV_ARGS(&pDebugController1))))
			{
				pDebugController1->SetEnableGPUBasedValidation(true);
				E_LOG(Warning, "D3D12 GPU Based Validation Enabled");
			}
		}

		// Enable additional debug layers.
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}

	if (CommandLine::GetBool("dred"))
	{
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings))))
		{
			// Turn on auto-breadcrumbs and page fault reporting.
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			E_LOG(Warning, "DRED Enabled");
		}
	}

	//Create the factory
	ComPtr<IDXGIFactory6> pFactory;
	VERIFY_HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory)));

	BOOL allowTearing = true;
	HRESULT hr = pFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(BOOL));
	allowTearing &= SUCCEEDED(hr);

	bool setStablePowerState = CommandLine::GetBool("stablepowerstate");

	if (setStablePowerState)
	{
		D3D12EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);
	}

	ComPtr<IDXGIAdapter4> pAdapter;

	bool useWarpAdapter = CommandLine::GetBool("warp");
	if (!useWarpAdapter)
	{
		uint32 adapterIndex = 0;
		E_LOG(Info, "Adapters:");
		DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
		while (pFactory->EnumAdapterByGpuPreference(adapterIndex++, gpuPreference, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) == S_OK)
		{
			DXGI_ADAPTER_DESC3 desc;
			pAdapter->GetDesc3(&desc);
			E_LOG(Info, "\t%s - %f GB", UNICODE_TO_MULTIBYTE(desc.Description), (float)desc.DedicatedVideoMemory * Math::BytesToGigaBytes);

			uint32 outputIndex = 0;
			ComPtr<IDXGIOutput> pOutput;
			while (pAdapter->EnumOutputs(outputIndex++, pOutput.ReleaseAndGetAddressOf()) == S_OK)
			{
				ComPtr<IDXGIOutput6> pOutput1;
				pOutput.As(&pOutput1);
				DXGI_OUTPUT_DESC1 outputDesc;
				pOutput1->GetDesc1(&outputDesc);

				E_LOG(Info, "\t\tMonitor %d - %dx%d - HDR: %s - %d BPP",
					outputIndex,
					outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
					outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top,
					outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "Yes" : "No",
					outputDesc.BitsPerColor);
			}
		}
		pFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(pAdapter.GetAddressOf()));
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);
		E_LOG(Info, "Using %s", UNICODE_TO_MULTIBYTE(desc.Description));

		//Create the device
		constexpr D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};
		auto GetFeatureLevelName = [](D3D_FEATURE_LEVEL featureLevel) {
			switch (featureLevel)
			{
			case D3D_FEATURE_LEVEL_12_2: return "D3D_FEATURE_LEVEL_12_2";
			case D3D_FEATURE_LEVEL_12_1: return "D3D_FEATURE_LEVEL_12_1";
			case D3D_FEATURE_LEVEL_12_0: return "D3D_FEATURE_LEVEL_12_0";
			case D3D_FEATURE_LEVEL_11_1: return "D3D_FEATURE_LEVEL_11_1";
			case D3D_FEATURE_LEVEL_11_0: return "D3D_FEATURE_LEVEL_11_0";
			default: noEntry(); return "";
			}
		};

		VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));
		D3D12_FEATURE_DATA_FEATURE_LEVELS caps{};
		caps.pFeatureLevelsRequested = featureLevels;
		caps.NumFeatureLevels = ARRAYSIZE(featureLevels);
		VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)), GetDevice());
		VERIFY_HR_EX(D3D12CreateDevice(pAdapter.Get(), caps.MaxSupportedFeatureLevel, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())), GetDevice());
		E_LOG(Info, "D3D12 Device Created: %s", GetFeatureLevelName(caps.MaxSupportedFeatureLevel));
	}

	if (!m_pDevice)
	{
		E_LOG(Warning, "No D3D12 Adapter selected. Falling back to WARP");
		pFactory->EnumWarpAdapter(IID_PPV_ARGS(pAdapter.GetAddressOf()));
		VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())));
	}

	m_pDevice.As(&m_pRaytracingDevice);
	D3D::SetObjectName(m_pDevice.Get(), "Main Device");

#if !PLATFORM_UWP
	auto OnDeviceRemovedCallback = [](void* pContext, BOOLEAN) {
		Graphics* pGraphics = (Graphics*)pContext;
		std::string error = D3D::GetErrorString(DXGI_ERROR_DEVICE_REMOVED, pGraphics->GetDevice());
		E_LOG(Error, "%s", error.c_str());
	};

	HANDLE deviceRemovedEvent = CreateEventA(nullptr, false, false, nullptr);
	VERIFY_HR(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pDeviceRemovalFence.GetAddressOf())));
	m_pDeviceRemovalFence->SetEventOnCompletion(UINT64_MAX, deviceRemovedEvent);
	RegisterWaitForSingleObject(&m_DeviceRemovedEvent, deviceRemovedEvent, OnDeviceRemovedCallback, this, INFINITE, 0);
#endif

	if (setStablePowerState)
	{
		VERIFY_HR(m_pDevice->SetStablePowerState(true));
		E_LOG(Warning, "Stable Power State enabled");
	}

	if (debugD3D)
	{
		ID3D12InfoQueue* pInfoQueue = nullptr;
		if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
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
				E_LOG(Warning, "D3D Validation Break on Severity Enabled");
			}
			pInfoQueue->PushStorageFilter(&NewFilter);
			pInfoQueue->Release();
		}
	}

	//Feature checks
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS caps0{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &caps0, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS))))
		{
			checkf(caps0.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2, "Device does not support Resource Heap Tier 2 or higher. Tier 1 is not supported");
			checkf(caps0.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3, "Device does not support Resource Binding Tier 3 or higher. Tier 2 and under is not supported.");
		}
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps5{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &caps5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5))))
		{
			m_RenderPassTier = caps5.RenderPassesTier;
			m_RayTracingTier = caps5.RaytracingTier;
		}
		D3D12_FEATURE_DATA_D3D12_OPTIONS6 caps6{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &caps6, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS6))))
		{
			m_VRSTier = caps6.VariableShadingRateTier;
			m_VRSTileSize = caps6.ShadingRateImageTileSize;
		}
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps7{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &caps7, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7))))
		{
			m_MeshShaderSupport = caps7.MeshShaderTier;
			m_SamplerFeedbackSupport = caps7.SamplerFeedbackTier;
		}
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelSupport{};
		shaderModelSupport.HighestShaderModel = D3D_SHADER_MODEL_6_7;
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelSupport, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))))
		{
			m_ShaderModelMajor = (uint8)(shaderModelSupport.HighestShaderModel >> 0x4);
			m_ShaderModelMinor = (uint8)(shaderModelSupport.HighestShaderModel & 0xF);
		}
	}

	//Create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this, BufferFlag::Upload);

	m_pGlobalViewHeap = std::make_unique<GlobalOnlineDescriptorHeap>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2000, 1000000);
	m_pPersistentDescriptorHeap = std::make_unique<OnlineDescriptorAllocator>(m_pGlobalViewHeap.get());
	m_SceneData.GlobalSRVHeapHandle = m_pGlobalViewHeap->GetStartHandle();
	
	check(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = SWAPCHAIN_FORMAT;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Flags = 0;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;
	ComPtr<IDXGISwapChain1> swapChain;
	swapchainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

#if PLATFORM_WINDOWS
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.RefreshRate.Denominator = 60;
	fsDesc.RefreshRate.Numerator = 1;
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fsDesc.Windowed = true;

	VERIFY_HR(pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(),
		m_pWindow,
		&swapchainDesc,
		&fsDesc,
		nullptr,
		swapChain.GetAddressOf()));
#elif PLATFORM_UWP
	VERIFY_HR(pFactory->CreateSwapChainForCoreWindow(m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(),
		reinterpret_cast<IUnknown*>(winrt::get_abi(winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())),
		&swapchainDesc,
		nullptr,
		swapChain.GetAddressOf()));
#endif

	m_pShaderManager = std::make_unique<ShaderManager>("Resources/Shaders/", m_ShaderModelMajor, m_ShaderModelMinor);

	m_pSwapchain.Reset();
	swapChain.As(&m_pSwapchain);

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_Backbuffers[i] = std::make_unique<Texture>(this, "Render Target");
	}
	m_pDepthStencil = std::make_unique<Texture>(this, "Depth Stencil");
	m_pResolvedDepthStencil = std::make_unique<Texture>(this, "Resolved Depth Stencil");

	if (m_SampleCount > 1)
	{
		m_pMultiSampleRenderTarget = std::make_unique<Texture>(this, "MSAA Target");
	}

	m_pNormals = std::make_unique<Texture>(this, "MSAA Normals");
	m_pResolvedNormals = std::make_unique<Texture>(this, "Normals");
	m_pHDRRenderTarget = std::make_unique<Texture>(this, "HDR Target");
	m_pPreviousColor = std::make_unique<Texture>(this, "Previous Color");
	m_pTonemapTarget = std::make_unique<Texture>(this, "Tonemap Target");
	m_pDownscaledColor = std::make_unique<Texture>(this, "Downscaled HDR Target");
	m_pAmbientOcclusion = std::make_unique<Texture>(this, "SSAO");
	m_pVelocity = std::make_unique<Texture>(this, "Velocity");
	m_pTAASource = std::make_unique<Texture>(this, "TAA Target");

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this, BufferFlag::Upload);
	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pTiledForward = std::make_unique<TiledForward>(this);
	m_pRTReflections = std::make_unique<RTReflections>(this);
	m_pRTAO = std::make_unique<RTAO>(this);
	m_pSSAO = std::make_unique<SSAO>(this);
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));
	m_pParticles = std::make_unique<GpuParticles>(this);

	Profiler::Get()->Initialize(this);
	DebugRenderer::Get()->Initialize(this);

	OnResize(m_WindowWidth, m_WindowHeight);
}

void Graphics::InitializeAssets(CommandContext& context)
{
	auto RegisterDefaultTexture = [this, &context](DefaultTexture type, const char* pName, const TextureDesc& desc, uint32* pData) {
		m_DefaultTextures[(int)type] = std::make_unique<Texture>(this, pName);
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
	uint32 DEFAULT_ROUGHNESS_METALNESS = 0xFF0080FF;
	RegisterDefaultTexture(DefaultTexture::RoughnessMetalness, "Default Roughness/Metalness", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_ROUGHNESS_METALNESS);

	uint32 BLACK_CUBE[6] = {};
	RegisterDefaultTexture(DefaultTexture::BlackCube, "Default Black Cube", TextureDesc::CreateCube(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), BLACK_CUBE);

	m_DefaultTextures[(int)DefaultTexture::ColorNoise256] = std::make_unique<Texture>(this, "Color Noise 256px");
	m_DefaultTextures[(int)DefaultTexture::ColorNoise256]->Create(&context, "Resources/Textures/Noise.png", false);
	m_DefaultTextures[(int)DefaultTexture::BlueNoise512] = std::make_unique<Texture>(this, "Blue Noise 512px");
	m_DefaultTextures[(int)DefaultTexture::BlueNoise512]->Create(&context, "Resources/Textures/BlueNoise.dds", false);
}

void Graphics::SetupScene(CommandContext& context)
{
	m_pLightCookie = std::make_unique<Texture>(this, "Light Cookie");
	m_pLightCookie->Create(&context, "Resources/Textures/LightProjector.png", false);

	{
		std::unique_ptr<Mesh> pMesh = std::make_unique<Mesh>();
		//pMesh->Load("Resources/Bistro_Godot/Bistro_Godot.gltf", this, &context, 3.0f);
		//pMesh->Load("Resources/Bathroom/scene.gltf", this, &context, 0.2f);
		pMesh->Load("Resources/sponza/sponza.dae", this, &context, 1.0f);
		m_Meshes.push_back(std::move(pMesh));
	}

	for (uint32 j = 0; j < (uint32)m_Meshes.size(); ++j)
	{
		auto& pMesh = m_Meshes[j];
		for (const SubMeshInstance& node : pMesh->GetMeshInstances())
		{
			const SubMesh& subMesh = pMesh->GetMesh(node.MeshIndex);
			const Material& material = pMesh->GetMaterial(subMesh.MaterialId);
			m_SceneData.Batches.push_back(Batch{});
			Batch& b = m_SceneData.Batches.back();
			b.Index = (int)m_SceneData.Batches.size() - 1;
			b.LocalBounds = subMesh.Bounds;
			b.pMesh = &subMesh;
			b.WorldMatrix = node.Transform;
			b.VertexBufferDescriptor = RegisterBindlessResource(subMesh.pVertexSRV);
			b.IndexBufferDescriptor = RegisterBindlessResource(subMesh.pIndexSRV);

			b.Material.Diffuse = RegisterBindlessResource(material.pDiffuseTexture, GetDefaultTexture(DefaultTexture::Gray2D));
			b.Material.Normal = RegisterBindlessResource(material.pNormalTexture, GetDefaultTexture(DefaultTexture::Normal2D));
			b.Material.RoughnessMetalness = RegisterBindlessResource(material.pRoughnessMetalnessTexture, GetDefaultTexture(DefaultTexture::RoughnessMetalness));
			b.Material.Emissive = RegisterBindlessResource(material.pEmissiveTexture, GetDefaultTexture(DefaultTexture::Black2D));
			b.BlendMode = material.IsTransparent ? Batch::Blending::AlphaMask : Batch::Blending::Opaque;
		}
	}

	{
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
			spotLight.LightTexture = RegisterBindlessResource(m_pLightCookie.get(), GetDefaultTexture(DefaultTexture::White2D));
			spotLight.VolumetricLighting = true;
			m_Lights.push_back(spotLight);
		}
		m_pLightBuffer = std::make_unique<Buffer>(this, "Lights");
		m_pLightBuffer->Create(BufferDesc::CreateStructured((int)m_Lights.size(), sizeof(Light), BufferFlag::ShaderResource));
	}
}

void Graphics::OnResize(int width, int height)
{
	E_LOG(Info, "Viewport resized: %dx%d", width, height);
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_Backbuffers[i]->Release();
	}
	m_pDepthStencil->Release();

	//Resize the buffers
	DXGI_SWAP_CHAIN_DESC1 desc{};
	m_pSwapchain->GetDesc1(&desc);
	VERIFY_HR_EX(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT,
		m_WindowWidth,
		m_WindowHeight,
		desc.Format,
		desc.Flags
	), GetDevice());

	m_CurrentBackBufferIndex = 0;

	//Recreate the render target views
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		ID3D12Resource* pResource = nullptr;
		VERIFY_HR_EX(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)), GetDevice());
		m_Backbuffers[i]->CreateForSwapchain(pResource);
	}
	if (m_SampleCount > 1)
	{
		m_pMultiSampleRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Color(0, 0, 0, 0))));
	}
	m_pNormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Color(0, 0, 0, 0))));
	m_pResolvedNormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, 1, ClearBinding(Color(0, 0, 0, 0))));
	m_pDepthStencil->Create(TextureDesc::CreateDepth(width, height, DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
	m_pResolvedDepthStencil->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pTAASource->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pPreviousColor->Create(TextureDesc::Create2D(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource));
	m_pTonemapTarget->Create(TextureDesc::CreateRenderTarget(width, height, SWAPCHAIN_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pVelocity->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

	m_pAmbientOcclusion->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

	m_pCamera->SetViewport(FloatRect(0, 0, (float)width, (float)height));
	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pTiledForward->OnSwapchainCreated(width, height);
	m_pSSAO->OnSwapchainCreated(width, height);
	m_pRTReflections->OnResize(width, height);

	m_ReductionTargets.clear();
	int w = width;
	int h = height;
	while (w > 1 || h > 1)
	{
		w = Math::DivideAndRoundUp(w, 16);
		h = Math::DivideAndRoundUp(h, 16);
		std::unique_ptr<Texture> pTexture = std::make_unique<Texture>(this, "SDSM Reduction Target");
		pTexture->Create(TextureDesc::Create2D(w, h, DXGI_FORMAT_R32G32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
		m_ReductionTargets.push_back(std::move(pTexture));
	}

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		std::unique_ptr<Buffer> pBuffer = std::make_unique<Buffer>(this, "SDSM Reduction Readback Target");
		pBuffer->Create(BufferDesc::CreateTyped(1, DXGI_FORMAT_R32G32_FLOAT, BufferFlag::Readback));
		pBuffer->Map();
		m_ReductionReadbackTargets.push_back(std::move(pBuffer));
	}
}

void Graphics::InitializePipelines()
{
	//Input layout
	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader* pVertexShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
			Shader* pAlphaClipShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

			//Rootsignature
			m_pShadowsRS = std::make_unique<RootSignature>(this);
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
			m_pShadowsOpaquePSO = CreatePipeline(psoDesc);

			psoDesc.SetPixelShader(pAlphaClipShader);
			psoDesc.SetName("Shadow Mapping Alpha Mask");
			m_pShadowsAlphaMaskPSO = CreatePipeline(psoDesc);
		}
	}

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader* pVertexShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = GetShaderManager()->GetShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>(this);
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", pVertexShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount);
		psoDesc.SetName("Depth Prepass Opaque");
		m_pDepthPrepassOpaquePSO = CreatePipeline(psoDesc);

		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetName("Depth Prepass Alpha Mask");
		m_pDepthPrepassAlphaMaskPSO = CreatePipeline(psoDesc);
	}

	//Luminance Historgram
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>(this);
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Historgram", pComputeShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Luminance Historgram");
		m_pLuminanceHistogramPSO = CreatePipeline(psoDesc);

		m_pLuminanceHistogram = std::make_unique<Buffer>(this, "Luminance Histogram");
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32) * 256));
		m_pAverageLuminance = std::make_unique<Buffer>(this, "Average Luminance");
		m_pAverageLuminance->Create(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource));
	}

	//Debug Draw Histogram
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("DrawLuminanceHistogram.hlsl", ShaderType::Compute, "DrawLuminanceHistogram");
		m_pDrawHistogramRS = std::make_unique<RootSignature>(this);
		m_pDrawHistogramRS->FinalizeFromShader("Draw Luminance Historgram", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDrawHistogramRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Draw Luminance Historgram");
		m_pDrawHistogramPSO = CreatePipeline(psoDesc);
	}

	//Average Luminance
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("AverageLuminance.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>(this);
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance", pComputeShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Average Luminance");
		m_pAverageLuminancePSO = CreatePipeline(psoDesc);
	}

	//Camera motion
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("CameraMotionVectors.hlsl", ShaderType::Compute, "CSMain");

		m_pCameraMotionRS = std::make_unique<RootSignature>(this);
		m_pCameraMotionRS->FinalizeFromShader("Camera Motion", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCameraMotionRS->GetRootSignature());
		psoDesc.SetName("Camera Motion");
		m_pCameraMotionPSO = CreatePipeline(psoDesc);
	}

	//Tonemapping
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("Tonemapping.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pToneMapRS = std::make_unique<RootSignature>(this);
		m_pToneMapRS->FinalizeFromShader("Tonemapping", pComputeShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pToneMapRS->GetRootSignature());
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetName("Tone mapping Pipeline");
		m_pToneMapPSO = CreatePipeline(psoDesc);
	}

	//Depth resolve
	//Resolves a multisampled depth buffer to a normal depth buffer
	//Only required when the sample count > 1
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("ResolveDepth.hlsl", ShaderType::Compute, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>(this);
		m_pResolveDepthRS->FinalizeFromShader("Depth Resolve", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		psoDesc.SetName("Resolve Depth Pipeline");
		m_pResolveDepthPSO = CreatePipeline(psoDesc);
	}

	//Depth reduce
	{
		Shader* pPrepareReduceShader = GetShaderManager()->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { });
		Shader* pPrepareReduceShaderMSAA = GetShaderManager()->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader* pReduceShader = GetShaderManager()->GetShader("ReduceDepth.hlsl", ShaderType::Compute, "ReduceDepth", { });

		m_pReduceDepthRS = std::make_unique<RootSignature>(this);
		m_pReduceDepthRS->FinalizeFromShader("Depth Reduce", pPrepareReduceShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pPrepareReduceShader);
		psoDesc.SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		psoDesc.SetName("Prepare Reduce Depth Pipeline");
		m_pPrepareReduceDepthPSO = CreatePipeline(psoDesc);
		psoDesc.SetComputeShader(pPrepareReduceShaderMSAA);
		psoDesc.SetName("Prepare Reduce Depth Pipeline MSAA");
		m_pPrepareReduceDepthMsaaPSO = CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(pReduceShader);
		psoDesc.SetName("Reduce Depth Pipeline");
		m_pReduceDepthPSO = CreatePipeline(psoDesc);
	}

	//TAA
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("TemporalResolve.hlsl", ShaderType::Compute, "CSMain");
		m_pTemporalResolveRS = std::make_unique<RootSignature>(this);
		m_pTemporalResolveRS->FinalizeFromShader("Temporal Resolve", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pTemporalResolveRS->GetRootSignature());
		psoDesc.SetName("Temporal Resolve");
		m_pTemporalResolvePSO = CreatePipeline(psoDesc);
	}

	//Mip generation
	{
		Shader* pComputeShader = GetShaderManager()->GetShader("GenerateMips.hlsl", ShaderType::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>(this);
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		psoDesc.SetName("Generate Mips");
		m_pGenerateMipsPSO = CreatePipeline(psoDesc);
	}

	//Sky
	{
		Shader* pVertexShader = GetShaderManager()->GetShader("ProceduralSky.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = GetShaderManager()->GetShader("ProceduralSky.hlsl", ShaderType::Pixel, "PSMain");

		//Rootsignature
		m_pSkyboxRS = std::make_unique<RootSignature>(this);
		m_pSkyboxRS->FinalizeFromShader("Skybox", pVertexShader);

		//Pipeline state
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pSkyboxRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = CreatePipeline(psoDesc);
	}
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = Time::DeltaTime();

	if (m_pVisualizeTexture)
	{
		ImGui::Begin("Visualize Texture");
		ImGui::Text("Resolution: %dx%d", m_pVisualizeTexture->GetWidth(), m_pVisualizeTexture->GetHeight());
		Vector2 image((float)m_pVisualizeTexture->GetWidth(), (float)m_pVisualizeTexture->GetHeight());
		Vector2 windowSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		float width = windowSize.x;
		float height = windowSize.x * image.y / image.x;
		if (image.x / windowSize.x < image.y / windowSize.y)
		{
			width = image.x / image.y * windowSize.y;
			height = windowSize.y;
		}
		ImGui::Image(m_pVisualizeTexture, ImVec2(width, height));
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
		ImGui::Combo("Render Path", (int*)&m_RenderPath, [](void* data, int index, const char** outText)
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
	if (ImGui::TreeNodeEx("Descriptor Heaps", ImGuiTreeNodeFlags_DefaultOpen))
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
			std::stringstream str;
			str << usedDescriptors << "/" << totalDescriptors;
			ImGui::ProgressBar((float)usedDescriptors / Math::Max(1u, totalDescriptors), ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Dynamic Upload Memory");
		ImGui::Text("%.2f MB", Math::BytesToMegaBytes * m_pDynamicAllocationManager->GetMemoryUsage());
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ProfileNode* pRootNode = Profiler::Get()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::TreePop();
	}

	ImGui::End();

	static bool showOutputLog = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(300, (float)m_WindowHeight), 0, ImVec2(0, 1));
	ImGui::SetNextWindowSize(ImVec2((float)m_WindowWidth - 300 * 2, 250));
	ImGui::SetNextWindowCollapsed(!showOutputLog);

	showOutputLog = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (showOutputLog)
	{
		for (const Console::LogEntry& entry : Console::GetHistory())
		{
			switch (entry.Type)
			{
			case LogType::VeryVerbose:
			case LogType::Verbose:
			case LogType::Info:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
				ImGui::TextWrapped("[Info] %s", entry.Message.c_str());
				break;
			case LogType::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
				ImGui::TextWrapped("[Warning] %s", entry.Message.c_str());
				break;
			case LogType::Error:
			case LogType::FatalError:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
				ImGui::TextWrapped("[Error] %s", entry.Message.c_str());
				break;
			}
			ImGui::PopStyleColor();
		}
		ImGui::SetScrollHereY(0.0f);
	}
	ImGui::PopStyleVar();
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2((float)m_WindowWidth, 0), 0, ImVec2(1, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("Parameters", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

	ImGui::Text("Sky");
	ImGui::SliderFloat("Sun Orientation", &Tweakables::g_SunOrientation, -Math::PI, Math::PI);
	ImGui::SliderFloat("Sun Inclination", &Tweakables::g_SunInclination, 0, 1);
	ImGui::SliderFloat("Sun Temperature", &Tweakables::g_SunTemperature, 1000, 15000);
	ImGui::SliderFloat("Sun Intensity", &Tweakables::g_SunIntensity, 0, 30);
	
	ImGui::Text("Shadows");
	ImGui::SliderInt("Shadow Cascades", &Tweakables::g_ShadowCascades, 1, 4);
	ImGui::Checkbox("SDSM", &Tweakables::g_SDSM);
	ImGui::Checkbox("Stabilize Cascades", &Tweakables::g_StabilizeCascades);
	ImGui::SliderFloat("PSSM Factor", &Tweakables::g_PSSMFactor, 0, 1);
	ImGui::Checkbox("Visualize Cascades", &Tweakables::g_VisualizeShadowCascades);

	ImGui::Text("Expose/Tonemapping");
	ImGui::SliderFloat("Min Log Luminance", &Tweakables::g_MinLogLuminance, -100, 20);
	ImGui::SliderFloat("Max Log Luminance", &Tweakables::g_MaxLogLuminance, -50, 50);
	ImGui::Checkbox("Draw Exposure Histogram", &Tweakables::g_DrawHistogram);
	ImGui::SliderFloat("White Point", &Tweakables::g_WhitePoint, 0, 20);
	ImGui::Combo("Tonemapper", (int*)&Tweakables::g_ToneMapper, [](void* data, int index, const char** outText)
		{
			if (index == 0)
				*outText = "Reinhard";
			else if (index == 1)
				*outText = "Reinhard Extended";
			else if (index == 2)
				*outText = "ACES Fast";
			else if (index == 3)
				*outText = "Unreal 3";
			else if (index == 4)
				*outText = "Uncharted 2";
			else
				return false;
			return true;
		}, nullptr, 5);

	ImGui::SliderFloat("Tau", &Tweakables::g_Tau, 0, 5);

	ImGui::Text("Misc");
	ImGui::Checkbox("Debug Render Lights", &Tweakables::g_VisualizeLights);
	ImGui::Checkbox("Visualize Light Density", &Tweakables::g_VisualizeLightDensity);
	extern bool g_VisualizeClusters;
	ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);
	ImGui::SliderInt("SSR Samples", &Tweakables::g_SsrSamples, 0, 32);
	ImGui::Checkbox("Object Bounds", &Tweakables::g_RenderObjectBounds);

	if (SupportsRayTracing())
	{
		ImGui::Checkbox("Raytraced AO", &Tweakables::g_RaytracedAO);
		ImGui::Checkbox("Raytraced Reflections", &Tweakables::g_RaytracedReflections);
		ImGui::SliderAngle("TLAS Bounds Threshold", &Tweakables::g_TLASBoundsThreshold, 0, 40);
	}

	ImGui::Checkbox("TAA", &Tweakables::g_TAA);

	ImGui::End();
}

void Graphics::UpdateTLAS(CommandContext& context)
{
	if (SupportsRayTracing())
	{
		ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

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
			m_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

			m_pTLASScratch = std::make_unique<Buffer>(this, "TLAS Scratch");
			m_pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
			m_pTLAS = std::make_unique<Buffer>(this, "TLAS");
			m_pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
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

int Graphics::RegisterBindlessResource(ResourceView* pResourceView, ResourceView* pFallback)
{
	auto it = m_ViewToDescriptorIndex.find(pResourceView);
	if (it != m_ViewToDescriptorIndex.end())
	{
		return it->second;
	}
	if (pResourceView)
	{
		DescriptorHandle handle = m_pPersistentDescriptorHeap->Allocate(1);
		m_pDevice->CopyDescriptorsSimple(1, handle.CpuHandle, pResourceView->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_ViewToDescriptorIndex[pResourceView] = handle.HeapIndex;
		return handle.HeapIndex;
	}
	return pFallback ? RegisterBindlessResource(pFallback) : 0;
}

int Graphics::RegisterBindlessResource(Texture* pTexture, Texture* pFallback /*= nullptr*/)
{
	return RegisterBindlessResource(pTexture ? pTexture->GetSRV() : nullptr, pFallback ? pFallback->GetSRV() : nullptr);
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
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
			ComPtr<ID3D12CommandList> pCommandList;
			ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
			VERIFY_HR(m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf())));
			D3D::SetObjectName(pCommandList.Get(), Sprintf("Pooled Commandlist - %d", m_CommandLists.size()).c_str());
			m_CommandLists.push_back(std::move(pCommandList));
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), type, pAllocator));
			pContext = m_CommandListPool[typeIndex].back().get();
		}
	}
	return pContext;
}

bool Graphics::IsFenceComplete(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	return pQueue->IsFenceComplete(fenceValue);
}

void Graphics::WaitForFence(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void Graphics::FreeCommandList(CommandContext* pCommandList)
{
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	m_FreeCommandLists[(int)pCommandList->GetType()].push(pCommandList);
}

bool Graphics::CheckTypedUAVSupport(DXGI_FORMAT format) const
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS featureData{};
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData)), GetDevice());

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
		return featureData.TypedUAVLoadAdditionalFormats;

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
		if (featureData.TypedUAVLoadAdditionalFormats)
		{
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
			VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)), GetDevice());
			const DWORD mask = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
			return ((formatSupport.Support2 & mask) == mask);
		}
		return false;

	default:
		return false;
	}
}

bool Graphics::UseRenderPasses() const
{
	return m_RenderPassTier >= D3D12_RENDER_PASS_TIER_0;
}

bool Graphics::GetShaderModel(int& major, int& minor) const
{
	bool supported = m_ShaderModelMajor > major || (m_ShaderModelMajor == major && m_ShaderModelMinor >= minor);
	major = m_ShaderModelMajor;
	minor = m_ShaderModelMinor;
	return supported;
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		if (pCommandQueue)
		{
			pCommandQueue->WaitForIdle();
		}
	}
}

ID3D12Resource* Graphics::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue /*= nullptr*/)
{
	ID3D12Resource* pResource;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
	VERIFY_HR_EX(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, initialState, pClearValue, IID_PPV_ARGS(&pResource)), GetDevice());
	return pResource;
}

PipelineState* Graphics::CreatePipeline(const PipelineStateInitializer& psoDesc)
{
	std::unique_ptr<PipelineState> pPipeline = std::make_unique<PipelineState>(this);
	pPipeline->Create(psoDesc);
	m_Pipelines.push_back(std::move(pPipeline));
	return m_Pipelines.back().get();
}

StateObject* Graphics::CreateStateObject(const StateObjectInitializer& stateDesc)
{
	std::unique_ptr<StateObject> pStateObject = std::make_unique<StateObject>(this);
	pStateObject->Create(stateDesc);
	m_StateObjects.push_back(std::move(pStateObject));
	return m_StateObjects.back().get();
}
