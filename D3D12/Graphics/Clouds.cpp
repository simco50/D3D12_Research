#include "stdafx.h"
#include "Clouds.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/CommandContext.h"
#include "Profiler.h"
#include "Scene/Camera.h"
#include "ImGuiRenderer.h"

static const int Resolution = 256;
static const int MaxPoints = 256;


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

void Clouds::Initialize(Graphics* pGraphics)
{
	pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Clouds");
		ImGui::SliderFloat("Scale", &sCloudParameters.CloudScale, 0, 0.02f);
		ImGui::SliderFloat("Threshold", &sCloudParameters.CloudThreshold, 0, 0.5f);
		ImGui::SliderFloat("Density", &sCloudParameters.CloudDensity, 0, 1);
		ImGui::SliderFloat3("Offset", &sCloudParameters.CloudOffset.x, -1, 1);
		ImGui::End();
		}));

	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	{
		Shader shader("Resources/Shaders/WorleyNoise.hlsl", Shader::Type::Compute, "WorleyNoiseCS");

		m_pWorleyNoiseRS = std::make_unique<RootSignature>();
		m_pWorleyNoiseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pWorleyNoiseRS->Finalize("Worley Noise RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pWorleyNoisePS = std::make_unique<PipelineState>();
		m_pWorleyNoisePS->SetComputeShader(shader.GetByteCode(), shader.GetByteCodeSize());
		m_pWorleyNoisePS->SetRootSignature(m_pWorleyNoiseRS->GetRootSignature());
		m_pWorleyNoisePS->Finalize("Worley Noise PS", pGraphics->GetDevice());

		m_pWorleyNoiseTexture = std::make_unique<Texture>(pGraphics);
		m_pWorleyNoiseTexture->Create(TextureDesc::Create3D(Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
		m_pWorleyNoiseTexture->SetName("Worley Noise Texture");
	}
	{
		Shader vertexShader("Resources/Shaders/Clouds.hlsl", Shader::Type::Vertex, "VSMain");
		Shader pixelShader("Resources/Shaders/Clouds.hlsl", Shader::Type::Pixel, "PSMain");
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

		m_pCloudsPS = std::make_unique<PipelineState>();
		m_pCloudsPS->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pCloudsPS->SetInputLayout(quadIL, 2);
		m_pCloudsPS->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pCloudsPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_pCloudsPS->SetDepthEnabled(false);
		m_pCloudsPS->SetDepthWrite(false);
		m_pCloudsPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount(), pGraphics->GetMultiSampleQualityLevel(pGraphics->GetMultiSampleCount()));
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

		m_pQuadVertexBuffer = std::make_unique<Buffer>(pGraphics);
		m_pQuadVertexBuffer->Create(BufferDesc::CreateVertexBuffer(6, sizeof(Vertex)));
		m_pQuadVertexBuffer->SetData(pContext, vertices, sizeof(Vertex) * 6);

		m_pIntermediateColor = std::make_unique<Texture>(pGraphics);
		m_pIntermediateColor->Create(TextureDesc::CreateRenderTarget(pGraphics->GetWindowWidth(), pGraphics->GetWindowHeight(), Graphics::RENDER_TARGET_FORMAT, TextureFlag::RenderTarget | TextureFlag::ShaderResource, pGraphics->GetMultiSampleCount()));
	}

	{
		GPU_PROFILE_SCOPE("Compute Clouds", pContext);

		pContext->SetPipelineState(m_pWorleyNoisePS.get());
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
	}
	pContext->Execute(true);
}

void Clouds::Render(CommandContext& context, Texture* pSceneTexture, Texture* pDepthTexture, Camera* pCamera)
{
	{
		GPU_PROFILE_SCOPE("Clouds", &context);
		context.InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		context.InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
		context.FlushResourceBarriers();

		context.SetViewport(FloatRect(0, 0, (float)pSceneTexture->GetWidth(), (float)pSceneTexture->GetHeight()));
		context.SetScissorRect(FloatRect(0, 0, (float)pSceneTexture->GetWidth(), (float)pSceneTexture->GetHeight()));

		context.BeginRenderPass(RenderPassInfo(m_pIntermediateColor.get(), RenderPassAccess::DontCare_Store, nullptr, RenderPassAccess::NoAccess));

		context.SetPipelineState(m_pCloudsPS.get());
		context.SetGraphicsRootSignature(m_pCloudsRS.get());
		context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		float fov = pCamera->GetFoV();
		float aspect = (float)pSceneTexture->GetWidth() / pSceneTexture->GetHeight();
		float halfFoV = fov * 0.5f;
		float tanFoV = tan(halfFoV);
		Vector3 toRight = Vector3::Right * tanFoV * aspect;
		Vector3 toTop = Vector3::Up * tanFoV;

		sCloudParameters.FrustumCorners[0] = Vector4(-Vector3::Forward - toRight + toTop);
		sCloudParameters.FrustumCorners[1] = Vector4(-Vector3::Forward + toRight + toTop);
		sCloudParameters.FrustumCorners[2] = Vector4(-Vector3::Forward + toRight - toTop);
		sCloudParameters.FrustumCorners[3] = Vector4(-Vector3::Forward - toRight - toTop);

		sCloudParameters.ViewInverse = pCamera->GetViewInverse();
		sCloudParameters.NearPlane = pCamera->GetNear();
		sCloudParameters.FarPlane = pCamera->GetFar();

		context.SetDynamicConstantBufferView(0, &sCloudParameters, sizeof(CloudParameters));

		context.SetDynamicDescriptor(1, 0, pSceneTexture->GetSRV());
		context.SetDynamicDescriptor(1, 1, pDepthTexture->GetSRV());
		context.SetDynamicDescriptor(1, 2, m_pWorleyNoiseTexture->GetSRV());

		context.SetVertexBuffer(m_pQuadVertexBuffer.get());

		context.Draw(0, 6);

		context.EndRenderPass();
	}

	{
		GPU_PROFILE_SCOPE("Blit to Main Render Target", &context);
		context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_COPY_DEST);
		context.InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.FlushResourceBarriers();
		context.CopyResource(m_pIntermediateColor.get(), pSceneTexture);
	}
}
