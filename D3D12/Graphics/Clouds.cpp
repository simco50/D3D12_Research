#include "stdafx.h"
#include "Clouds.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Graphics.h"
#include "Texture.h"
#include "CommandContext.h"
#include "Profiler.h"
#include "Scene/Camera.h"

static const int Resolution = 256;
static const int MaxPoints = 256;

void Clouds::Initialize(Graphics* pGraphics)
{
	{
		Shader shader("Resources/Shaders/WorleyNoise.hlsl", Shader::Type::ComputeShader, "WorleyNoiseCS");

		m_pWorleyNoiseRS = std::make_unique<RootSignature>();
		m_pWorleyNoiseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->Finalize("Worley Noise RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pWorleyNoisePS = std::make_unique<ComputePipelineState>();
		m_pWorleyNoisePS->SetComputeShader(shader.GetByteCode(), shader.GetByteCodeSize());
		m_pWorleyNoisePS->SetRootSignature(m_pWorleyNoiseRS->GetRootSignature());
		m_pWorleyNoisePS->Finalize("Worley Noise PS", pGraphics->GetDevice());

		m_pWorleyNoiseTexture = std::make_unique<Texture3D>();
		m_pWorleyNoiseTexture->Create(pGraphics, Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureUsage::UnorderedAccess | TextureUsage::ShaderResource);
		m_pWorleyNoiseTexture->SetName("Worley Noise Texture");
	}
	{
		Shader vertexShader("Resources/Shaders/Clouds.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/Clouds.hlsl", Shader::Type::PixelShader, "PSMain");
		m_pCloudsRS = std::make_unique<RootSignature>();
		m_pCloudsRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pCloudsRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		m_pCloudsRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		m_pCloudsRS->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		
		m_pCloudsRS->Finalize("Clouds RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		
		D3D12_INPUT_ELEMENT_DESC quadIL[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		m_pCloudsPS = std::make_unique<GraphicsPipelineState>();
		m_pCloudsPS->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pCloudsPS->SetInputLayout(quadIL, 2);
		m_pCloudsPS->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pCloudsPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_pCloudsPS->SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
		m_pCloudsPS->SetDepthWrite(false);
		m_pCloudsPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, 1, 0);
		m_pCloudsPS->SetRootSignature(m_pCloudsRS->GetRootSignature());
		m_pCloudsPS->Finalize("Clouds PS", pGraphics->GetDevice());
	}
	{
		struct Vertex
		{
			Vector3 Position;
			Vector2 TexCoord;
		};
		Vertex vertices[] = {
			{ Vector3(-1, 1, 0), Vector2(0, 0) },
			{ Vector3(1, 1, 1), Vector2(1, 0) },
			{ Vector3(-1, -1, 3), Vector2(0, 1) },
			{ Vector3(-1, -1, 3), Vector2(0, 1) },
			{ Vector3(1, 1, 1), Vector2(1, 0) },
			{ Vector3(1, -1, 2), Vector2(1, 1) },
		};

		GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		m_pQuadVertexBuffer = std::make_unique<VertexBuffer>();
		m_pQuadVertexBuffer->Create(pGraphics, 6, sizeof(Vertex), false);
		m_pQuadVertexBuffer->SetData(pContext, vertices, sizeof(Vertex) * 6);
		pContext->Execute(true);

		m_pIntermediateColor = std::make_unique<Texture2D>();
		m_pIntermediateColor->Create(pGraphics, pGraphics->GetWindowWidth(), pGraphics->GetWindowHeight(), Graphics::RENDER_TARGET_FORMAT, TextureUsage::RenderTarget | TextureUsage::ShaderResource, 1, -1, ClearBinding(Color(1, 0, 0, 1)));
		m_pIntermediateDepth = std::make_unique<Texture2D>();
		m_pIntermediateDepth->Create(pGraphics, pGraphics->GetWindowWidth(), pGraphics->GetWindowHeight(), Graphics::DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1, -1, ClearBinding(1.0f, 0));
	}

	{
		GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("Clouds_Render", pContext);

		pContext->SetComputePipelineState(m_pWorleyNoisePS.get());
		pContext->SetComputeRootSignature(m_pWorleyNoiseRS.get());

		struct
		{
			Vector4 WorleyNoisePositions[MaxPoints];
			uint32 PointsPerRow[4];
			uint32 Resolution;
		} Constants;

		for (int i = 0; i < MaxPoints; ++i)
		{
			Constants.WorleyNoisePositions[i].x = Math::RandomRange(0.0f, 1.0f);
			Constants.WorleyNoisePositions[i].y = Math::RandomRange(0.0f, 1.0f);
			Constants.WorleyNoisePositions[i].z = Math::RandomRange(0.0f, 1.0f);
		}
		Constants.Resolution = Resolution;
		Constants.PointsPerRow[0] = 4;
		Constants.PointsPerRow[1] = 8;
		Constants.PointsPerRow[2] = 10;
		Constants.PointsPerRow[3] = 12;

		pContext->InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pContext->FlushResourceBarriers();

		pContext->SetComputeDynamicConstantBufferView(0, &Constants, sizeof(Constants));
		pContext->SetDynamicDescriptor(1, 0, m_pWorleyNoiseTexture->GetUAV());

		pContext->Dispatch(Resolution / 8, Resolution / 8, Resolution / 8);
		Profiler::Instance()->End();
		pContext->Execute(true);
	}
}

struct CloudParameters
{
	Vector4 FrustumCorners[4];
	Matrix ViewInverse;
	float NearPlane;
	float FarPlane;

	float CloudScale = 0.02f;
	float CloudThreshold = 0.4f;
	Vector3 CloudOffset;
	float CloudDensity = 0.7f;
};

static CloudParameters sCloudParameters;

void Clouds::RenderUI()
{
	ImGui::Begin("Clouds");
	ImGui::SliderFloat("Scale", &sCloudParameters.CloudScale, 0, 0.02f);
	ImGui::SliderFloat("Threshold", &sCloudParameters.CloudThreshold, 0, 0.5f);
	ImGui::SliderFloat("Density", &sCloudParameters.CloudDensity, 0, 1);
	ImGui::SliderFloat3("Offset", &sCloudParameters.CloudOffset.x, -1, 1);
	ImGui::End();
}

void Clouds::Render(Graphics* pGraphics, Texture2D* pSceneTexture, Texture2D* pDepthTexture)
{
	GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
	{
		Profiler::Instance()->Begin("Clouds", pContext);
		pContext->InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		pContext->InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		pContext->InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		pContext->InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
		pContext->FlushResourceBarriers();

		pContext->SetViewport(FloatRect(0, 0, (float)pGraphics->GetWindowWidth(), (float)pGraphics->GetWindowHeight()));
		pContext->SetScissorRect(FloatRect(0, 0, (float)pGraphics->GetWindowWidth(), (float)pGraphics->GetWindowHeight()));

		pContext->BeginRenderPass(RenderPassInfo(m_pIntermediateColor.get(), RenderPassAccess::DontCare_Store, m_pIntermediateDepth.get(), RenderPassAccess::Clear_Store));
		
		pContext->SetGraphicsPipelineState(m_pCloudsPS.get());
		pContext->SetGraphicsRootSignature(m_pCloudsRS.get());
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		float fov = pGraphics->GetCamera()->GetFoV();
		float aspect = (float)pGraphics->GetWindowWidth() / pGraphics->GetWindowHeight();
		float halfFoV = fov * 0.5f;
		float tanFoV = tan(halfFoV);
		Vector3 toRight = Vector3::Right * tanFoV * aspect;
		Vector3 toTop = Vector3::Up * tanFoV;

		sCloudParameters.FrustumCorners[0] = Vector4(-Vector3::Forward - toRight + toTop);
		sCloudParameters.FrustumCorners[1] = Vector4(-Vector3::Forward + toRight + toTop);
		sCloudParameters.FrustumCorners[2] = Vector4(-Vector3::Forward + toRight - toTop);
		sCloudParameters.FrustumCorners[3] = Vector4(-Vector3::Forward - toRight - toTop);

		sCloudParameters.ViewInverse = pGraphics->GetCamera()->GetViewInverse();
		sCloudParameters.NearPlane = pGraphics->GetCamera()->GetNear();
		sCloudParameters.FarPlane = pGraphics->GetCamera()->GetFar();

		pContext->SetDynamicConstantBufferView(0, &sCloudParameters, sizeof(CloudParameters));

		pContext->SetDynamicDescriptor(1, 0, pSceneTexture->GetSRV());
		pContext->SetDynamicDescriptor(1, 1, pDepthTexture->GetSRV());
		pContext->SetDynamicDescriptor(1, 2, m_pWorleyNoiseTexture->GetSRV());

		pContext->SetVertexBuffer(m_pQuadVertexBuffer.get());

		pContext->Draw(0, 6);

		pContext->EndRenderPass();

		Profiler::Instance()->End(pContext);
	}

	{
		Profiler::Instance()->Begin("Blit to Main Render Target", pContext);
		pContext->InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_COPY_DEST);
		pContext->InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		pContext->FlushResourceBarriers();
		pContext->CopyResource(m_pIntermediateColor.get(), pSceneTexture);
		Profiler::Instance()->End(pContext);
	}
	pContext->Execute(false);
}
