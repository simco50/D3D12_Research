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
#include "Mesh.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"
#include "Core/Input.h"
#include "Texture.h"
#include "GraphicsBuffer.h"
#include "Profiler.h"
#include "ClusteredForward.h"
#include "Scene/Camera.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/Blackboard.h"
#include "RenderGraph/ResourceAllocator.h"
#include "DebugRenderer.h"
#include "ResourceViews.h"

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

	Shader::AddGlobalShaderDefine("BLOCK_SIZE", std::to_string(FORWARD_PLUS_BLOCK_SIZE));
	Shader::AddGlobalShaderDefine("SHADOWMAP_DX", std::to_string(1.0f / SHADOW_MAP_SIZE));
	Shader::AddGlobalShaderDefine("PCF_KERNEL_SIZE", std::to_string(5));
	Shader::AddGlobalShaderDefine("MAX_SHADOW_CASTERS", std::to_string(MAX_SHADOW_CASTERS));

	InitD3D();
	InitializeAssets();

	RandomizeLights(m_DesiredLightCount);
}

void Graphics::RandomizeLights(int count)
{
	m_Lights.resize(count);

	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	int lightIndex = 0;
	Vector3 Dir(-300, -300, -300);
	Dir.Normalize();
	m_Lights[lightIndex] = Light::Directional(Vector3(300, 300, 300), Dir, 0.1f);
	m_Lights[lightIndex].ShadowIndex = 0;
	
	int randomLightsStartIndex = lightIndex+1;

	for (int i = randomLightsStartIndex; i < m_Lights.size(); ++i)
	{
		Vector3 c = Vector3(Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f));
		Vector4 color(c.x, c.y, c.z, 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(4.0f, 6.0f);
		const float angle = Math::RandomRange(40.0f, 80.0f);

		Light::Type type = rand() % 2 == 0 ? Light::Type::Point : Light::Type::Spot;
		switch (type)
		{
		case Light::Type::Point:
			m_Lights[i] = Light::Point(position, range, 4.0f, 0.5f, color);
			break;
		case Light::Type::Spot:
			m_Lights[i] = Light::Spot(position, range, Math::RandVector(), angle, 4.0f, 0.5f, color);
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
	if (m_pLightBuffer->GetDesc().ElementCount != count)
	{
		m_pLightBuffer->Create(BufferDesc::CreateStructured(count, sizeof(Light)));
	}
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pLightBuffer->SetData(pContext, m_Lights.data(), sizeof(Light) * m_Lights.size());
	pContext->Execute(true);
}

void Graphics::Update()
{
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

	//PER FRAME CONSTANTS
	/////////////////////////////////////////
	struct PerFrameData
	{
		Matrix ViewInverse;
	} frameData;

	//Camera constants
	frameData.ViewInverse = m_pCamera->GetViewInverse();

	// SHADOW MAP PARTITIONING
	/////////////////////////////////////////

	struct LightData
	{
		Matrix LightViewProjections[MAX_SHADOW_CASTERS];
		Vector4 ShadowMapOffsets[MAX_SHADOW_CASTERS];
	} lightData;

	Matrix projection = Math::CreateOrthographicMatrix(512, 512, 10000, 0.1f);
	
	m_ShadowCasters = 0;
	lightData.LightViewProjections[m_ShadowCasters] = Matrix(XMMatrixLookAtLH(m_Lights[0].Position, Vector3(), Vector3(0.0f, 1.0f, 0.0f))) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 1.0f;
	++m_ShadowCasters;

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////
	
	PROFILE_END();

	BeginFrame();
	m_pImGuiRenderer->Update();

	RGGraph graph(m_pGraphAllocator.get());
	struct MainData
	{
		RGResourceHandle DepthStencil;
		RGResourceHandle DepthStencilResolved;
	};
	MainData Data;
	Data.DepthStencil = graph.ImportTexture("Depth Stencil", GetDepthStencil());
	Data.DepthStencilResolved = graph.ImportTexture("Depth Stencil Target", GetResolvedDepthStencil());

	uint64 nextFenceValue = 0;
	uint64 lightCullingFence = 0;

	//1. DEPTH PREPASS
	// - Depth only pass that renders the entire scene
	// - Optimization that prevents wasteful lighting calculations during the base pass
	// - Required for light culling
	graph.AddPass("Depth Prepass", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			Data.DepthStencil = builder.Write(Data.DepthStencil);

			return [=](CommandContext& renderContext, const RGPassResources& resources)
			{
				Texture* pDepthStencil = resources.GetTexture(Data.DepthStencil);
				const TextureDesc& desc = pDepthStencil->GetDesc();
				renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				renderContext.InsertResourceBarrier(m_pMSAANormals.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

				RenderPassInfo info = RenderPassInfo(m_pMSAANormals.get(), RenderPassAccess::Clear_Resolve, pDepthStencil, RenderPassAccess::Clear_Store);
				info.RenderTargets[0].ResolveTarget = m_pNormals.get();

				renderContext.BeginRenderPass(info);
				renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

				renderContext.SetGraphicsPipelineState(m_pDepthPrepassPSO.get());
				renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());

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
			};
		});

	//2. [OPTIONAL] DEPTH RESOLVE
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
					renderContext.SetComputePipelineState(m_pResolveDepthPSO.get());

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

	graph.AddPass("SSAO", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			Data.DepthStencilResolved = builder.Read(Data.DepthStencilResolved);
			return [=](CommandContext& renderContext, const RGPassResources& resources)
			{
				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencilResolved), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pNormals.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pSSAOTarget.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				renderContext.InsertResourceBarrier(m_pNoiseTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				renderContext.SetComputeRootSignature(m_pSSAORS.get());
				renderContext.SetComputePipelineState(m_pSSAOPSO.get());

				struct ShaderParameters
				{
					Vector4 RandomVectors[32];
					Matrix ProjectionInverse;
					Matrix Projection;
					Matrix View;
					uint32 Dimensions[2];
				} shaderParameters;

				//lovely hacky
				static bool written = false;
				static Vector4 randoms[32];
				if (!written)
				{
					for (int i = 0; i < 32; ++i)
					{
						randoms[i] = Vector4(Math::RandVector());
						randoms[i].z = Math::Lerp(0.1f, 1.0f, (float)abs(randoms[i].z));
						randoms[i].Normalize();
						randoms[i] *= Math::Lerp(0.1f, 1.0f, (float)pow(Math::RandomRange(0, 1), 2));
					}
					written = true;
				}
				memcpy(shaderParameters.RandomVectors, randoms, sizeof(Vector4) * 32);

				shaderParameters.ProjectionInverse = m_pCamera->GetProjectionInverse();
				shaderParameters.Projection = m_pCamera->GetProjection();
				shaderParameters.View = m_pCamera->GetView();
				shaderParameters.Dimensions[0] = m_pSSAOTarget->GetWidth();
				shaderParameters.Dimensions[1] = m_pSSAOTarget->GetHeight();

				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
				renderContext.SetDynamicDescriptor(1, 0, m_pSSAOTarget->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, resources.GetTexture(Data.DepthStencilResolved)->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, m_pNormals.get()->GetSRV());
				renderContext.SetDynamicDescriptor(2, 2, m_pNoiseTexture.get()->GetSRV());

				int dispatchGroupsX = Math::DivideAndRoundUp(m_pSSAOTarget->GetWidth(), 16);
				int dispatchGroupsY = Math::DivideAndRoundUp(m_pSSAOTarget->GetHeight(), 16);
				renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);

				renderContext.InsertResourceBarrier(resources.GetTexture(Data.DepthStencilResolved), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(m_pSSAOTarget.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			};
		});

	if (m_RenderPath == RenderPath::Tiled)
	{
		//3. LIGHT CULLING
		// - Compute shader to buckets lights in tiles depending on their screen position.
		// - Requires a depth buffer 
		// - Outputs a: - Texture containing a count and an offset of lights per tile.
		//				- uint[] index buffer to indicate what lights are visible in each tile.
		graph.AddPass("Light Culling", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				Data.DepthStencilResolved = builder.Read(Data.DepthStencilResolved);
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(GetResolvedDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounterRawUAV);

					context.SetComputePipelineState(m_pComputeLightCullPSO.get());
					context.SetComputeRootSignature(m_pComputeLightCullRS.get());

					struct ShaderParameters
					{
						Matrix CameraView;
						Matrix ProjectionInverse;
						uint32 NumThreadGroups[4];
						Vector2 ScreenDimensions;
						uint32 LightCount;
					} Data{};

					Data.CameraView = m_pCamera->GetView();
					Data.NumThreadGroups[0] = Math::DivideAndRoundUp(m_WindowWidth, FORWARD_PLUS_BLOCK_SIZE);
					Data.NumThreadGroups[1] = Math::DivideAndRoundUp(m_WindowHeight, FORWARD_PLUS_BLOCK_SIZE);
					Data.NumThreadGroups[2] = 1;
					Data.ScreenDimensions.x = (float)m_WindowWidth;
					Data.ScreenDimensions.y = (float)m_WindowHeight;
					Data.LightCount = (uint32)m_Lights.size();
					Data.ProjectionInverse = m_pCamera->GetProjectionInverse();

					context.SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameters));
					context.SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
					context.SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
					context.SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
					context.SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparant->GetUAV());
					context.SetDynamicDescriptor(1, 4, m_pLightGridTransparant->GetUAV());
					context.SetDynamicDescriptor(2, 0, GetResolvedDepthStencil()->GetSRV());
					context.SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

					context.Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
				};
			});

		//4. SHADOW MAPPING
		// - Renders the scene depth onto a separate depth buffer from the light's view
		if (m_ShadowCasters > 0)
		{
			graph.AddPass("Shadow Mapping", [&](RGPassBuilder& builder)
				{
					builder.NeverCull();
					return [=](CommandContext& context, const RGPassResources& resources)
					{
						context.InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

						context.BeginRenderPass(RenderPassInfo(m_pShadowMap.get(), RenderPassAccess::Clear_Store));
						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

						for (int i = 0; i < m_ShadowCasters; ++i)
						{
							GPU_PROFILE_SCOPE("Light View", &context);
							const Vector4& shadowOffset = lightData.ShadowMapOffsets[i];
							FloatRect viewport;
							viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
							viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
							viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
							viewport.Bottom = viewport.Top + shadowOffset.z * (float)m_pShadowMap->GetHeight();
							context.SetViewport(viewport);

							struct PerObjectData
							{
								Matrix WorldViewProjection;
							} ObjectData{};
							context.SetGraphicsRootSignature(m_pShadowsRS.get());

							//Opaque
							{
								GPU_PROFILE_SCOPE("Opaque", &context);
								context.SetGraphicsPipelineState(m_pShadowsOpaquePSO.get());

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
								context.SetGraphicsPipelineState(m_pShadowsAlphaPSO.get());

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

		//5. BASE PASS
		// - Render the scene using the shadow mapping result and the light culling buffers
		graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

					context.InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

					context.BeginRenderPass(RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Clear_Store, GetDepthStencil(), RenderPassAccess::Load_DontCare));

					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.SetGraphicsRootSignature(m_pPBRDiffuseRS.get());

					context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
					context.SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
					context.SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
					context.SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
					context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());
					context.SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

					struct PerObjectData
					{
						Matrix World;
						Matrix WorldViewProjection;
					} ObjectData{};

					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetGraphicsPipelineState(m_pPBRDiffusePSO.get());
						
						for (const Batch& b : m_OpaqueBatches)
						{
							ObjectData.World = b.WorldMatrix;
							ObjectData.WorldViewProjection = ObjectData.World * m_pCamera->GetViewProjection();
							context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
							context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
							context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
							context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
							b.pMesh->Draw(&context);
						}
					}

					{
						GPU_PROFILE_SCOPE("Transparant", &context);
						context.SetGraphicsPipelineState(m_pPBRDiffuseAlphaPSO.get());

						for (const Batch& b : m_TransparantBatches)
						{
							ObjectData.World = b.WorldMatrix;
							ObjectData.WorldViewProjection = ObjectData.World * m_pCamera->GetViewProjection();
							context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
							context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
							context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
							context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
							b.pMesh->Draw(&context);
						}
					}

					context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.EndRenderPass();
				};
			});
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
		m_pClusteredForward->Execute(graph, resources);
	}

	m_pDebugRenderer->Render(graph);

	//7. MSAA Render Target Resolve
	// - We have to resolve a MSAA render target ourselves. Unlike D3D11, this is not done automatically by the API.
	//	Luckily, there's a method that does it for us!
	if (m_SampleCount > 1)
	{
		graph.AddPass("Resolve", [&](RGPassBuilder& builder)
			{
				builder.NeverCull();
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
					context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
					context.ResolveResource(GetCurrentRenderTarget(), 0, m_pHDRRenderTarget.get(), 0, RENDER_TARGET_FORMAT);
				};
			});
	}

	//8. Tonemapping
	{
		bool downscaleTonemapInput = true;
		Texture* pToneMapInput = downscaleTonemapInput ? m_pDownscaledColor.get() : m_pHDRRenderTarget.get();
		RGResourceHandle toneMappingInput = graph.ImportTexture("Tonemap Input", pToneMapInput);

		if (downscaleTonemapInput)
		{
			graph.AddPass("Downsample Color", [&](RGPassBuilder& builder)
				{
					builder.NeverCull();
					toneMappingInput = builder.Write(toneMappingInput);
					return [=](CommandContext& context, const RGPassResources& resources)
					{
						Texture* pToneMapInput = resources.GetTexture(toneMappingInput);
						context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

						context.SetComputePipelineState(m_pGenerateMipsPSO.get());
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
				builder.NeverCull();
				toneMappingInput = builder.Read(toneMappingInput);
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					Texture* pToneMapInput = resources.GetTexture(toneMappingInput);

					context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.InsertResourceBarrier(pToneMapInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.ClearUavUInt(m_pLuminanceHistogram.get(), m_pLuminanceHistogram->GetUAV());

					context.SetComputePipelineState(m_pLuminanceHistogramPSO.get());
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
				builder.NeverCull();
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(m_pLuminanceHistogram.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetComputePipelineState(m_pAverageLuminancePSO.get());
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
				builder.NeverCull();
				return [=](CommandContext& context, const RGPassResources& resources)
				{
					context.InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET);
					context.InsertResourceBarrier(m_pAverageLuminance.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pHDRRenderTarget.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

					context.SetGraphicsPipelineState(m_pToneMapPSO.get());
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

	//9. UI
	// - ImGui render, pretty straight forward
	{
		m_pImGuiRenderer->Render(graph, GetCurrentBackbuffer());
	}

	graph.AddPass("Temp Barriers", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
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
	nextFenceValue = graph.Execute(this);

	//10. PRESENT
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
	m_pDebugRenderer->EndFrame();
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

#if 1
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
	}

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
	m_pMSAANormals = std::make_unique<Texture>(this, "MSAA Normals");
	m_pNormals = std::make_unique<Texture>(this, "Normals");
	m_pSSAOTarget = std::make_unique<Texture>(this, "SSAO");

	m_pLightGridOpaque = std::make_unique<Texture>(this, "Opaque Light Grid");
	m_pLightGridTransparant = std::make_unique<Texture>(this, "Transparant Light Grid");

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pImGuiRenderer->AddUpdateCallback(ImGuiCallbackDelegate::CreateRaw(this, &Graphics::UpdateImGui));

	OnResize(m_WindowWidth, m_WindowHeight);

	m_pGraphAllocator = std::make_unique<RGResourceAllocator>(this);
	m_pDebugRenderer = std::make_unique<DebugRenderer>(this);
	m_pDebugRenderer->SetCamera(m_pCamera.get());
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
	m_pHDRRenderTarget->Create(TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::RenderTarget));
	m_pDownscaledColor->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), RENDER_TARGET_FORMAT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));

	m_pMSAANormals->Create(TextureDesc::CreateRenderTarget(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::RenderTarget, m_SampleCount));
	m_pNormals->Create(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::ShaderResource));
	m_pSSAOTarget->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(width, 4), Math::DivideAndRoundUp(height, 4), DXGI_FORMAT_R32_FLOAT, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));

	int frustumCountX = Math::RoundUp((float)width / FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::RoundUp((float)height / FORWARD_PLUS_BLOCK_SIZE);
	m_pLightGridOpaque->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pLightGridTransparant->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	
	m_pCamera->SetDirty();

	m_pClusteredForward->OnSwapchainCreated(width, height);
}

void Graphics::InitializeAssets()
{
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

	//PBR Diffuse passes
	{
		//Shaders
		Shader vertexShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain", { /*"SHADOW"*/ });
		Shader pixelShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { /*"SHADOW"*/ });

		//Rootsignature
		m_pPBRDiffuseRS = std::make_unique<RootSignature>();
		m_pPBRDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, m_pDevice.Get());

		{
			//Opaque
			m_pPBRDiffusePSO = std::make_unique<GraphicsPipelineState>();
			m_pPBRDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pPBRDiffusePSO->SetRootSignature(m_pPBRDiffuseRS->GetRootSignature());
			m_pPBRDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pPBRDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pPBRDiffusePSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pPBRDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			m_pPBRDiffusePSO->SetDepthWrite(false);
			m_pPBRDiffusePSO->Finalize("Diffuse PBR Pipeline", m_pDevice.Get());

			//Transparant
			m_pPBRDiffuseAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pPBRDiffusePSO.get());
			m_pPBRDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
			m_pPBRDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pPBRDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline", m_pDevice.Get());
		}
	}

	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");
			Shader alphaVertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain", { "ALPHA_BLEND" });
			Shader alphaPixelShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::PixelShader, "PSMain", { "ALPHA_BLEND" });

			//Rootsignature
			m_pShadowsRS = std::make_unique<RootSignature>();
			m_pShadowsRS->FinalizeFromShader("Shadow Mapping (Opaque)", vertexShader, m_pDevice.Get());

			//Pipeline state
			m_pShadowsOpaquePSO = std::make_unique<GraphicsPipelineState>();
			m_pShadowsOpaquePSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
			m_pShadowsOpaquePSO->SetRootSignature(m_pShadowsRS->GetRootSignature());
			m_pShadowsOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowsOpaquePSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowsOpaquePSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsOpaquePSO->SetDepthBias(-1, -5.0f, -4.0f);
			m_pShadowsOpaquePSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());

			m_pShadowsAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pShadowsOpaquePSO);
			m_pShadowsAlphaPSO->SetVertexShader(alphaVertexShader.GetByteCode(), alphaVertexShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->SetPixelShader(alphaPixelShader.GetByteCode(), alphaPixelShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		}

		m_pShadowMap = std::make_unique<Texture>(this, "Shadow Map");
		m_pShadowMap->Create(TextureDesc::CreateDepth(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureFlag::DepthStencil | TextureFlag::ShaderResource, 1, ClearBinding(0.0f, 0)));
	}

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/Shaders/Prepass.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/Prepass.hlsl", Shader::Type::PixelShader, "PSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pDepthPrepassPSO = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetRenderTargetFormat(DXGI_FORMAT_R32G32B32A32_FLOAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	//Luminance Historgram
	{
		Shader computeShader("Resources/Shaders/LuminanceHistogram.hlsl", Shader::Type::ComputeShader, "CSMain");

		//Rootsignature
		m_pLuminanceHistogramRS = std::make_unique<RootSignature>();
		m_pLuminanceHistogramRS->FinalizeFromShader("Luminance Historgram", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pLuminanceHistogramPSO = std::make_unique<ComputePipelineState>();
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
		Shader computeShader("Resources/Shaders/AverageLuminance.hlsl", Shader::Type::ComputeShader, "CSMain");

		//Rootsignature
		m_pAverageLuminanceRS = std::make_unique<RootSignature>();
		m_pAverageLuminanceRS->FinalizeFromShader("Average Luminance", computeShader, m_pDevice.Get());

		//Pipeline state
		m_pAverageLuminancePSO = std::make_unique<ComputePipelineState>();
		m_pAverageLuminancePSO->SetRootSignature(m_pAverageLuminanceRS->GetRootSignature());
		m_pAverageLuminancePSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pAverageLuminancePSO->Finalize("Average Luminance", m_pDevice.Get());
	}

	//Tonemapping
	{
		Shader vertexShader("Resources/Shaders/Tonemapping.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/Tonemapping.hlsl", Shader::Type::PixelShader, "PSMain");

		//Rootsignature
		m_pToneMapRS = std::make_unique<RootSignature>();
		m_pToneMapRS->FinalizeFromShader("Tonemapping", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pToneMapPSO = std::make_unique<GraphicsPipelineState>();
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
	if(m_SampleCount > 1)
	{
		Shader computeShader("Resources/Shaders/ResolveDepth.hlsl", Shader::Type::ComputeShader, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->FinalizeFromShader("Depth Resolve", computeShader, m_pDevice.Get());

		m_pResolveDepthPSO = std::make_unique<ComputePipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	//Light culling
	//Compute shader that requires depth buffer and light data to place lights into tiles
	{
		Shader computeShader("Resources/Shaders/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRS = std::make_unique<RootSignature>();
		m_pComputeLightCullRS->FinalizeFromShader("Light Culling", computeShader, m_pDevice.Get());

		m_pComputeLightCullPSO = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPSO->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
		m_pComputeLightCullPSO->Finalize("Compute Light Culling Pipeline", m_pDevice.Get());

		m_pLightIndexCounter = std::make_unique<Buffer>(this, "Light Index Counter");
		m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32)));
		m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
		m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(this, "Light List Opaque");
		m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));
		m_pLightIndexListBufferTransparant = std::make_unique<Buffer>(this, "Light List Transparant");
		m_pLightIndexListBufferTransparant->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));
		m_pLightBuffer = std::make_unique<Buffer>(this, "Light Buffer");
	}

	//Mip generation
	{
		Shader computeShader("Resources/Shaders/GenerateMips.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pGenerateMipsRS = std::make_unique<RootSignature>();
		m_pGenerateMipsRS->FinalizeFromShader("Generate Mips", computeShader, m_pDevice.Get());

		m_pGenerateMipsPSO = std::make_unique<ComputePipelineState>();
		m_pGenerateMipsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pGenerateMipsPSO->SetRootSignature(m_pGenerateMipsRS->GetRootSignature());
		m_pGenerateMipsPSO->Finalize("Generate Mips PSO", m_pDevice.Get());
	}

	//SSAO
	{
		Shader computeShader("Resources/Shaders/SSAO.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>();
		m_pSSAORS->FinalizeFromShader("SSAO", computeShader, m_pDevice.Get());

		m_pSSAOPSO = std::make_unique<ComputePipelineState>();
		m_pSSAOPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOPSO->SetRootSignature(m_pSSAORS->GetRootSignature());
		m_pSSAOPSO->Finalize("SSAO PSO", m_pDevice.Get());
	}

	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);

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

	m_pNoiseTexture = std::make_unique<Texture>(this, "Noise");
	m_pNoiseTexture->Create(pContext, "Resources/Textures/Noise.png", false);

	pContext->Execute(true);
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = GameTimer::DeltaTime();

	ImGui::Begin("SSAO");
	Vector2 image((float)m_pSSAOTarget->GetWidth(), (float)m_pSSAOTarget->GetHeight());
	Vector2 windowSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
	float width = windowSize.x;
	float height = windowSize.x * image.y / image.x;
	if (image.x / windowSize.x < image.y / windowSize.y)
	{
		width = image.x / image.y * windowSize.y;
		height = windowSize.y;
	}
	ImGui::Image(m_pSSAOTarget.get(), ImVec2(width, height));
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(300, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());
	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), m_Frame % m_FrameTimes.size(), 0, 0.0f, 0.03f, ImVec2(200, 100));

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
		extern bool gVisualizeClusters;
		ImGui::Checkbox("Visualize Clusters", &gVisualizeClusters);

		ImGui::Separator();
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 10, 16384*10);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
		}

		ImGui::SliderFloat("Min Log Luminance", &g_MinLogLuminance, -100, 20);
		ImGui::SliderFloat("Max Log Luminance", &g_MaxLogLuminance, -50, 50);
		ImGui::SliderFloat("White Point", &g_WhitePoint, 0, 20);
		ImGui::SliderFloat("Tau", &g_Tau, 0, 100);

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