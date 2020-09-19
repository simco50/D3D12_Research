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
#include "Graphics/RenderGraph/ResourceAllocator.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/Techniques/ClusteredForward.h"
#include "Graphics/Techniques/TiledForward.h"
#include "Graphics/Techniques/RTAO.h"
#include "Graphics/Techniques/SSAO.h"
#include "Graphics/Techniques/GpuParticles.h"
#include "Core/CommandLine.h"
#include "Content/Image.h"
#include "Core/TaskQueue.h"

#ifndef D3D_VALIDATION
#define D3D_VALIDATION 0
#endif

#ifndef GPU_VALIDATION
#define GPU_VALIDATION 0
#endif

const DXGI_FORMAT Graphics::DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
const DXGI_FORMAT Graphics::DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
const DXGI_FORMAT Graphics::RENDER_TARGET_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
const DXGI_FORMAT Graphics::SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;


bool gDumpRenderGraph = false;
bool g_Screenshot = false;

float g_WhitePoint = 1;
float g_MinLogLuminance = -10;
float g_MaxLogLuminance = 20;
float g_Tau = 2;
bool g_DrawHistogram = false;
int32 g_ToneMapper = 1;

bool g_SDSM = false;
bool g_StabilizeCascades = true;
bool g_VisualizeShadowCascades = false;
int g_ShadowCascades = 4;
float g_PSSMFactor = 1.0f;
bool g_ShowRaytraced = false;
bool g_VisualizeLights = false;
bool g_VisualizeLightDensity = false;

float g_SunInclination = 0.579f;
float g_SunOrientation = -3.055f;
float g_SunTemperature = 5000.0f;

int g_ShadowMapIndex = 0;

bool g_EnableUI = true;

Graphics::Graphics(uint32 width, uint32 height, int sampleCount /*= 1*/)
	: m_SampleCount(sampleCount), m_WindowWidth(width), m_WindowHeight(height)
{
}

Graphics::~Graphics()
{
}

void Graphics::Initialize(HWND window)
{
	m_pWindow = window;

	m_pCamera = std::make_unique<FreeCamera>();
	m_pCamera->SetPosition(Vector3(0, 100, -15));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV4, Math::PIDIV4, 0));
	m_pCamera->SetNearPlane(500.0f);
	m_pCamera->SetFarPlane(10.0f);

	InitD3D();
	InitializePipelines();

	CommandContext* pContext = AllocateCommandContext();
	InitializeAssets(*pContext);
	pContext->Execute(true);

	g_ShowRaytraced = SupportsRayTracing() ? g_ShowRaytraced : false;

	m_DesiredLightCount = 3;
	RandomizeLights(m_DesiredLightCount);

	m_pDynamicAllocationManager->FlushAll();
}

void Graphics::RandomizeLights(int count)
{
	m_Lights.resize(count);

	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	Vector3 Position(-150, 160, -10);
	Vector3 Direction;
	Position.Normalize(Direction);
	m_Lights[0] = Light::Directional(Position, -Direction, 10);
	m_Lights[0].CastShadows = true;

	m_Lights[1] = Light::Point(Vector3(0, 10, 0), 100, 5000, Color(1, 0.2f, 0.2f, 1));
	m_Lights[1].CastShadows = true;

	m_Lights[2] = Light::Spot(Vector3(0, 10, -10), 200, Vector3(0, 0, 1), 90, 70, 5000, Color(1, 0, 0, 1.0f));
	m_Lights[2].CastShadows = true;

	if (m_pLightBuffer->GetDesc().ElementCount != count)
	{
		IdleGPU();
		m_pLightBuffer->Create(BufferDesc::CreateStructured(count, sizeof(Light), BufferFlag::ShaderResource));
	}
}

void Graphics::Update()
{
	PROFILE_BEGIN("Update");
	BeginFrame();
	m_pImGuiRenderer->Update();

	PROFILE_BEGIN("Update Game State");

	m_pCamera->Update();

	if (Input::Instance().IsKeyPressed('O'))
	{
		RandomizeLights(m_DesiredLightCount);
	}
	else if (Input::Instance().IsKeyPressed('U'))
	{
		g_EnableUI = !g_EnableUI;
	}

	std::sort(m_TransparantBatches.begin(), m_TransparantBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist > bDist;
		});

	std::sort(m_OpaqueBatches.begin(), m_OpaqueBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist < bDist;
		});

	float costheta = cosf(g_SunOrientation);
	float sintheta = sinf(g_SunOrientation);
	float cosphi = cosf(g_SunInclination * Math::PIDIV2);
	float sinphi = sinf(g_SunInclination * Math::PIDIV2);
	m_Lights[0].Direction = -Vector3(costheta * cosphi, sinphi, sintheta * cosphi);
	m_Lights[0].Colour = Math::EncodeColor(Math::MakeFromColorTemperature(g_SunTemperature));

	m_Lights[1].Position.x = 50 * sin(Time::TotalTime());

	if (g_VisualizeLights)
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

	shadowData.NumCascades = g_ShadowCascades;

	if (g_SDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % FRAME_COUNT].get();
		Vector2* pData = (Vector2*)pSourceBuffer->Map();
		minPoint = pData->x;
		maxPoint = pData->y;
		pSourceBuffer->Unmap();
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

	for (int i = 0; i < g_ShadowCascades; ++i)
	{
		float p = (i + 1) / (float)g_ShadowCascades;
		float log = minZ * std::pow(maxZ / minZ, p);
		float uniform = minZ + (maxZ - minZ) * p;
		float d = g_PSSMFactor * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
	}

	int shadowIndex = 0;
	for (size_t i = 0; i < m_Lights.size(); ++i)
	{
		Light& light = m_Lights[i];
		if (!light.CastShadows)
		{
			continue;
		}
		light.ShadowIndex = shadowIndex;
		if (light.Type == LightType::Directional)
		{
			for (int i = 0; i < g_ShadowCascades; ++i)
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
				if (g_StabilizeCascades)
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
				if (g_StabilizeCascades)
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
			Matrix projection = Math::CreatePerspectiveMatrix(2 * acos(light.UmbraAngle * Math::ToRadians), 1.0f, light.Range, 1.0f);
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

	if (shadowIndex >= m_ShadowMaps.size())
	{
		m_ShadowMaps.resize(shadowIndex + 1);
		int i = 0;
		for (auto& pShadowMap : m_ShadowMaps)
		{
			pShadowMap = std::make_unique<Texture>(this, "Shadow Map");
			if (i < 4)
				pShadowMap->Create(TextureDesc::CreateDepth(2048, 2048, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
			else
				pShadowMap->Create(TextureDesc::CreateDepth(512, 512, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
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

	if (g_Screenshot && m_ScreenshotDelay < 0)
	{
		RGPassBuilder screenshot = graph.AddPass("Take Screenshot");
		screenshot.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
			{
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
				m_pDevice->GetCopyableFootprints(&m_pTonemapTarget->GetResource()->GetDesc(), 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
				m_pScreenshotBuffer = std::make_unique<Buffer>(this, "Screenshot Texture");
				m_pScreenshotBuffer->Create(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height));
				renderContext.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
				renderContext.InsertResourceBarrier(m_pScreenshotBuffer.get(), D3D12_RESOURCE_STATE_COPY_DEST);
				renderContext.CopyTexture(m_pTonemapTarget.get(), m_pScreenshotBuffer.get(), CD3DX12_BOX(0, 0, m_pTonemapTarget->GetWidth(), m_pTonemapTarget->GetHeight()));
				m_ScreenshotRowPitch = textureFootprint.Footprint.RowPitch;
			});
		m_ScreenshotDelay = 4;
		g_Screenshot = false;
	}

	if (m_pScreenshotBuffer)
	{
		if (m_ScreenshotDelay == 0)
		{
			TaskContext taskContext;
			TaskQueue::Execute([&](uint32) {
				char* pData = (char*)m_pScreenshotBuffer->Map(0, m_pScreenshotBuffer->GetSize());
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
				m_pScreenshotBuffer->Unmap();

				SYSTEMTIME time;
				GetSystemTime(&time);
				char stringTarget[128];
				GetTimeFormat(LOCALE_INVARIANT, TIME_FORCE24HOURFORMAT, &time, "hh_mm_ss", stringTarget, 128);
				std::stringstream filePath;
				filePath << "Screenshot_" << stringTarget << ".jpg";
				img.Save(filePath.str().c_str());
				m_pScreenshotBuffer.reset();
				}, taskContext);
			m_ScreenshotDelay = -1;
		}
		else
		{
			m_ScreenshotDelay--;
		}
	}

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

			renderContext.SetPipelineState(m_pDepthPrepassPSO.get());
			renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

			struct Parameters
			{
				Matrix WorldViewProj;
			} constBuffer;

			for (const Batch& b : m_OpaqueBatches)
			{
				constBuffer.WorldViewProj = b.WorldMatrix * m_pCamera->GetViewProjection();
				renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));
				b.pMesh->Draw(&renderContext);
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
				renderContext.SetPipelineState(m_pResolveDepthPSO.get());

				renderContext.SetDynamicDescriptor(0, 0, resources.GetTexture(Data.DepthStencilResolved)->GetUAV());
				renderContext.SetDynamicDescriptor(1, 0, resources.GetTexture(Data.DepthStencil)->GetSRV());

				int dispatchGroupsX = Math::DivideAndRoundUp(m_WindowWidth, 16);
				int dispatchGroupsY = Math::DivideAndRoundUp(m_WindowHeight, 16);
				renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);

				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencilResolved), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencil), D3D12_RESOURCE_STATE_DEPTH_READ);
				renderContext.FlushResourceBarriers();
			});
	}

	m_pParticles->Simulate(graph, GetResolvedDepthStencil(), *m_pCamera);

	if (g_ShowRaytraced)
	{
		m_pRTAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolvedDepthStencil(), m_pTLAS.get(), *m_pCamera);
	}
	else
	{
		m_pSSAO->Execute(graph, m_pAmbientOcclusion.get(), GetResolvedDepthStencil(), *m_pCamera);
	}

	//SHADOW MAPPING
	// - Renders the scene depth onto a separate depth buffer from the light's view
	if (shadowIndex > 0)
	{
		if (g_SDSM)
		{
			RGPassBuilder depthReduce = graph.AddPass("Depth Reduce");
			Data.DepthStencil = depthReduce.Write(Data.DepthStencil);
			depthReduce.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
				{
					Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					renderContext.InsertResourceBarrier(m_ReductionTargets[0].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					renderContext.SetComputeRootSignature(m_pReduceDepthRS.get());
					renderContext.SetPipelineState(pDepthStencil->GetDesc().SampleCount > 1 ? m_pPrepareReduceDepthMsaaPSO.get() : m_pPrepareReduceDepthPSO.get());

					struct ShaderParameters
					{
						float Near;
						float Far;
					} parameters;
					parameters.Near = m_pCamera->GetNear();
					parameters.Far = m_pCamera->GetFar();

					renderContext.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(ShaderParameters));
					renderContext.SetDynamicDescriptor(1, 0, m_ReductionTargets[0]->GetUAV());
					renderContext.SetDynamicDescriptor(2, 0, pDepthStencil->GetSRV());

					renderContext.Dispatch(m_ReductionTargets[0]->GetWidth(), m_ReductionTargets[0]->GetHeight());

					renderContext.SetPipelineState(m_pReduceDepthPSO.get());
					for (size_t i = 1; i < m_ReductionTargets.size(); ++i)
					{
						renderContext.InsertResourceBarrier(m_ReductionTargets[i - 1].get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						renderContext.InsertResourceBarrier(m_ReductionTargets[i].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

						renderContext.SetDynamicDescriptor(1, 0, m_ReductionTargets[i]->GetUAV());
						renderContext.SetDynamicDescriptor(2, 0, m_ReductionTargets[i - 1]->GetSRV());

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

				for (int i = 0; i < shadowIndex; ++i)
				{
					GPU_PROFILE_SCOPE("Light View", &context);
					Texture* pShadowmap = m_ShadowMaps[i].get();
					context.BeginRenderPass(RenderPassInfo(pShadowmap, RenderPassAccess::Clear_Store));

					struct PerObjectData
					{
						Matrix WorldViewProjection;
					} ObjectData{};

					//Opaque
					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pShadowsOpaquePSO.get());

						for (const Batch& b : m_OpaqueBatches)
						{
							ObjectData.WorldViewProjection = b.WorldMatrix * shadowData.LightViewProjections[i];
							context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
							b.pMesh->Draw(&context);
						}
					}
					//Transparant
					{
						GPU_PROFILE_SCOPE("Transparant", &context);
						context.SetPipelineState(m_pShadowsAlphaPSO.get());

						context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
						for (const Batch& b : m_TransparantBatches)
						{
							ObjectData.WorldViewProjection = b.WorldMatrix * shadowData.LightViewProjections[i];
							context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
							context.SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
							b.pMesh->Draw(&context);
						}
					}
					context.EndRenderPass();
				}
			});
	}

	SceneData sceneData;
	sceneData.pDepthBuffer = GetDepthStencil();
	sceneData.pResolvedDepth = GetResolvedDepthStencil();
	sceneData.pOpaqueBatches = &m_OpaqueBatches;
	sceneData.pTransparantBatches = &m_TransparantBatches;
	sceneData.pRenderTarget = GetCurrentRenderTarget();
	sceneData.pLightBuffer = m_pLightBuffer.get();
	sceneData.pCamera = m_pCamera.get();
	sceneData.pShadowMaps = &m_ShadowMaps;
	sceneData.pShadowData = &shadowData;
	sceneData.pAO = m_pAmbientOcclusion.get();

	if (m_RenderPath == RenderPath::Tiled)
	{
		m_pTiledForward->Execute(graph, sceneData);
	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		m_pClusteredForward->Execute(graph, sceneData);
	}

	m_pParticles->Render(graph, GetCurrentRenderTarget(), GetDepthStencil(), *m_pCamera);

	RGPassBuilder sky = graph.AddPass("Sky");
	Data.DepthStencil = sky.Read(Data.DepthStencil);
	sky.Bind([=](CommandContext& renderContext, const RGPassResources& resources)
		{
			Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
			renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);
			renderContext.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_DontCare);

			renderContext.BeginRenderPass(info);
			renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			renderContext.SetPipelineState(m_pSkyboxPSO.get());
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

			renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));

			renderContext.Draw(0, 36);

			renderContext.EndRenderPass();
		});

	DebugRenderer::Get()->Render(graph, m_pCamera->GetViewProjection(), GetCurrentRenderTarget(), GetDepthStencil());

	//MSAA Render Target Resolve
	// - We have to resolve a MSAA render target ourselves. Unlike D3D11, this is not done automatically by the API.
	//	Luckily, there's a method that does it for us!
	if (m_SampleCount > 1)
	{
		RGPassBuilder resolve = graph.AddPass("Resolve");
		resolve.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
				context.ResolveResource(GetCurrentRenderTarget(), 0, m_pHDRRenderTarget.get(), 0, RENDER_TARGET_FORMAT);
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

					context.SetPipelineState(m_pGenerateMipsPSO.get());
					context.SetComputeRootSignature(m_pGenerateMipsRS.get());

					struct DownscaleParameters
					{
						IntVector2 TargetDimensions;
						Vector2 TargetDimensionsInv;
					} Parameters{};
					Parameters.TargetDimensions.x = pToneMapInput->GetWidth();
					Parameters.TargetDimensions.y = pToneMapInput->GetHeight();
					Parameters.TargetDimensionsInv = Vector2(1.0f / pToneMapInput->GetWidth(), 1.0f / pToneMapInput->GetHeight());

					context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(DownscaleParameters));
					context.SetDynamicDescriptor(1, 0, pToneMapInput->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());

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
				context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.ClearUavUInt(m_pLuminanceHistogram.get(), m_pLuminanceHistogram->GetUAV());

				context.SetPipelineState(m_pLuminanceHistogramPSO.get());
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
				Parameters.MinLogLuminance = g_MinLogLuminance;
				Parameters.OneOverLogLuminanceRange = 1.0f / (g_MaxLogLuminance - g_MinLogLuminance);

				context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(HistogramParameters));
				context.SetDynamicDescriptor(1, 0, m_pLuminanceHistogram->GetUAV());
				context.SetDynamicDescriptor(2, 0, pToneMapInput->GetSRV());

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

				context.SetPipelineState(m_pAverageLuminancePSO.get());
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
				Parameters.MinLogLuminance = g_MinLogLuminance;
				Parameters.LogLuminanceRange = g_MaxLogLuminance - g_MinLogLuminance;
				Parameters.TimeDelta = Time::DeltaTime();
				Parameters.Tau = g_Tau;

				context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(AverageParameters));
				context.SetDynamicDescriptor(1, 0, m_pAverageLuminance->GetUAV());
				context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());

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
				constBuffer.WhitePoint = g_WhitePoint;
				constBuffer.Tonemapper = g_ToneMapper;

				context.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				context.SetPipelineState(m_pToneMapPSO.get());
				context.SetComputeRootSignature(m_pToneMapRS.get());

				context.SetComputeDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));

				context.SetDynamicDescriptor(1, 0, m_pTonemapTarget->GetUAV());
				context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());
				context.SetDynamicDescriptor(2, 1, m_pAverageLuminance->GetSRV());

				context.Dispatch(
					Math::DivideAndRoundUp(m_pHDRRenderTarget->GetWidth(), 16),
					Math::DivideAndRoundUp(m_pHDRRenderTarget->GetHeight(), 16)
				);
			});

		if (g_EnableUI && g_DrawHistogram)
		{
			RGPassBuilder drawHistogram = graph.AddPass("Draw Histogram");
			drawHistogram.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pTonemapTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetPipelineState(m_pDrawHistogramPSO.get());
					context.SetComputeRootSignature(m_pDrawHistogramRS.get());

					struct AverageParameters
					{
						float MinLogLuminance;
						float InverseLogLuminanceRange;
					} Parameters;

					Parameters.MinLogLuminance = g_MinLogLuminance;
					Parameters.InverseLogLuminanceRange = 1.0f / (g_MaxLogLuminance - g_MinLogLuminance);

					context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(AverageParameters));
					context.SetDynamicDescriptor(1, 0, m_pTonemapTarget->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());
					context.SetDynamicDescriptor(2, 1, m_pAverageLuminance->GetSRV());

					context.Dispatch(1, m_pLuminanceHistogram->GetDesc().ElementCount);
				});
		}
	}

	if (g_VisualizeLightDensity)
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
			ImGui::Button(number, ImVec2(40,  20));
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleColor();
		ImGui::End();
	}

	//UI
	// - ImGui render, pretty straight forward
	if(g_EnableUI)
	{
		m_pImGuiRenderer->Render(graph, m_pTonemapTarget.get());
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
	if (gDumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		gDumpRenderGraph = false;
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
	m_pSwapchain->SetFullscreenState(false, nullptr);
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame(m_WindowWidth, m_WindowHeight);
}

void Graphics::EndFrame(uint64 fenceValue)
{
	Profiler::Get()->Resolve(this, m_Frame);

	//This always gets me confused!
	//The 'm_CurrentBackBufferIndex' is the frame that just got queued so we set the fence value on that frame
	//We present and request the new backbuffer index and wait for that one to finish on the GPU before starting to queue work for that frame.

	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	++m_Frame;
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D()");
	UINT dxgiFactoryFlags = 0;

	bool debugD3D = CommandLine::GetBool("d3ddebug") || D3D_VALIDATION;
	bool gpuValidation = CommandLine::GetBool("gpuvalidation") || GPU_VALIDATION;

	if (debugD3D)
	{
		ComPtr<ID3D12Debug> pDebugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController))))
		{
			pDebugController->EnableDebugLayer();
			E_LOG(Info, "D3D12 Debug Layer Enabled");
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
			E_LOG(Info, "DRED Enabled");
		}
	}
	
	//Create the factory
	ComPtr<IDXGIFactory6> pFactory;
	VERIFY_HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory)));

	ComPtr<IDXGIAdapter4> pAdapter;
	uint32 adapterIndex = 0;
	E_LOG(Info, "Adapters:");
	DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
	while (pFactory->EnumAdapterByGpuPreference(adapterIndex++, gpuPreference, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf())) == S_OK)
	{
		DXGI_ADAPTER_DESC3 desc;
		pAdapter->GetDesc3(&desc);
		char name[256];
		ToMultibyte(desc.Description, name, 256);
		E_LOG(Info, "\t%s - %f GB", name, (float)desc.DedicatedVideoMemory * Math::ToGigaBytes);

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
	char name[256];
	ToMultibyte(desc.Description, name, 256);
	E_LOG(Info, "Using %s", name);

	//Create the device
	constexpr D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};
	auto GetFeatureLevelName = [](D3D_FEATURE_LEVEL featureLevel) {
		switch (featureLevel)
		{
		case D3D_FEATURE_LEVEL_12_1: return "D3D_FEATURE_LEVEL_12_1";
		case D3D_FEATURE_LEVEL_12_0: return "D3D_FEATURE_LEVEL_12_1";
		case D3D_FEATURE_LEVEL_11_1: return "D3D_FEATURE_LEVEL_12_1";
		case D3D_FEATURE_LEVEL_11_0: return "D3D_FEATURE_LEVEL_12_1";
		default: noEntry(); return "";
		}
	};

	VERIFY_HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));
	D3D12_FEATURE_DATA_FEATURE_LEVELS caps{};
	caps.pFeatureLevelsRequested = featureLevels;
	caps.NumFeatureLevels = ARRAYSIZE(featureLevels);
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)), GetDevice());
	VERIFY_HR_EX(D3D12CreateDevice(pAdapter.Get(), caps.MaxSupportedFeatureLevel, IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf())), GetDevice());

	pAdapter.Reset();

	E_LOG(Info, "D3D12 Device Created: %s", GetFeatureLevelName(caps.MaxSupportedFeatureLevel));
	m_pDevice.As(&m_pRaytracingDevice);
	D3D::SetObjectName(m_pDevice.Get(), "Main Device");

	if(debugD3D)
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
			}
			pInfoQueue->PushStorageFilter(&NewFilter);
			pInfoQueue->Release();
		}
	}

	//Feature checks
	{	
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupport{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupport, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5))))
		{
			m_RenderPassTier = featureSupport.RenderPassesTier;
			m_RayTracingTier = featureSupport.RaytracingTier;
		}
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelSupport{};
		shaderModelSupport.HighestShaderModel = D3D_SHADER_MODEL_6_6;
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelSupport, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))))
		{
			m_ShaderModelMajor = shaderModelSupport.HighestShaderModel >> 0x4;
			m_ShaderModelMinor = shaderModelSupport.HighestShaderModel & 0xF;
		}
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps7{};
		if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &caps7, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7))))
		{
			m_MeshShaderSupport = caps7.MeshShaderTier;
			m_SamplerFeedbackSupport = caps7.SamplerFeedbackTier;
		}
	}

	//Create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	//m_CommandQueues[D3D12_COMMAND_LIST_TYPE_BUNDLE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_BUNDLE);

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
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;
	ComPtr<IDXGISwapChain1> swapChain;

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

	m_pSwapchain.Reset();
	swapChain.As(&m_pSwapchain);

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	m_pGraphAllocator = std::make_unique<RGResourceAllocator>(this);

	Profiler::Get()->Initialize(this);

	//Create the textures but don't create the resources themselves yet.
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_Backbuffers[i] = std::make_unique<Texture>(this, "Render Target");
	}
	m_pDepthStencil = std::make_unique<Texture>(this, "Depth Stencil");

	if (m_SampleCount > 1)
	{
		m_pResolvedDepthStencil = std::make_unique<Texture>(this, "Resolved Depth Stencil");
		m_pMultiSampleRenderTarget = std::make_unique<Texture>(this, "MSAA Target");
	}
	m_pHDRRenderTarget = std::make_unique<Texture>(this, "HDR Target");
	m_pTonemapTarget = std::make_unique<Texture>(this, "Tonemap Target");
	m_pDownscaledColor = std::make_unique<Texture>(this, "Downscaled HDR Target");
	m_pAmbientOcclusion = std::make_unique<Texture>(this, "SSAO");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pTiledForward = std::make_unique<TiledForward>(this);
	m_pRTAO = std::make_unique<RTAO>(this);
	m_pSSAO = std::make_unique<SSAO>(this);
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));
	m_pParticles = std::make_unique<GpuParticles>(this);

	DebugRenderer::Get()->Initialize(this);

	OnResize(m_WindowWidth, m_WindowHeight);
}


void Graphics::InitializeAssets(CommandContext& context)
{
	m_pMesh = std::make_unique<Mesh>();
	m_pMesh->Load("Resources/sponza/sponza.dae", this, &context);

	for (int i = 0; i < m_pMesh->GetMeshCount(); ++i)
	{
		Batch b;
		b.Bounds = m_pMesh->GetMesh(i)->GetBounds();
		b.pMesh = m_pMesh->GetMesh(i);
		b.pMaterial = &m_pMesh->GetMaterial(b.pMesh->GetMaterialId());
		b.WorldMatrix = Matrix::Identity;
		if (b.pMaterial->IsTransparent)
		{
			m_TransparantBatches.push_back(b);
		}
		else
		{
			m_OpaqueBatches.push_back(b);
		}
	}

	if (SupportsRayTracing())
	{
		ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

		//Bottom Level Acceleration Structure
		{
			std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
			for (size_t i = 0; i < m_pMesh->GetMeshCount(); ++i)
			{
				const SubMesh* pSubMesh = m_pMesh->GetMesh((int)i);
				if (m_pMesh->GetMaterial(pSubMesh->GetMaterialId()).IsTransparent)
				{
					continue;
				}
				D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
				geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
				geometryDesc.Triangles.IndexBuffer = pSubMesh->GetIndicesLocation();
				geometryDesc.Triangles.IndexCount = pSubMesh->GetIndexCount();
				geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexBuffer.StartAddress = pSubMesh->GetVerticesLocation();
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = pSubMesh->GetStride();
				geometryDesc.Triangles.VertexCount = pSubMesh->GetVertexCount();
				geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
				geometries.push_back(geometryDesc);
			}

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
			prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			prebuildInfo.Flags =
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
				| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
			prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			prebuildInfo.NumDescs = (uint32)geometries.size();
			prebuildInfo.pGeometryDescs = geometries.data();

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
			m_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

			m_pBLASScratch = std::make_unique<Buffer>(this, "BLAS Scratch Buffer");
			m_pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));

			m_pBLAS = std::make_unique<Buffer>(this, "BLAS");
			m_pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
			asDesc.Inputs = prebuildInfo;
			asDesc.DestAccelerationStructureData = m_pBLAS->GetGpuHandle();
			asDesc.ScratchAccelerationStructureData = m_pBLASScratch->GetGpuHandle();
			asDesc.SourceAccelerationStructureData = 0;

			pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
			context.InsertUavBarrier(m_pBLAS.get());
			context.FlushResourceBarriers();
		}
		//Top Level Acceleration Structure
		{
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
			prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			prebuildInfo.Flags =
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
				| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
			prebuildInfo.NumDescs = 1;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
			m_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

			m_pTLASScratch = std::make_unique<Buffer>(this, "TLAS Scratch");
			m_pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
			m_pTLAS = std::make_unique<Buffer>(this, "TLAS");
			m_pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

			DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
			D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(allocation.pMappedMemory);
			pInstanceDesc->AccelerationStructure = m_pBLAS->GetGpuHandle();
			pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			pInstanceDesc->InstanceContributionToHitGroupIndex = 0;
			pInstanceDesc->InstanceID = 0;
			pInstanceDesc->InstanceMask = 0xFF;

			//The layout of Transform is a transpose of how affine matrices are typically stored in memory. Instead of four 3-vectors, Transform is laid out as three 4-vectors.
			auto ApplyTransform = [](const Matrix& m, D3D12_RAYTRACING_INSTANCE_DESC& desc)
			{
				desc.Transform[0][0] = m.m[0][0];
				desc.Transform[0][1] = m.m[1][0];
				desc.Transform[0][2] = m.m[2][0];
				desc.Transform[0][3] = m.m[3][0];
				desc.Transform[1][0] = m.m[0][1];
				desc.Transform[1][1] = m.m[1][1];
				desc.Transform[1][2] = m.m[2][1];
				desc.Transform[1][3] = m.m[3][1];
				desc.Transform[2][0] = m.m[0][2];
				desc.Transform[2][1] = m.m[1][2];
				desc.Transform[2][2] = m.m[2][2];
				desc.Transform[2][3] = m.m[3][2];
			};
			ApplyTransform(Matrix::Identity, *pInstanceDesc);

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
			asDesc.DestAccelerationStructureData = m_pTLAS->GetGpuHandle();
			asDesc.ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle();
			asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			asDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
			asDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			asDesc.Inputs.InstanceDescs = allocation.GpuHandle;
			asDesc.Inputs.NumDescs = 1;
			asDesc.SourceAccelerationStructureData = 0;

			pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
			context.InsertUavBarrier(m_pTLAS.get());
		}
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
	VERIFY_HR_EX(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT, 
		m_WindowWidth, 
		m_WindowHeight, 
		SWAPCHAIN_FORMAT,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH), GetDevice());

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
		m_pDepthStencil->Create(TextureDesc::CreateDepth(width, height, DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
		m_pResolvedDepthStencil->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
		m_pMultiSampleRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::RenderTarget, m_SampleCount, ClearBinding(Color(0, 0, 0, 0))));
	}
	else
	{
		m_pDepthStencil->Create(TextureDesc::CreateDepth(width, height, DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
	}
	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pTonemapTarget->Create(TextureDesc::CreateRenderTarget(width, height, SWAPCHAIN_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget | TextureFlag::UnorderedAccess));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

	m_pAmbientOcclusion->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

	m_pCamera->SetAspectRatio((float)width / height);
	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pTiledForward->OnSwapchainCreated(width, height);
	m_pSSAO->OnSwapchainCreated(width, height);

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
		m_ReductionReadbackTargets.push_back(std::move(pBuffer));
	}
}

void Graphics::InitializePipelines()
{
	m_pLightBuffer = std::make_unique<Buffer>(this, "Lights");

	//Input layout
	//UNIVERSAL
	CD3DX12_INPUT_ELEMENT_DESC depthOnlyInputElements[] = {
		CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
		CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
	};

	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader vertexShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");
			Shader alphaPixelShader("DepthOnly.hlsl", ShaderType::Pixel, "PSMain");

			//Rootsignature
			m_pShadowsRS = std::make_unique<RootSignature>();
			m_pShadowsRS->FinalizeFromShader("Shadow Mapping (Opaque)", vertexShader, m_pDevice.Get());

			//Pipeline state
			m_pShadowsOpaquePSO = std::make_unique<PipelineState>();
			m_pShadowsOpaquePSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
			m_pShadowsOpaquePSO->SetRootSignature(m_pShadowsRS->GetRootSignature());
			m_pShadowsOpaquePSO->SetVertexShader(vertexShader);
			m_pShadowsOpaquePSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1);
			m_pShadowsOpaquePSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsOpaquePSO->SetDepthBias(-1, -5.0f, -4.0f);
			m_pShadowsOpaquePSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());

			m_pShadowsAlphaPSO = std::make_unique<PipelineState>(*m_pShadowsOpaquePSO);
			m_pShadowsAlphaPSO->SetPixelShader(alphaPixelShader);
			m_pShadowsAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		}
	}

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("DepthOnly.hlsl", ShaderType::Vertex, "VSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pDepthPrepassPSO = std::make_unique<PipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	//Luminance Historgram
	{
		Shader computeShader("LuminanceHistogram.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>();
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Historgram", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pLuminanceHistogramPSO = std::make_unique<PipelineState>();
		m_pLuminanceHistogramPSO->SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		m_pLuminanceHistogramPSO->SetComputeShader(computeShader);
		m_pLuminanceHistogramPSO->Finalize("Luminance Historgram", m_pDevice.Get());

		m_pLuminanceHistogram = std::make_unique<Buffer>(this, "Luminance Histogram");
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32) * 256));
		m_pAverageLuminance = std::make_unique<Buffer>(this, "Average Luminance");
		m_pAverageLuminance->Create(BufferDesc::CreateStructured(3, sizeof(float), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource));
	}

	//Debug Draw Histogram
	{
		Shader computeDrawShader("DrawLuminanceHistogram.hlsl", ShaderType::Compute, "DrawLuminanceHistogram");
		m_pDrawHistogramRS = std::make_unique<RootSignature>();
		m_pDrawHistogramRS->FinalizeFromShader("Draw Luminance Historgram", computeDrawShader, m_pDevice.Get());

		m_pDrawHistogramPSO = std::make_unique<PipelineState>();
		m_pDrawHistogramPSO->SetRootSignature(m_pDrawHistogramRS->GetRootSignature());
		m_pDrawHistogramPSO->SetComputeShader(computeDrawShader);
		m_pDrawHistogramPSO->Finalize("Draw Luminance Historgram", m_pDevice.Get());
	}

	//Average Luminance
	{
		Shader computeShader("AverageLuminance.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>();
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pAverageLuminancePSO = std::make_unique<PipelineState>();
		m_pAverageLuminancePSO->SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		m_pAverageLuminancePSO->SetComputeShader(computeShader);
		m_pAverageLuminancePSO->Finalize("Average Luminance", m_pDevice.Get());
	}

	//Tonemapping
	{
		Shader computeShader("Tonemapping.hlsl", ShaderType::Compute, "CSMain");

		//Rootsignature
		m_pToneMapRS = std::make_unique<RootSignature>();
		m_pToneMapRS->FinalizeFromShader("Tonemapping", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pToneMapPSO = std::make_unique<PipelineState>();
		m_pToneMapPSO->SetRootSignature(m_pToneMapRS->GetRootSignature());
		m_pToneMapPSO->SetComputeShader(computeShader);
		m_pToneMapPSO->Finalize("Tone mapping Pipeline", m_pDevice.Get());
	}

	//Depth resolve
	//Resolves a multisampled depth buffer to a normal depth buffer
	//Only required when the sample count > 1
	{
		Shader computeShader("ResolveDepth.hlsl", ShaderType::Compute, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->FinalizeFromShader("Depth Resolve", computeShader, m_pDevice.Get());

		m_pResolveDepthPSO = std::make_unique<PipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader);
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	//Depth reduce
	{
		Shader prepareReduceShader("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { });
		Shader prepareReduceShaderMSAA("ReduceDepth.hlsl", ShaderType::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader reduceShader("ReduceDepth.hlsl", ShaderType::Compute, "ReduceDepth", { });

		m_pReduceDepthRS = std::make_unique<RootSignature>();
		m_pReduceDepthRS->FinalizeFromShader("Depth Reduce", prepareReduceShader, m_pDevice.Get());

		m_pPrepareReduceDepthPSO = std::make_unique<PipelineState>();
		m_pPrepareReduceDepthPSO->SetComputeShader(prepareReduceShader);
		m_pPrepareReduceDepthPSO->SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		m_pPrepareReduceDepthPSO->Finalize("Prepare Reduce Depth Pipeline", m_pDevice.Get());
		m_pPrepareReduceDepthMsaaPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pPrepareReduceDepthMsaaPSO->SetComputeShader(prepareReduceShaderMSAA);
		m_pPrepareReduceDepthMsaaPSO->Finalize("Prepare Reduce Depth Pipeline MSAA", m_pDevice.Get());

		m_pReduceDepthPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pReduceDepthPSO->SetComputeShader(reduceShader);
		m_pReduceDepthPSO->Finalize("Reduce Depth Pipeline", m_pDevice.Get());
	}

	//Mip generation
	{
		Shader computeShader("GenerateMips.hlsl", ShaderType::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>();
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips", computeShader, m_pDevice.Get());

		m_pGenerateMipsPSO = std::make_unique<PipelineState>();
		m_pGenerateMipsPSO->SetComputeShader(computeShader);
		m_pGenerateMipsPSO->SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		m_pGenerateMipsPSO->Finalize("Generate Mips PSO", m_pDevice.Get());
	}

	//Sky
	{
		Shader vertexShader("ProceduralSky.hlsl", ShaderType::Vertex, "VSMain");
		Shader pixelShader("ProceduralSky.hlsl", ShaderType::Pixel, "PSMain");

		//Rootsignature
		m_pSkyboxRS = std::make_unique<RootSignature>();
		m_pSkyboxRS->FinalizeFromShader("Skybox", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pSkyboxPSO = std::make_unique<PipelineState>();
		m_pSkyboxPSO->SetInputLayout(nullptr, 0);
		m_pSkyboxPSO->SetRootSignature(m_pSkyboxRS->GetRootSignature());
		m_pSkyboxPSO->SetVertexShader(vertexShader);
		m_pSkyboxPSO->SetPixelShader(pixelShader);
		m_pSkyboxPSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount);
		m_pSkyboxPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pSkyboxPSO->Finalize("Skybox", m_pDevice.Get());
	}
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = Time::DeltaTime();


	if(m_pVisualizeTexture)
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

	if (g_VisualizeShadowCascades)
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

	if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Combo("Render Path", (int*)& m_RenderPath, [](void* data, int index, const char** outText)
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
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 10, 10000);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
		}

		if (ImGui::Button("Dump RenderGraph"))
		{
			gDumpRenderGraph = true;
		}
		if (ImGui::Button("Screenshot"))
		{
			g_Screenshot = true;
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
			ImGui::ProgressBar((float)usedDescriptors / totalDescriptors, ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Dynamic Upload Memory");
		ImGui::Text("%.2f MB", Math::ToMegaBytes* m_pDynamicAllocationManager->GetMemoryUsage());
		ImGui::TreePop();
	}
	ImGui::End();

	static bool showOutputLog = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(300, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
	ImGui::SetNextWindowSize(ImVec2(showOutputLog ? (float)(m_WindowWidth - 300) * 0.5f : m_WindowWidth - 250, 250));
	ImGui::SetNextWindowCollapsed(!showOutputLog);

	showOutputLog = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (showOutputLog)
	{
		ImGui::SetScrollHereY(1.0f);
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
	}
	ImGui::End();

	if (showOutputLog)
	{
		ImGui::SetNextWindowPos(ImVec2(300 + (m_WindowWidth - 300) / 2.0f, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
		ImGui::SetNextWindowSize(ImVec2((float)(m_WindowWidth - 300) * 0.5f, 250));
		ImGui::SetNextWindowCollapsed(!showOutputLog);
		ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ProfileNode* pRootNode = Profiler::Get()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::End();
	}
	ImGui::PopStyleVar();

	ImGui::Begin("Parameters");

	ImGui::Text("Sky");
	ImGui::SliderFloat("Sun Orientation", &g_SunOrientation, -Math::PI, Math::PI);
	ImGui::SliderFloat("Sun Inclination", &g_SunInclination, 0, 1);
	ImGui::SliderFloat("Sun Temperature", &g_SunTemperature, 1000, 15000);

	ImGui::Text("Shadows");
	ImGui::SliderInt("Shadow Cascades", &g_ShadowCascades, 1, 4);
	ImGui::Checkbox("SDSM", &g_SDSM);
	ImGui::Checkbox("Stabilize Cascades", &g_StabilizeCascades);
	ImGui::SliderFloat("PSSM Factor", &g_PSSMFactor, 0, 1);
	ImGui::Checkbox("Visualize Cascades", &g_VisualizeShadowCascades);

	ImGui::Text("Expose/Tonemapping");
	ImGui::SliderFloat("Min Log Luminance", &g_MinLogLuminance, -100, 20);
	ImGui::SliderFloat("Max Log Luminance", &g_MaxLogLuminance, -50, 50);
	ImGui::Checkbox("Draw Exposure Histogram", &g_DrawHistogram);
	ImGui::SliderFloat("White Point", &g_WhitePoint, 0, 20);
	ImGui::Combo("Tonemapper", (int*)&g_ToneMapper, [](void* data, int index, const char** outText)
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

	ImGui::SliderFloat("Tau", &g_Tau, 0, 5);

	ImGui::Text("Misc");
	ImGui::Checkbox("Debug Render Lights", &g_VisualizeLights);
	ImGui::Checkbox("Visualize Light Density", &g_VisualizeLightDensity);
	extern bool g_VisualizeClusters;
	ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);

	if (ImGui::Checkbox("Raytracing", &g_ShowRaytraced))
	{
		if (m_RayTracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		{
			g_ShowRaytraced = false;
		}
	}

	ImGui::End();
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
		}
		else
		{
			ComPtr<ID3D12CommandList> pCommandList;
			ComPtr<ID3D12Device4> pDevice4;
			VERIFY_HR(m_pDevice.As(&pDevice4));
			pDevice4->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(pCommandList.GetAddressOf()));
			D3D::SetObjectName(pCommandList.Get(), "Pooled Commandlist");
			m_CommandLists.push_back(std::move(pCommandList));
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), type));
			pContext = m_CommandListPool[typeIndex].back().get();
		}
	}
	pContext->Reset();
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
	return m_RenderPassTier > D3D12_RENDER_PASS_TIER_0;
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

uint32 Graphics::GetMaxMSAAQuality(uint32 msaa, DXGI_FORMAT format)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = format == DXGI_FORMAT_UNKNOWN ? RENDER_TARGET_FORMAT : format;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = msaa;
	VERIFY_HR_EX(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)), GetDevice());
	return qualityLevels.NumQualityLevels - 1;
}

ID3D12Resource* Graphics::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue /*= nullptr*/)
{
	ID3D12Resource* pResource;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
	VERIFY_HR_EX(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, initialState, pClearValue, IID_PPV_ARGS(&pResource)), GetDevice());
	return pResource;
}