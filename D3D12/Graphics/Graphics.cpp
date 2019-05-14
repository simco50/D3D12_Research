#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "DescriptorAllocator.h"
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
#include "PersistentResourceAllocator.h"

const DXGI_FORMAT Graphics::DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
const DXGI_FORMAT Graphics::DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
const DXGI_FORMAT Graphics::RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

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

	Shader::AddGlobalShaderDefine("LIGHT_COUNT", std::to_string(MAX_LIGHT_COUNT));
	Shader::AddGlobalShaderDefine("BLOCK_SIZE", std::to_string(FORWARD_PLUS_BLOCK_SIZE));
	Shader::AddGlobalShaderDefine("SHADOWMAP_DX", std::to_string(1.0f / SHADOW_MAP_SIZE));
	Shader::AddGlobalShaderDefine("PCF_KERNEL_SIZE", std::to_string(5));
	Shader::AddGlobalShaderDefine("MAX_SHADOW_CASTERS", std::to_string(MAX_SHADOW_CASTERS));
	
	InitD3D();
	InitializeAssets();

	m_FrameTimes.resize(64*3);

	m_CameraPosition = Vector3(0, 100, -15);
	m_CameraRotation = Quaternion::CreateFromYawPitchRoll(XM_PIDIV4, XM_PIDIV4, 0);

	RandomizeLights();
}

void Graphics::RandomizeLights()
{
	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	m_Lights.resize(MAX_LIGHT_COUNT);

	int lightIndex = 0;
	m_Lights[lightIndex] = Light::Point(Vector3(0, 20, 0), 200);
	m_Lights[lightIndex].ShadowIndex = lightIndex;

	int randomLightsStartIndex = lightIndex + 1;

	for (int i = randomLightsStartIndex; i < m_Lights.size(); ++i)
	{
		Vector3 c = Vector3(Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f));
		Vector4 color(c.x, c.y, c.z, 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(15.0f, 25.0f);
		const float angle = Math::RandomRange(30.0f, 60.0f);

		Light::Type type = rand() % 2 == 0 ? Light::Type::Point : Light::Type::Spot;
		switch (type)
		{
		case Light::Type::Point:
			m_Lights[i] = Light::Point(position, range, 1.0f, 0.5f, color);
			break;
		case Light::Type::Spot:
			m_Lights[i] = Light::Spot(position, range, Math::RandVector(), angle, 1.0f, 0.5f, color);
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
}

void Graphics::SortBatchesBackToFront(const Vector3& cameraPosition, std::vector<Batch>& batches)
{
	std::sort(batches.begin(), batches.end(), [cameraPosition](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, cameraPosition);
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, cameraPosition);
		return aDist > bDist;
	});
}

void Graphics::Update()
{
	//Render forward+ tiles
	if (Input::Instance().IsKeyPressed('P'))
	{
		m_UseDebugView = !m_UseDebugView;
	}
	if (Input::Instance().IsKeyPressed('O'))
	{
		RandomizeLights();
	}

	//Camera movement
	if (Input::Instance().IsMouseDown(0))
	{
		Vector2 mouseDelta = Input::Instance().GetMouseDelta();
		Quaternion yr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * GameTimer::DeltaTime() * 0.1f, 0);
		Quaternion pr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * GameTimer::DeltaTime() * 0.1f, 0, 0);
		m_CameraRotation = yr * m_CameraRotation * pr;
	}
	Vector3 movement;
	movement.x -= (int)Input::Instance().IsKeyDown('A');
	movement.x += (int)Input::Instance().IsKeyDown('D');
	movement.z -= (int)Input::Instance().IsKeyDown('S');
	movement.z += (int)Input::Instance().IsKeyDown('W');
	movement.y -= (int)Input::Instance().IsKeyDown('Q');
	movement.y += (int)Input::Instance().IsKeyDown('E');
	movement = Vector3::Transform(movement, m_CameraRotation);
	movement *= GameTimer::DeltaTime() * 20.0f;
	m_CameraPosition += movement;

	//Set main light position
	m_Lights[0].Position = Vector3(cos((float)GameTimer::GameTime() / 20.0f) * 70, 50, 0);

	SortBatchesBackToFront(m_CameraPosition, m_TransparantBatches);

	//PER FRAME CONSTANTS
	/////////////////////////////////////////
	struct PerFrameData
	{
		Matrix ViewInverse;
	} frameData;

	//Camera constants
	frameData.ViewInverse = Matrix::CreateFromQuaternion(m_CameraRotation) * Matrix::CreateTranslation(m_CameraPosition);
	Matrix cameraView;
	frameData.ViewInverse.Invert(cameraView);
	Matrix cameraProjection = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 100000.0f, 0.1f);
	Matrix cameraViewProjection = cameraView * cameraProjection;

	// SHADOW MAP PARTITIONING
	/////////////////////////////////////////

	struct LightData
	{
		Matrix LightViewProjections[MAX_SHADOW_CASTERS];
		Vector4 ShadowMapOffsets[MAX_SHADOW_CASTERS];
	} lightData;

	Matrix projection = XMMatrixPerspectiveFovLH(Math::PIDIV2, 1.0f, m_Lights[0].Range, 0.1f);
	
	m_ShadowCasters = 0;
	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 0.25f;
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.25f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 0.25f;
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.5f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 0.25f;
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.75f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 0.25f;
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.0f, 0.0f, -1.0f), Vector3(0.0f, 1.0f, 0.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.25f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 0.25f;
	++m_ShadowCasters;

	lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 1.0f, 0.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.25f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.25f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 0.25f;
	++m_ShadowCasters;

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////

	BeginFrame();

	uint64 nextFenceValue = 0;
	uint64 lightCullingFence = 0;

	//1. DEPTH PREPASS
	// - Depth only pass that renders the entire scene
	// - Optimization that prevents wasteful lighting calculations during the base pass
	// - Required for light culling
	{
		GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("Depth Prepass", pContext);

		pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pContext->SetDepthOnlyTarget(GetDepthStencil()->GetDSV());
		pContext->ClearDepth(GetDepthStencil()->GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0);
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
		pContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

		struct PerObjectData
		{
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.WorldViewProjection = cameraViewProjection;

		pContext->SetGraphicsPipelineState(m_pDepthPrepassPSO.get());
		pContext->SetGraphicsRootSignature(m_pDepthPrepassRS.get());
		pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
		for (const Batch& b : m_OpaqueBatches)
		{
			b.pMesh->Draw(pContext);
		}

		pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
		if (m_SampleCount > 1)
		{
			pContext->InsertResourceBarrier(GetResolvedDepthStencil(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		}

		Profiler::Instance()->End(pContext);
		uint64 depthPrepassFence = pContext->Execute(false);
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(depthPrepassFence);
	}


	//2. [OPTIONAL] DEPTH RESOLVE
	// - If MSAA is enabled, run a compute shader to resolve the depth buffer
	if(m_SampleCount > 1)
	{
		ComputeCommandContext* pContext = static_cast<ComputeCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE));
		Profiler::Instance()->Begin("Depth Resolve", pContext);

		pContext->SetComputeRootSignature(m_pResolveDepthRS.get());
		pContext->SetComputePipelineState(m_pResolveDepthPSO.get());

		pContext->SetDynamicDescriptor(0, 0, GetResolvedDepthStencil()->GetUAV());
		pContext->SetDynamicDescriptor(1, 0, GetDepthStencil()->GetSRV());

		int dispatchGroupsX = Math::RoundUp((float)m_WindowWidth / 16);
		int dispatchGroupsY = Math::RoundUp((float)m_WindowHeight / 16);
		pContext->Dispatch(dispatchGroupsX, dispatchGroupsY, 1);

		pContext->InsertResourceBarrier(GetResolvedDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		Profiler::Instance()->End(pContext);

		uint64 resolveDepthFence = pContext->Execute(false);
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(resolveDepthFence);
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(resolveDepthFence);
	}

	//3. LIGHT CULLING
	// - Compute shader to buckets lights in tiles depending on their screen position.
	// - Requires a depth buffer 
	// - Outputs a: - Texture2D containing a count and an offset of lights per tile.
	//				- uint[] index buffer to indicate what lights are visible in each tile.
	{
		ComputeCommandContext* pContext = static_cast<ComputeCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE));
		Profiler::Instance()->Begin("Light Culling", pContext);
		Profiler::Instance()->Begin("Setup Light Data", pContext);
		uint32 zero[] = { 0, 0 };
		m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32) * 2);
		m_pLightBuffer->SetData(pContext, m_Lights.data(), (uint32)m_Lights.size() * sizeof(Light));
		Profiler::Instance()->End(pContext);

		pContext->SetComputePipelineState(m_pComputeLightCullPSO.get());
		pContext->SetComputeRootSignature(m_pComputeLightCullRS.get());

		struct ShaderParameters
		{
			Matrix CameraView;
			uint32 NumThreadGroups[4];
			Matrix ProjectionInverse;
			Vector2 ScreenDimensions;
		} Data;

		Data.CameraView = cameraView;
		Data.NumThreadGroups[0] = Math::RoundUp((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
		Data.NumThreadGroups[1] = Math::RoundUp((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
		Data.NumThreadGroups[2] = 1;
		Data.ScreenDimensions.x = (float)m_WindowWidth;
		Data.ScreenDimensions.y = (float)m_WindowHeight;
		cameraProjection.Invert(Data.ProjectionInverse);

		pContext->SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameters));
		pContext->SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
		pContext->SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
		pContext->SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
		pContext->SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparant->GetUAV());
		pContext->SetDynamicDescriptor(1, 4, m_pLightGridTransparant->GetUAV());
		pContext->SetDynamicDescriptor(2, 0, GetResolvedDepthStencil()->GetSRV());
		pContext->SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

		pContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
		Profiler::Instance()->End(pContext);

		lightCullingFence = pContext->Execute(false);
	}

	//4. SHADOW MAPPING
	// - Renders the scene depth onto a separate depth buffer from the light's view
	if(m_ShadowCasters > 0)
	{
		GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));

		Profiler::Instance()->Begin("Shadows", pContext);
		pContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pContext->SetDepthOnlyTarget(m_pShadowMap->GetDSV());
		pContext->ClearDepth(m_pShadowMap->GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0);
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		
		for(int i = 0; i < m_ShadowCasters; ++i)
		{
			Profiler::Instance()->Begin("Light View", pContext);
			const Vector4& shadowOffset = lightData.ShadowMapOffsets[i];
			FloatRect viewport;
			viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
			viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
			viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
			viewport.Bottom = viewport.Top + shadowOffset.z * (float)m_pShadowMap->GetHeight();
			pContext->SetViewport(viewport);
			pContext->SetScissorRect(viewport);

			struct PerObjectData
			{
				Matrix WorldViewProjection;
			} ObjectData;
			ObjectData.WorldViewProjection = lightData.LightViewProjections[i];

			//Opaque
			{
				Profiler::Instance()->Begin("Opaque", pContext);
				pContext->SetGraphicsPipelineState(m_pShadowsOpaquePSO.get());
				pContext->SetGraphicsRootSignature(m_pShadowsOpaqueRS.get());

				pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
				for (const Batch& b : m_OpaqueBatches)
				{
					b.pMesh->Draw(pContext);
				}
				Profiler::Instance()->End(pContext);
			}
			//Transparant
			{
				Profiler::Instance()->Begin("Transparant", pContext);
				pContext->SetGraphicsPipelineState(m_pShadowsAlphaPSO.get());
				pContext->SetGraphicsRootSignature(m_pShadowsAlphaRS.get());

				pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
				for (const Batch& b : m_TransparantBatches)
				{
					pContext->SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					b.pMesh->Draw(pContext);
				}
				Profiler::Instance()->End(pContext);
			}
			Profiler::Instance()->End(pContext);
		}
		Profiler::Instance()->End(pContext);
		pContext->Execute(false);
	}

	//Can't do the lighting until the light culling is complete
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(lightCullingFence);

	//5. BASE PASS
	// - Render the scene using the shadow mapping result and the light culling buffers
	{
		GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("3D", pContext);
	
		pContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
		pContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

		pContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false); 
		pContext->InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_READ, false);
		pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		pContext->SetRenderTarget(GetCurrentRenderTarget()->GetRTV(), GetDepthStencil()->GetDSV());
		pContext->ClearRenderTarget(GetCurrentRenderTarget()->GetRTV());
	
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
		struct PerObjectData
		{
			Matrix World;
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.World = XMMatrixIdentity();
		ObjectData.WorldViewProjection = ObjectData.World * cameraViewProjection;
	
		//Opaque
		{
			Profiler::Instance()->Begin("Opaque", pContext);
			pContext->SetGraphicsPipelineState(m_UseDebugView ? m_pDiffuseDebugPSO.get() : m_pDiffuseOpaquePSO.get());
			pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

			pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
			pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
			pContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
			pContext->SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
			pContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());
			pContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

			for (const Batch& b : m_OpaqueBatches)
			{
				pContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
				pContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
			Profiler::Instance()->End(pContext);
		}

		//Transparant
		{
			Profiler::Instance()->Begin("Transparant", pContext);
			pContext->SetGraphicsPipelineState(m_UseDebugView ? m_pDiffuseDebugPSO.get() : m_pDiffuseAlphaPSO.get());
			pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

			pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
			pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
			pContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
			pContext->SetDynamicDescriptor(4, 1, m_pLightGridTransparant->GetSRV());
			pContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparant->GetSRV());
			pContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

			for (const Batch& b : m_TransparantBatches)
			{
				pContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
				pContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
			Profiler::Instance()->End(pContext);
		}

		Profiler::Instance()->End(pContext);

		pContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		pContext->InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pContext->InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

		pContext->Execute(false);
	}

	{
		GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("UI", pContext);
		//6. UI
		// - ImGui render, pretty straight forward
		{
			UpdateImGui();
			m_pImGuiRenderer->Render(*pContext);
		}
		Profiler::Instance()->End(pContext);

		Profiler::Instance()->Begin("Present", pContext);
		//7. MSAA Render Target Resolve
		// - We have to resolve a MSAA render target ourselves. Unlike D3D11, this is not done automatically by the API.
		//	Luckily, there's a method that does it for us!
		{
			if (m_SampleCount > 1)
			{
				pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, false);
				pContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RESOLVE_DEST, true);
				pContext->GetCommandList()->ResolveSubresource(GetCurrentBackbuffer()->GetResource(), 0, GetCurrentRenderTarget()->GetResource(), 0, RENDER_TARGET_FORMAT);
			}
			pContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT, true);
		}
		Profiler::Instance()->End(pContext);
		nextFenceValue = pContext->Execute(false);
	}

	//8. PRESENT
	//	- Set fence for the currently queued frame
	//	- Present the frame buffer
	//	- Wait for the next frame to be finished to start queueing work for it
	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	IdleGPU();
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

	Profiler::Instance()->BeginReadback(m_CurrentBackBufferIndex);
	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	Profiler::Instance()->EndReadBack(m_CurrentBackBufferIndex);
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D()");
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	//Enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();
	// Enable additional debug layers.
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	//Create the factory
	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	//Create the device
	HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));

#ifdef _DEBUG
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

		pInfoQueue->PushStorageFilter(&NewFilter);
		pInfoQueue->Release();
	}
#endif

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
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	m_pPersistentAllocationManager = std::make_unique<PersistentResourceAllocator>(GetDevice());
	Profiler::Instance()->Initialize(this);

	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = RENDER_TARGET_FORMAT;
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
	HR(m_pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(), 
		m_pWindow, 
		&swapchainDesc, 
		&fsDesc, 
		nullptr, 
		&swapChain));

	swapChain.As(&m_pSwapchain);

	//Create the textures but don't create the resources themselves yet.
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i] = std::make_unique<Texture2D>();
	}
	m_pDepthStencil = std::make_unique<Texture2D>();

	if (m_SampleCount > 1)
	{
		m_pResolvedDepthStencil = std::make_unique<Texture2D>();
		for (int i = 0; i < FRAME_COUNT; ++i)
		{
			m_MultiSampleRenderTargets[i] = std::make_unique<Texture2D>();
		}
	}

	m_pLightGridOpaque = std::make_unique<Texture2D>();
	m_pLightGridTransparant = std::make_unique<Texture2D>();

	OnResize(m_WindowWidth, m_WindowHeight);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
}

void Graphics::OnResize(int width, int height)
{
	E_LOG(Info, "Graphics::OnResize()");
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i]->Release();
	}
	m_pDepthStencil->Release();

	//Resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT, 
		m_WindowWidth, 
		m_WindowHeight, 
		RENDER_TARGET_FORMAT,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	//Recreate the render target views
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		ID3D12Resource* pResource = nullptr;
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_RenderTargets[i]->CreateForSwapchain(this, pResource);
		m_RenderTargets[i]->SetName("Rendertarget");

		if (m_SampleCount > 1)
		{
			m_MultiSampleRenderTargets[i]->Create(this, width, height, RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_SampleCount);
			m_MultiSampleRenderTargets[i]->SetName("Multisample Rendertarget");
		}
	}
	if (m_SampleCount > 1)
	{
		m_pDepthStencil->Create(this, width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount);
		m_pDepthStencil->SetName("Depth Stencil");
		m_pResolvedDepthStencil->Create(this, width, height, DXGI_FORMAT_R32_FLOAT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess, 1);
		m_pResolvedDepthStencil->SetName("Resolve Depth Stencil");
	}
	else
	{
		m_pDepthStencil->Create(this, width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount);
		m_pDepthStencil->SetName("Depth Stencil");
	}

	int frustumCountX = (int)(ceil((float)width / FORWARD_PLUS_BLOCK_SIZE));
	int frustumCountY = (int)(ceil((float)height / FORWARD_PLUS_BLOCK_SIZE));
	m_pLightGridOpaque->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess, 1);
	m_pLightGridTransparant->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess, 1);
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
	};

	D3D12_INPUT_ELEMENT_DESC depthOnlyAlphaInputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//Diffuse passes
	{
		//Shaders
		Shader vertexShader("Resources/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain");

		//Rootsignature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pDiffuseRS->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetConstantBufferView(2, 2, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRS->SetDescriptorTableSimple(3, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRS->SetDescriptorTableSimple(4, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		//Static samplers
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pDiffuseRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		m_pDiffuseRS->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pDiffuseRS->AddStaticSampler(2, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		m_pDiffuseRS->Finalize("Diffuse", m_pDevice.Get(), rootSignatureFlags);

		{
			//Opaque
			m_pDiffuseOpaquePSO = std::make_unique<GraphicsPipelineState>();
			m_pDiffuseOpaquePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffuseOpaquePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffuseOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffuseOpaquePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffuseOpaquePSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffuseOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			m_pDiffuseOpaquePSO->SetDepthWrite(false);
			m_pDiffuseOpaquePSO->Finalize("Diffuse (Opaque) Pipeline", m_pDevice.Get());

			//Transparant
			m_pDiffuseAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffuseOpaquePSO.get());
			m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
			m_pDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseAlphaPSO->Finalize("Diffuse (Alpha) Pipeline", m_pDevice.Get());

			//Debug version
			m_pDiffuseDebugPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffuseOpaquePSO.get());
			m_pDiffuseDebugPSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			Shader debugPixelShader = Shader("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { "DEBUG_VISUALIZE" });
			m_pDiffuseDebugPSO->SetPixelShader(debugPixelShader.GetByteCode(), debugPixelShader.GetByteCodeSize());
			m_pDiffuseDebugPSO->Finalize("Diffuse (Debug) Pipeline", m_pDevice.Get());
		}
	}

	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader vertexShader("Resources/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

			//Rootsignature
			m_pShadowsOpaqueRS = std::make_unique<RootSignature>();
			m_pShadowsOpaqueRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

			m_pShadowsOpaqueRS->Finalize("Shadow Mapping (Opaque)", m_pDevice.Get(), rootSignatureFlags);

			//Pipeline state
			m_pShadowsOpaquePSO = std::make_unique<GraphicsPipelineState>();
			m_pShadowsOpaquePSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
			m_pShadowsOpaquePSO->SetRootSignature(m_pShadowsOpaqueRS->GetRootSignature());
			m_pShadowsOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowsOpaquePSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowsOpaquePSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsOpaquePSO->SetDepthBias(-1, -5.0f, -4.0f);
			m_pShadowsOpaquePSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());
		}

		//Transparant
		{
			Shader vertexShader("Resources/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain", { "ALPHA_BLEND" });
			Shader pixelShader("Resources/DepthOnly.hlsl", Shader::Type::PixelShader, "PSMain", { "ALPHA_BLEND" });

			//Rootsignature
			m_pShadowsAlphaRS = std::make_unique<RootSignature>();
			m_pShadowsAlphaRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
			m_pShadowsAlphaRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_PIXEL);

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

			D3D12_SAMPLER_DESC samplerDesc = {};
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			m_pShadowsAlphaRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

			m_pShadowsAlphaRS->Finalize("Shadow Mapping (Transparant)", m_pDevice.Get(), rootSignatureFlags);

			//Pipeline state
			m_pShadowsAlphaPSO = std::make_unique<GraphicsPipelineState>();
			m_pShadowsAlphaPSO->SetInputLayout(depthOnlyAlphaInputElements, sizeof(depthOnlyAlphaInputElements) / sizeof(depthOnlyAlphaInputElements[0]));
			m_pShadowsAlphaPSO->SetRootSignature(m_pShadowsAlphaRS->GetRootSignature());
			m_pShadowsAlphaPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowsAlphaPSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsAlphaPSO->SetDepthBias(0, 0.0f, 0.0f);
			m_pShadowsAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		}

		m_pShadowMap = std::make_unique<Texture2D>();
		m_pShadowMap->Create(this, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1);
	}

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		m_pDepthPrepassRS->Finalize("Depth Prepass", m_pDevice.Get(), rootSignatureFlags);

		//Pipeline state
		m_pDepthPrepassPSO = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	//Depth resolve
	//Resolves a multisampled depth buffer to a normal depth buffer
	//Only required when the sample count > 1
	if(m_SampleCount > 1)
	{
		Shader computeShader("Resources/ResolveDepth.hlsl", Shader::Type::ComputeShader, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pResolveDepthRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pResolveDepthRS->Finalize("Depth Resolve", m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pResolveDepthPSO = std::make_unique<ComputePipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	//Light culling
	//Compute shader that requires depth buffer and light data to place lights into tiles
	{
		Shader computeShader("Resources/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRS = std::make_unique<RootSignature>();
		m_pComputeLightCullRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRS->Finalize("Light Culling", m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pComputeLightCullPSO = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPSO->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
		m_pComputeLightCullPSO->Finalize("Compute Light Culling Pipeline", m_pDevice.Get());

		m_pLightIndexCounter = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexCounter->Create(this, sizeof(uint32), 2);
		m_pLightIndexListBufferOpaque = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexListBufferOpaque->Create(this, sizeof(uint32), MAX_LIGHT_DENSITY);
		m_pLightIndexListBufferTransparant = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexListBufferTransparant->Create(this, sizeof(uint32), MAX_LIGHT_DENSITY);
		m_pLightBuffer = std::make_unique<StructuredBuffer>(this);
		m_pLightBuffer->Create(this, sizeof(Light), MAX_LIGHT_COUNT);
	}

	//Geometry
	{
		CopyCommandContext* pContext = static_cast<CopyCommandContext*>(AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY));
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pContext);
		pContext->Execute(true);

		for (int i = 0; i < m_pMesh->GetMeshCount(); ++i)
		{
			Batch b;
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
}

void Graphics::UpdateImGui()
{
	for (int i = 1; i < m_FrameTimes.size(); ++i)
	{
		m_FrameTimes[i - 1] = m_FrameTimes[i];
	}
	m_FrameTimes[m_FrameTimes.size() - 1] = GameTimer::DeltaTime();

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(250, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());
	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), 0, 0, 0.0f, 0.03f, ImVec2(200, 100));
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
			uint32 totalDescriptors = pAllocator->GetHeapCount() * DescriptorAllocator::DESCRIPTORS_PER_HEAP;
			uint32 usedDescriptors = pAllocator->GetNumAllocatedDescriptors();
			std::stringstream str;
			str << usedDescriptors << "/" << totalDescriptors;
			ImGui::ProgressBar((float)usedDescriptors / totalDescriptors, ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Persistent Resources", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)ResourceType::MAX; ++i)
		{
			ResourceType type = (ResourceType)i;
			switch (type)
			{
			case ResourceType::Buffer:
				ImGui::TextWrapped("Buffers");
				break;
			case ResourceType::Texture:
				ImGui::TextWrapped("Textures");
				break;
			case ResourceType::RenderTarget:
				ImGui::TextWrapped("Render Target/Depth Stencil");
				break;
			case ResourceType::MAX:
			default:
				break;
			}
			ImGui::Text("Heaps: %d", m_pPersistentAllocationManager->GetHeapCount(type));
			float totalSize = (float)m_pPersistentAllocationManager->GetTotalSize(type) / 0b100000000000000000000;
			float used = totalSize - (float)m_pPersistentAllocationManager->GetRemainingSize(type) / 0b100000000000000000000;
			std::stringstream str;
			str << used << "/" << totalSize << "MB";
			ImGui::ProgressBar((float)used / totalSize, ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}
	ImGui::End();

	static bool showOutputLog = false;
	ImGui::SetNextWindowPos(ImVec2(250, showOutputLog ? (float)m_WindowHeight - 200 : (float)m_WindowHeight - 20));
	ImGui::SetNextWindowSize(ImVec2((float)m_WindowWidth - 250, 200));
	ImGui::SetNextWindowCollapsed(!showOutputLog);

	showOutputLog = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	for (const Console::LogEntry& entry : Console::GetHistory())
	{
		switch (entry.Type)
		{
		case LogType::VeryVerbose:
		case LogType::Verbose:
		case LogType::Info:
			ImGui::TextColored(ImVec4(1, 1, 1, 1), "[Info] %s", entry.Message.c_str());
			break;
		case LogType::Warning:
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Warning] %s", entry.Message.c_str());
			break;
		case LogType::Error:
		case LogType::FatalError:
			ImGui::TextColored(ImVec4(1, 0, 0, 1), "[Error] %s", entry.Message.c_str());
			break;
		default:
			break;
		}
	}
	if (true)
	{
		ImGui::SetScrollHereY(1.0f);
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
		switch (type)
		{
		case D3D12_COMMAND_LIST_TYPE_DIRECT:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<GraphicsCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			m_CommandListPool[typeIndex].back()->SetName("Pooled Graphics Command Context");
			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<ComputeCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			m_CommandListPool[typeIndex].back()->SetName("Pooled Compute Command Context");
			break;
		case D3D12_COMMAND_LIST_TYPE_COPY:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<CopyCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			m_CommandListPool[typeIndex].back()->SetName("Pooled Copy Command Context");
			break;
		default:
			assert(false);
			break;
		}
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

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateCpuDescriptors(int count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	assert((int)type < m_DescriptorHeaps.size());
	return m_DescriptorHeaps[type]->AllocateDescriptors(count);
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
	if (heapType == D3D12_HEAP_TYPE_DEFAULT)
	{
		pResource = m_pPersistentAllocationManager->CreateResource(desc, initialState, pClearValue);
	}
	else
	{
		D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
		HR(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&pResource)));
	}
	return pResource;
}
