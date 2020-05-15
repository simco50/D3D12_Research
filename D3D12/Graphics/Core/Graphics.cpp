#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
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
#include "Graphics/ClusteredForward.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/Blackboard.h"
#include "Graphics/RenderGraph/ResourceAllocator.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/TiledForward.h"
#include "Graphics/RTAO.h"
#include "Graphics/SSAO.h"
#include "Graphics/ImGuiRenderer.h"
#include "../GpuParticles.h"

#ifdef _DEBUG
#define D3D_VALIDATION 1
#endif

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

float g_WhitePoint = 4;
float g_MinLogLuminance = -10;
float g_MaxLogLuminance = 2;
float g_Tau = 10;

bool g_SDSM = false;
bool g_StabilizeCascades = true;
float g_PSSMFactor = 1.0f;
bool g_ShowRaytraced = false;
bool g_VisualizeLights = false;

float g_SunOrientation = 0;
float g_SunInclination = 0.2f;

Graphics::Graphics(uint32 width, uint32 height, int sampleCount /*= 1*/)
	: m_WindowWidth(width), m_WindowHeight(height), m_SampleCount(sampleCount)
{

}

Graphics::~Graphics()
{
}

void Graphics::Initialize(HWND window)
{
	m_pWindow = window;

	m_pCamera = std::make_unique<FreeCamera>(this);
	m_pCamera->SetPosition(Vector3(0, 100, -15));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PIDIV4, Math::PIDIV4, 0));
	m_pCamera->SetNearPlane(500.0f);
	m_pCamera->SetFarPlane(10.0f);
	m_pCamera->SetViewport(0, 0, 1, 1);

	InitD3D();
	InitializeAssets();
	g_ShowRaytraced = SupportsRayTracing() ? g_ShowRaytraced : false;

	RandomizeLights(m_DesiredLightCount);
}

void Graphics::RandomizeLights(int count)
{
	m_Lights.resize(count);

	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	int lightIndex = 0;
	Vector3 Position(-150, 160, -10);
	Vector3 Direction;
	Position.Normalize(Direction);
	m_Lights[lightIndex] = Light::Directional(Position, -Direction, 5.0f);
	m_Lights[lightIndex].ShadowIndex = 0;

	int randomLightsStartIndex = lightIndex + 1;

	for (int i = randomLightsStartIndex; i < m_Lights.size(); ++i)
	{
		Vector3 c = Vector3(Math::RandomRange(0.6f, 1.0f), Math::RandomRange(0.6f, 1.0f), Math::RandomRange(0.6f, 1.0f));
		Vector4 color(c.x, c.y, c.z, 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(40.0f, 60.0f);
		const float angle = Math::RandomRange(60.0f, 120.0f);
		const float intensity = Math::RandomRange(250.0f, 270.0f);

		Light::Type type = rand() % 2 == 0 ? Light::Type::Point : Light::Type::Spot;
		switch (type)
		{
		case Light::Type::Point:
			m_Lights[i] = Light::Point(position, range, intensity, color);
			break;
		case Light::Type::Spot:
			m_Lights[i] = Light::Spot(position, range, Math::RandVector(), angle, angle - Math::RandomRange(0.0f, angle / 2), intensity, color);
			break;
		case Light::Type::Directional:
		case Light::Type::MAX:
		default:
			assert(false);
			break;
		}
	}

	//It's a bit weird but I don't sort the lights that I manually created because I access them by their original index during the update function
	std::sort(m_Lights.begin() + randomLightsStartIndex, m_Lights.end(), [](const Light& a, const Light& b) { return (int)a.LightType < (int)b.LightType; });

	IdleGPU();
	if (m_pLightBuffer->GetDesc().ElementCount != m_Lights.size())
	{
		m_pLightBuffer->Create(BufferDesc::CreateStructured((int)m_Lights.size(), sizeof(Light)));
	}
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pLightBuffer->SetData(pContext, m_Lights.data(), sizeof(Light) * m_Lights.size());
	pContext->Execute(true);
}

void Graphics::Update()
{
	BeginFrame();
	m_pImGuiRenderer->Update();

	PIX_CAPTURE_SCOPE();
	PROFILE_BEGIN("Update Game State");

	m_pCamera->Update();

	if (Input::Instance().IsKeyPressed('O'))
	{
		RandomizeLights(m_DesiredLightCount);
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

	if (g_VisualizeLights)
	{
		for (const Light& light : m_Lights)
		{
			DebugRenderer::Instance().AddLight(light);
		}
	}

	// SHADOW MAP PARTITIONING
	/////////////////////////////////////////

	m_ShadowCasters = 0;
	ShadowData lightData;

	uint32 numCascades = 4;
	float minPoint = 0;
	float maxPoint = 1;

	if (g_SDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % FRAME_COUNT].get();
		float* pData = (float*)pSourceBuffer->Map();
		minPoint = pData[0];
		maxPoint = pData[1];
		pSourceBuffer->Unmap();
	}

	float nearPlane = m_pCamera->GetFar();
	float farPlane = m_pCamera->GetNear();
	float clipPlaneRange = farPlane - nearPlane;

	float minZ = nearPlane + minPoint * clipPlaneRange;
	float maxZ = nearPlane + maxPoint * clipPlaneRange;

	constexpr uint32 MAX_CASCADES = 4;
	std::array<float, MAX_CASCADES> cascadeSplits;

	for (uint32 i = 0; i < numCascades; ++i)
	{
		float p = (i + 1) / (float)numCascades;
		float log = minZ * std::pow(maxZ / minZ, p);
		float uniform = minZ + (maxZ - minZ) * p;
		float d = g_PSSMFactor * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
	}

	for (uint32 i = 0; i < numCascades; ++i)
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
			Matrix lightView = XMMatrixLookToLH(center, m_Lights[0].Direction, Vector3::Up);
			for (const Vector3& corner : frustumCorners)
			{
				Vector3 p = Vector3::Transform(corner, lightView);
				minExtents = Vector3::Min(minExtents, p);
				maxExtents = Vector3::Max(maxExtents, p);
			}
		}

		Matrix shadowView = XMMatrixLookToLH(center + m_Lights[0].Direction * -400, m_Lights[0].Direction, Vector3::Up);

		Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z + 400, 0);
		Matrix lightViewProjection = shadowView * projectionMatrix;

		//Snap projection to shadowmap texels to avoid flickering edges
		if (g_StabilizeCascades)
		{
			float shadowMapSize = m_pShadowMap->GetHeight() / 2.0f;
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

		lightData.LightViewProjections[i] = lightViewProjection;
		lightData.CascadeDepths[i] = currentCascadeSplit * (farPlane - nearPlane) + nearPlane;
		lightData.ShadowMapOffsets[i] = Vector4((float)(m_ShadowCasters % 2) / 2, (float)(m_ShadowCasters / 2) / 2, 0.5f, 0.5f);
		m_ShadowCasters++;
	}

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////

	PROFILE_END();

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

	graph.AddPass("Simulate Particles", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				m_pParticles->Simulate(context);
			};
		});

	//DEPTH PREPASS
	// - Depth only pass that renders the entire scene
	// - Optimization that prevents wasteful lighting calculations during the base pass
	// - Required for light culling
	graph.AddPass("Depth Prepass", [&](RGPassBuilder& builder)
		{
			Data.DepthStencil = builder.Write(Data.DepthStencil);

			return [=](CommandContext& renderContext, const RGPassResources& resources)
			{
				Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
				const TextureDesc& desc = pDepthStencil->GetDesc();
				renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

				RenderPassInfo info = RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store);

				renderContext.BeginRenderPass(info);
				renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

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
			};
		});

	//NORMALS
	graph.AddPass("Normals", [&](RGPassBuilder& builder)
		{
			Data.DepthStencil = builder.Write(Data.DepthStencil);

			return [=](CommandContext& renderContext, const RGPassResources& resources)
			{
				Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
				const TextureDesc& desc = pDepthStencil->GetDesc();
				renderContext.InsertResourceBarrier(m_pNormals.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

				RenderPassInfo info = RenderPassInfo(m_pNormals.get(), RenderPassAccess::Clear_Store, pDepthStencil, RenderPassAccess::Load_DontCare);

				renderContext.BeginRenderPass(info);
				renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

				renderContext.SetPipelineState(m_pNormalsPSO.get());
				renderContext.SetGraphicsRootSignature(m_pNormalsRS.get());

				struct Parameters
				{
					Matrix World;
					Matrix WorldViewProj;
				} constBuffer;

				for (const Batch& b : m_OpaqueBatches)
				{
					constBuffer.World = b.WorldMatrix;
					constBuffer.WorldViewProj = constBuffer.World * m_pCamera->GetViewProjection();
					renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));
					renderContext.SetDynamicDescriptor(1, 0, b.pMaterial->pNormalTexture->GetSRV());
					b.pMesh->Draw(&renderContext);
				}
				renderContext.EndRenderPass();

				if (m_SampleCount > 1)
				{
					renderContext.InsertResourceBarrier(m_pResolvedNormals.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
					renderContext.InsertResourceBarrier(m_pNormals.get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
					renderContext.ResolveResource(m_pNormals.get(), 0, m_pResolvedNormals.get(), 0, m_pResolvedNormals->GetFormat());
				}
			};
		});

	//[WITH MSAA] DEPTH RESOLVE
	// - If MSAA is enabled, run a compute shader to resolve the depth buffer
	if (m_SampleCount > 1)
	{
		graph.AddPass("Depth Resolve", [&](RGPassBuilder& builder)
			{
				Data.DepthStencil = builder.Read(Data.DepthStencil);
				Data.DepthStencilResolved = builder.Write(Data.DepthStencilResolved);

				return [=](CommandContext& renderContext, const RGPassResources& resources)
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
				};
			});
	}

	if (g_ShowRaytraced)
	{
		RtaoInputResources rtResources{};
		rtResources.pCamera = m_pCamera.get();
		rtResources.pRenderTarget = m_pAmbientOcclusion.get();
		rtResources.pNormalsTexture = GetResolvedNormals();
		rtResources.pDepthTexture = GetResolvedDepthStencil();
		m_pRTAO->Execute(graph, rtResources);
	}
	else
	{
		SsaoInputResources ssaoResources{};
		ssaoResources.pCamera = m_pCamera.get();
		ssaoResources.pRenderTarget = m_pAmbientOcclusion.get();
		ssaoResources.pNormalsTexture = GetResolvedNormals();
		ssaoResources.pDepthTexture = GetResolvedDepthStencil();
		m_pSSAO->Execute(graph, ssaoResources);
	}

	//SHADOW MAPPING
	// - Renders the scene depth onto a separate depth buffer from the light's view
	if (m_ShadowCasters > 0)
	{
		if (g_SDSM)
		{
			graph.AddPass("Depth Reduce", [&](RGPassBuilder& builder)
				{
					Data.DepthStencil = builder.Write(Data.DepthStencil);

					return [=](CommandContext& renderContext, const RGPassResources& resources)
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

						renderContext.Dispatch(m_ReductionTargets[0]->GetWidth(), m_ReductionTargets[0]->GetHeight(), 1);

						renderContext.SetPipelineState(m_pReduceDepthPSO.get());
						for (size_t i = 1; i < m_ReductionTargets.size(); ++i)
						{
							renderContext.InsertResourceBarrier(m_ReductionTargets[i - 1].get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
							renderContext.InsertResourceBarrier(m_ReductionTargets[i].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

							renderContext.SetDynamicDescriptor(1, 0, m_ReductionTargets[i]->GetUAV());
							renderContext.SetDynamicDescriptor(2, 0, m_ReductionTargets[i - 1]->GetSRV());

							renderContext.Dispatch(m_ReductionTargets[i]->GetWidth(), m_ReductionTargets[i]->GetHeight(), 1);
						}

						renderContext.InsertResourceBarrier(m_ReductionTargets.back().get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
						renderContext.FlushResourceBarriers();

						D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
						bufferFootprint.Footprint.Width = 1;
						bufferFootprint.Footprint.Height = 1;
						bufferFootprint.Footprint.Depth = 1;
						bufferFootprint.Footprint.RowPitch = Math::AlignUp<int>(sizeof(Vector2), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
						bufferFootprint.Footprint.Format = DXGI_FORMAT_R32G32_FLOAT;
						bufferFootprint.Offset = 0;

						CD3DX12_TEXTURE_COPY_LOCATION srcLocation(m_ReductionTargets.back()->GetResource(), 0);
						CD3DX12_TEXTURE_COPY_LOCATION dstLocation(m_ReductionReadbackTargets[m_Frame % FRAME_COUNT]->GetResource(), bufferFootprint);
						renderContext.GetCommandList()->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
					};
				});
		}

		graph.AddPass("Shadow Mapping", [&](RGPassBuilder& builder)
			{
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

					context.BeginRenderPass(RenderPassInfo(m_pShadowMap.get(), RenderPassAccess::Clear_Store));
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					context.SetGraphicsRootSignature(m_pShadowsRS.get());

					for (int i = 0; i < m_ShadowCasters; ++i)
					{
						GPU_PROFILE_SCOPE("Light View", &context);
						const Vector4& shadowOffset = lightData.ShadowMapOffsets[i];
						FloatRect viewport;
						viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
						viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
						viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
						viewport.Bottom = viewport.Top + shadowOffset.w * (float)m_pShadowMap->GetHeight();
						context.SetViewport(viewport);

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
								ObjectData.WorldViewProjection = b.WorldMatrix * lightData.LightViewProjections[i];
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
								ObjectData.WorldViewProjection = b.WorldMatrix * lightData.LightViewProjections[i];
								context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
								context.SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
								b.pMesh->Draw(&context);
							}
						}
					}
					context.EndRenderPass();
				};
			});
	}

	if (m_RenderPath == RenderPath::Tiled)
	{
		TiledForwardInputResources resources;
		resources.ResolvedDepthBuffer = Data.DepthStencilResolved;
		resources.DepthBuffer = Data.DepthStencil;
		resources.pOpaqueBatches = &m_OpaqueBatches;
		resources.pTransparantBatches = &m_TransparantBatches;
		resources.pRenderTarget = GetCurrentRenderTarget();
		resources.pLightBuffer = m_pLightBuffer.get();
		resources.pCamera = m_pCamera.get();
		resources.pShadowMap = m_pShadowMap.get();
		resources.pShadowData = &lightData;
		m_pTiledForward->Execute(graph, resources);
	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		ClusteredForwardInputResources resources;
		resources.DepthBuffer = Data.DepthStencil;
		resources.pOpaqueBatches = &m_OpaqueBatches;
		resources.pTransparantBatches = &m_TransparantBatches;
		resources.pRenderTarget = GetCurrentRenderTarget();
		resources.pLightBuffer = m_pLightBuffer.get();
		resources.pCamera = m_pCamera.get();
		resources.pAO = m_pAmbientOcclusion.get();
		resources.pShadowMap = m_pShadowMap.get();
		resources.pShadowData = &lightData;
		m_pClusteredForward->Execute(graph, resources);
	}

	graph.AddPass("Draw Particles", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				m_pParticles->Render(context);
			};
		});

	graph.AddPass("Sky", [&](RGPassBuilder& builder)
		{
			Data.DepthStencil = builder.Read(Data.DepthStencil);

			return [=](CommandContext& renderContext, const RGPassResources& resources)
			{
				Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
				const TextureDesc& desc = pDepthStencil->GetDesc();
				renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_READ);

				RenderPassInfo info = RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Load_Store, pDepthStencil, RenderPassAccess::Load_DontCare);

				renderContext.BeginRenderPass(info);
				renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

				renderContext.SetPipelineState(m_pSkyboxPSO.get());
				renderContext.SetGraphicsRootSignature(m_pSkyboxRS.get());

				float costheta = cosf(g_SunOrientation);
				float sintheta = sinf(g_SunOrientation);
				float cosphi = cosf(g_SunInclination * Math::PIDIV2);
				float sinphi = sinf(g_SunInclination * Math::PIDIV2);

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
				constBuffer.SunDirection = Vector3(costheta * cosphi, sinphi, sintheta * cosphi);
				constBuffer.SunDirection.Normalize();

				renderContext.SetDynamicConstantBufferView(0, &constBuffer, sizeof(Parameters));

				renderContext.Draw(0, 36);

				renderContext.EndRenderPass();
			};
		});

	DebugRenderer::Instance().Render(graph);

	//MSAA Render Target Resolve
	// - We have to resolve a MSAA render target ourselves. Unlike D3D11, this is not done automatically by the API.
	//	Luckily, there's a method that does it for us!
	if (m_SampleCount > 1)
	{
		graph.AddPass("Resolve", [&](RGPassBuilder& builder)
			{
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
					context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
					context.ResolveResource(GetCurrentRenderTarget(), 0, m_pHDRRenderTarget.get(), 0, RENDER_TARGET_FORMAT);
				};
			});
	}

	//Tonemapping
	{
		bool downscaleTonemapInput = true;
		Texture* pToneMapInput = downscaleTonemapInput ? m_pDownscaledColor.get() : m_pHDRRenderTarget.get();
		RGResourceHandle toneMappingInput = graph.ImportTexture("Tonemap Input", pToneMapInput);

		if (downscaleTonemapInput)
		{
			graph.AddPass("Downsample Color", [&](RGPassBuilder& builder)
				{
					toneMappingInput = builder.Write(toneMappingInput);
					return [=](CommandContext& context, const RGPassResources& resources)
					{
						Texture* pToneMapInput = resources.GetTexture(toneMappingInput);
						context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

						context.SetPipelineState(m_pGenerateMipsPSO.get());
						context.SetComputeRootSignature(m_pGenerateMipsRS.get());

						struct DownscaleParameters
						{
							uint32 TargetDimensions[2];
						} Parameters{};
						Parameters.TargetDimensions[0] = pToneMapInput->GetWidth();
						Parameters.TargetDimensions[1] = pToneMapInput->GetHeight();

						context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(DownscaleParameters));
						context.SetDynamicDescriptor(1, 0, pToneMapInput->GetUAV());
						context.SetDynamicDescriptor(2, 0, m_pHDRRenderTarget->GetSRV());

						context.Dispatch(
							Math::DivideAndRoundUp(Parameters.TargetDimensions[0], 16), 
							Math::DivideAndRoundUp(Parameters.TargetDimensions[1], 16)
						);
					};
				});
		}

		graph.AddPass("Luminance Histogram", [&](RGPassBuilder& builder)
			{
				toneMappingInput = builder.Read(toneMappingInput);
				return [=](CommandContext& context, const RGPassResources& resources)
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
				};
			});

		graph.AddPass("Average Luminance", [&](RGPassBuilder& builder)
			{
				return [=](CommandContext& context, const RGPassResources& resources)
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
					Parameters.TimeDelta = GameTimer::DeltaTime();
					Parameters.Tau = g_Tau;

					context.SetComputeDynamicConstantBufferView(0, &Parameters, sizeof(AverageParameters));
					context.SetDynamicDescriptor(1, 0, m_pAverageLuminance->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pLuminanceHistogram->GetSRV());

					context.Dispatch(1);
				};
			});

		graph.AddPass("Tonemap", [&](RGPassBuilder& builder)
			{
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

					context.SetPipelineState(m_pToneMapPSO.get());
					context.SetGraphicsRootSignature(m_pToneMapRS.get());
					context.SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
					context.BeginRenderPass(RenderPassInfo(GetCurrentBackbuffer(), RenderPassAccess::Clear_Store, nullptr, RenderPassAccess::NoAccess));

					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.SetDynamicConstantBufferView(0, &g_WhitePoint, sizeof(float));
					context.SetDynamicDescriptor(1, 0, m_pHDRRenderTarget->GetSRV());
					context.SetDynamicDescriptor(1, 1, m_pAverageLuminance->GetSRV());
					context.Draw(0, 3);
					context.EndRenderPass();
				};
			});
	}

	//UI
	// - ImGui render, pretty straight forward
	{
		m_pImGuiRenderer->Render(graph, GetCurrentBackbuffer());
	}

	graph.AddPass("Temp Barriers", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
			};
		});

	graph.Compile();
	if (gDumpRenderGraph)
	{
		graph.DumpGraphMermaid("graph.html");
		gDumpRenderGraph = false;
	}
	nextFenceValue = graph.Execute();

	//PRESENT
	//	- Set fence for the currently queued frame
	//	- Present the frame buffer
	//	- Wait for the next frame to be finished to start queueing work for it
	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	IdleGPU();
	m_pSwapchain->SetFullscreenState(false, nullptr);
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame();
}

void Graphics::EndFrame(uint64 fenceValue)
{
	//This always gets me confused!
	//The 'm_CurrentBackBufferIndex' is the frame that just got queued so we set the fence value on that frame
	//We present and request the new backbuffer index and wait for that one to finish on the GPU before starting to queue work for that frame.

	++m_Frame;
	Profiler::Instance()->BeginReadback(m_Frame);
	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	Profiler::Instance()->EndReadBack(m_Frame);
	DebugRenderer::Instance().EndFrame();
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D()");
	UINT dxgiFactoryFlags = 0;

#if D3D_VALIDATION
	//Enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();

#if GPU_VALIDATION
	ComPtr<ID3D12Debug1> pDebugController1;
	HR(pDebugController->QueryInterface(IID_PPV_ARGS(&pDebugController1)));
	pDebugController1->SetEnableGPUBasedValidation(true);
#endif

	// Enable additional debug layers.
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	//Create the factory
	ComPtr<IDXGIFactory6> pFactory;
	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory)));

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
		E_LOG(Info, "\t%s", name);
	}
	pFactory->EnumAdapterByGpuPreference(0, gpuPreference, IID_PPV_ARGS(pAdapter.GetAddressOf()));
	DXGI_ADAPTER_DESC3 desc;
	pAdapter->GetDesc3(&desc);
	char name[256];
	ToMultibyte(desc.Description, name, 256);
	E_LOG(Info, "Using %s", name);

	//Create the device
	HR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));
	pAdapter.Reset();

	m_pDevice.As(&m_pRaytracingDevice);

#if D3D_VALIDATION
	ID3D12InfoQueue* pInfoQueue = nullptr;
	if (HR(m_pDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
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

#if 0
		HR(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true));
#endif
		pInfoQueue->PushStorageFilter(&NewFilter);
		pInfoQueue->Release();
	}
#endif

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupport{};
	if (m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupport, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5)) == S_OK)
	{
		m_RenderPassTier = featureSupport.RenderPassesTier;
		m_RayTracingTier = featureSupport.RaytracingTier;
	}

	D3D12_FEATURE_DATA_SHADER_MODEL shaderModelSupport{};
	shaderModelSupport.HighestShaderModel = D3D_SHADER_MODEL_6_5;
	m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModelSupport, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL));
	m_ShaderModelMajor = shaderModelSupport.HighestShaderModel >> 0x4;
	m_ShaderModelMinor = shaderModelSupport.HighestShaderModel & 0xF;

	//Check MSAA support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = m_SampleCount;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	m_SampleQuality = qualityLevels.NumQualityLevels - 1;

	//Create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	//m_CommandQueues[D3D12_COMMAND_LIST_TYPE_BUNDLE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_BUNDLE);

	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128);
	m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = std::make_unique<OfflineDescriptorAllocator>(this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	Profiler::Instance()->Initialize(this);

	m_pSwapchain.Reset();

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
	HR(pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(), 
		m_pWindow, 
		&swapchainDesc, 
		&fsDesc, 
		nullptr, 
		swapChain.GetAddressOf()));

	swapChain.As(&m_pSwapchain);

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
	m_pDownscaledColor = std::make_unique<Texture>(this, "Downscaled HDR Target");
	m_pNormals = std::make_unique<Texture>(this, "MSAA Normals");
	m_pResolvedNormals = std::make_unique<Texture>(this, "Normals");
	m_pAmbientOcclusion = std::make_unique<Texture>(this, "SSAO");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pTiledForward = std::make_unique<TiledForward>(this);
	m_pRTAO = std::make_unique<RTAO>(this);
	m_pSSAO = std::make_unique<SSAO>(this);
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));
	m_pParticles = std::make_unique<GpuParticles>(this);
	m_pParticles->Initialize();

	OnResize(m_WindowWidth, m_WindowHeight);

	m_pGraphAllocator = std::make_unique<RGResourceAllocator>(this);

	DebugRenderer::Instance().Initialize(this);
	DebugRenderer::Instance().SetCamera(m_pCamera.get());
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
	HR(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT, 
		m_WindowWidth, 
		m_WindowHeight, 
		SWAPCHAIN_FORMAT,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	//Recreate the render target views
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		ID3D12Resource* pResource = nullptr;
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
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
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

	m_pNormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, m_SampleCount));
	m_pResolvedNormals->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::ShaderResource));
	m_pAmbientOcclusion->Create(TextureDesc::CreateRenderTarget(Math::DivideAndRoundUp(width, 2), Math::DivideAndRoundUp(height, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource | TextureFlag::RenderTarget));

	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
	m_pTiledForward->OnSwapchainCreated(width, height);
	m_pRTAO->OnSwapchainCreated(width, height);
	m_pSSAO->OnSwapchainCreated(width, height);

	m_ReductionTargets.clear();
	int w = GetWindowWidth();
	int h = GetWindowHeight();
	while (w > 1 || h > 1)
	{
		w = Math::DivideAndRoundUp(w, 16);
		h = Math::DivideAndRoundUp(h, 16);
		std::unique_ptr<Texture> pTexture = std::make_unique<Texture>(this);
		pTexture->Create(TextureDesc::Create2D(w, h, DXGI_FORMAT_R32G32_FLOAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
		m_ReductionTargets.push_back(std::move(pTexture));
	}

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		std::unique_ptr<Buffer> pBuffer = std::make_unique<Buffer>(this);
		pBuffer->Create(BufferDesc::CreateStructured(2, sizeof(float), BufferFlag::Readback));
		m_ReductionReadbackTargets.push_back(std::move(pBuffer));
	}
}

void Graphics::InitializeAssets()
{
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pLightBuffer = std::make_unique<Buffer>(this, "Lights");

	//Input layout
	//UNIVERSAL
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_INPUT_ELEMENT_DESC depthOnlyInputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::Vertex, "VSMain");
			Shader alphaPixelShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::Pixel, "PSMain");

			//Rootsignature
			m_pShadowsRS = std::make_unique<RootSignature>();
			m_pShadowsRS->FinalizeFromShader("Shadow Mapping (Opaque)", vertexShader, m_pDevice.Get());

			//Pipeline state
			m_pShadowsOpaquePSO = std::make_unique<PipelineState>();
			m_pShadowsOpaquePSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
			m_pShadowsOpaquePSO->SetRootSignature(m_pShadowsRS->GetRootSignature());
			m_pShadowsOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowsOpaquePSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowsOpaquePSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsOpaquePSO->SetDepthBias(-1, -5.0f, -4.0f);
			m_pShadowsOpaquePSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());

			m_pShadowsAlphaPSO = std::make_unique<PipelineState>(*m_pShadowsOpaquePSO);
			m_pShadowsAlphaPSO->SetPixelShader(alphaPixelShader.GetByteCode(), alphaPixelShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		}

		m_pShadowMap = std::make_unique<Texture>(this, "Shadow Map");
		m_pShadowMap->Create(TextureDesc::CreateDepth(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
	}

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::Vertex, "VSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pDepthPrepassPSO = std::make_unique<PipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	//Normals
	{
		Shader vertexShader("Resources/Shaders/OutputNormals.hlsl", Shader::Type::Vertex, "VSMain");
		Shader pixelShader("Resources/Shaders/OutputNormals.hlsl", Shader::Type::Pixel, "PSMain");

		//Rootsignature
		m_pNormalsRS = std::make_unique<RootSignature>();
		m_pNormalsRS->FinalizeFromShader("Normals", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pNormalsPSO = std::make_unique<PipelineState>();
		m_pNormalsPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pNormalsPSO->SetRootSignature(m_pNormalsRS->GetRootSignature());
		m_pNormalsPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pNormalsPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pNormalsPSO->SetRenderTargetFormat(DXGI_FORMAT_R32G32B32A32_FLOAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pNormalsPSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		m_pNormalsPSO->SetDepthWrite(false);
		m_pNormalsPSO->Finalize("Normals Pipeline", m_pDevice.Get());
	}

	//Luminance Historgram
	{
		Shader computeShader("Resources/Shaders/LuminanceHistogram.hlsl", Shader::Type::Compute, "CSMain");

		//Rootsignature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>();
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Historgram", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pLuminanceHistogramPSO = std::make_unique<PipelineState>();
		m_pLuminanceHistogramPSO->SetRootSignature(m_pLuminanceHistogramRS->GetRootSignature());
		m_pLuminanceHistogramPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pLuminanceHistogramPSO->Finalize("Luminance Historgram", m_pDevice.Get());

		m_pLuminanceHistogram = std::make_unique<Buffer>(this);
		m_pLuminanceHistogram->Create(BufferDesc::CreateByteAddress(sizeof(uint32) * 256));
		m_pAverageLuminance = std::make_unique<Texture>(this);
		m_pAverageLuminance->Create(TextureDesc::Create2D(1, 1, DXGI_FORMAT_R32_FLOAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
	}

	//Average Luminance
	{
		Shader computeShader("Resources/Shaders/AverageLuminance.hlsl", Shader::Type::Compute, "CSMain");

		//Rootsignature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>();
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pAverageLuminancePSO = std::make_unique<PipelineState>();
		m_pAverageLuminancePSO->SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		m_pAverageLuminancePSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pAverageLuminancePSO->Finalize("Average Luminance", m_pDevice.Get());
	}

	//Tonemapping
	{
		Shader vertexShader("Resources/Shaders/Tonemapping.hlsl", Shader::Type::Vertex, "VSMain");
		Shader pixelShader("Resources/Shaders/Tonemapping.hlsl", Shader::Type::Pixel, "PSMain");

		//Rootsignature
		m_pToneMapRS = std::make_unique<RootSignature>();
		m_pToneMapRS->FinalizeFromShader("Tonemapping", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pToneMapPSO = std::make_unique<PipelineState>();
		m_pToneMapPSO->SetDepthEnabled(false);
		m_pToneMapPSO->SetDepthWrite(false);
		m_pToneMapPSO->SetRootSignature(m_pToneMapRS->GetRootSignature());
		m_pToneMapPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pToneMapPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pToneMapPSO->SetRenderTargetFormat(SWAPCHAIN_FORMAT, DEPTH_STENCIL_FORMAT, 1, 0);
		m_pToneMapPSO->Finalize("Tone mapping Pipeline", m_pDevice.Get());
	}

	//Depth resolve
	//Resolves a multisampled depth buffer to a normal depth buffer
	//Only required when the sample count > 1
	{
		Shader computeShader("Resources/Shaders/ResolveDepth.hlsl", Shader::Type::Compute, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->FinalizeFromShader("Depth Resolve", computeShader, m_pDevice.Get());

		m_pResolveDepthPSO = std::make_unique<PipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	//Depth reduce
	{
		Shader prepareReduceShader("Resources/Shaders/ReduceDepth.hlsl", Shader::Type::Compute, "PrepareReduceDepth", { });
		Shader prepareReduceShaderMSAA("Resources/Shaders/ReduceDepth.hlsl", Shader::Type::Compute, "PrepareReduceDepth", { "WITH_MSAA" });
		Shader reduceShader("Resources/Shaders/ReduceDepth.hlsl", Shader::Type::Compute, "ReduceDepth", { });

		m_pReduceDepthRS = std::make_unique<RootSignature>();
		m_pReduceDepthRS->FinalizeFromShader("Depth Reduce", prepareReduceShader, m_pDevice.Get());

		m_pPrepareReduceDepthPSO = std::make_unique<PipelineState>();
		m_pPrepareReduceDepthPSO->SetComputeShader(prepareReduceShader.GetByteCode(), prepareReduceShader.GetByteCodeSize());
		m_pPrepareReduceDepthPSO->SetRootSignature(m_pReduceDepthRS->GetRootSignature());
		m_pPrepareReduceDepthPSO->Finalize("Prepare Reduce Depth Pipeline", m_pDevice.Get());
		m_pPrepareReduceDepthMsaaPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pPrepareReduceDepthMsaaPSO->SetComputeShader(prepareReduceShaderMSAA.GetByteCode(), prepareReduceShaderMSAA.GetByteCodeSize());
		m_pPrepareReduceDepthMsaaPSO->Finalize("Prepare Reduce Depth Pipeline MSAA", m_pDevice.Get());

		m_pReduceDepthPSO = std::make_unique<PipelineState>(*m_pPrepareReduceDepthPSO);
		m_pReduceDepthPSO->SetComputeShader(reduceShader.GetByteCode(), reduceShader.GetByteCodeSize());
		m_pReduceDepthPSO->Finalize("Reduce Depth Pipeline", m_pDevice.Get());
	}

	//Mip generation
	{
		Shader computeShader("Resources/Shaders/GenerateMips.hlsl", Shader::Type::Compute, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>();
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips", computeShader, m_pDevice.Get());

		m_pGenerateMipsPSO = std::make_unique<PipelineState>();
		m_pGenerateMipsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pGenerateMipsPSO->SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		m_pGenerateMipsPSO->Finalize("Generate Mips PSO", m_pDevice.Get());
	}

	//Sky
	{
		D3D12_INPUT_ELEMENT_DESC cubeInputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		Shader vertexShader("Resources/Shaders/ProceduralSky.hlsl", Shader::Type::Vertex, "VSMain");
		Shader pixelShader("Resources/Shaders/ProceduralSky.hlsl", Shader::Type::Pixel, "PSMain");

		//Rootsignature
		m_pSkyboxRS = std::make_unique<RootSignature>();
		m_pSkyboxRS->FinalizeFromShader("Skybox", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pSkyboxPSO = std::make_unique<PipelineState>();
		m_pSkyboxPSO->SetInputLayout(nullptr, 0);
		m_pSkyboxPSO->SetRootSignature(m_pSkyboxRS->GetRootSignature());
		m_pSkyboxPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pSkyboxPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pSkyboxPSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pSkyboxPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pSkyboxPSO->Finalize("Skybox", m_pDevice.Get());
	}

	//Geometry
	{
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pContext);

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
	}

	m_pRTAO->GenerateAccelerationStructure(this, m_pMesh.get(), *pContext);
	pContext->Execute(true);
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = GameTimer::DeltaTime();

	if(m_pVisualizeTexture)
	{
		ImGui::Begin("Shadow Map");
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

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
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

		if (m_RenderPath == RenderPath::Clustered)
		{
			extern bool g_VisualizeClusters;
			ImGui::Checkbox("Visualize Clusters", &g_VisualizeClusters);
		}
		else if (m_RenderPath == RenderPath::Tiled)
		{
			extern bool g_VisualizeLightDensity;
			ImGui::Checkbox("Visualize Light Density", &g_VisualizeLightDensity);
		}

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
	ImGui::End();

	static bool showOutputLog = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(300, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
	ImGui::SetNextWindowSize(ImVec2(showOutputLog ? (float)(m_WindowWidth - 250) * 0.5f : m_WindowWidth - 250, 250));
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
		ImGui::SetNextWindowPos(ImVec2(250 + (m_WindowWidth - 250) / 2.0f, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
		ImGui::SetNextWindowSize(ImVec2((float)(m_WindowWidth - 250) * 0.5f, 250));
		ImGui::SetNextWindowCollapsed(!showOutputLog);
		ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ProfileNode* pRootNode = Profiler::Instance()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::End();
	}
	ImGui::PopStyleVar();

	ImGui::Begin("Parameters");

	ImGui::Text("Sky");
	ImGui::SliderFloat("Sun Orientation", &g_SunOrientation, -Math::PI, Math::PI);
	ImGui::SliderFloat("Sun Inclination", &g_SunInclination, 0, 1);

	ImGui::Text("Shadows");
	ImGui::Checkbox("SDSM", &g_SDSM);
	ImGui::Checkbox("Stabilize Cascades", &g_StabilizeCascades);
	ImGui::SliderFloat("PSSM Factor", &g_PSSMFactor, 0, 1);

	ImGui::Text("Expose/Tonemapping");
	ImGui::SliderFloat("Min Log Luminance", &g_MinLogLuminance, -100, 20);
	ImGui::SliderFloat("Max Log Luminance", &g_MaxLogLuminance, -50, 50);
	ImGui::SliderFloat("White Point", &g_WhitePoint, 0, 20);
	ImGui::SliderFloat("Tau", &g_Tau, 0, 100);

	ImGui::Text("Misc");
	ImGui::Checkbox("Debug Render Lights", &g_VisualizeLights);

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

	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	if (m_FreeCommandLists[typeIndex].size() > 0)
	{
		CommandContext* pCommandList = m_FreeCommandLists[typeIndex].front();
		m_FreeCommandLists[typeIndex].pop();
		pCommandList->Reset();
		return pCommandList;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf()));
		m_CommandLists.push_back(std::move(pCommandList));
		m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type));
		return m_CommandListPool[typeIndex].back().get();
	}
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
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData)));

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
			HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)));
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
	return m_RenderPassTier > D3D12_RENDER_PASS_TIER::D3D12_RENDER_PASS_TIER_0;
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

uint32 Graphics::GetMultiSampleQualityLevel(uint32 msaa)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = msaa;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	return qualityLevels.NumQualityLevels - 1;
}

ID3D12Resource* Graphics::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue /*= nullptr*/)
{
	ID3D12Resource* pResource;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
	HR(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, initialState, pClearValue, IID_PPV_ARGS(&pResource)));
	return pResource;
}