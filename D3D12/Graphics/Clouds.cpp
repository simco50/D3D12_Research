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

static const int Resolution = 128;
static const int MaxPoints = 1024;
static Vector4 NoiseWeights = Vector4(0.625f, 0.225f, 0.15f, 0.05f);

struct CloudParameters
{
	Vector4 NoiseWeights;
	Vector4 FrustumCorners[4];
	Matrix ViewInverse;
	float NearPlane;
	float FarPlane;

	float CloudScale = 0.004f;
	float CloudThreshold = 0.4f;
	Vector3 CloudOffset;
	float CloudDensity = 0.3f;

	Vector4 MinExtents;
	Vector4 MaxExtents;

	Vector4 SunDirection;
	Vector4 SunColor;
};

static CloudParameters sCloudParameters;

Clouds::Clouds()
{
	m_CloudBounds.Center = Vector3(0, 200, 0);
	m_CloudBounds.Extents = Vector3(300, 20, 300);
}

void Clouds::Initialize(Graphics* pGraphics)
{
	pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([this]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Clouds");
		ImGui::SliderFloat3("Position", &m_CloudBounds.Center.x, 0, 500);
		ImGui::SliderFloat3("Extents", &m_CloudBounds.Extents.x, 0, 500);
		ImGui::SliderFloat("Scale", &sCloudParameters.CloudScale, 0, 0.02f);
		ImGui::SliderFloat("Cloud Threshold", &sCloudParameters.CloudThreshold, 0, 0.5f);
		ImGui::SliderFloat("Density", &sCloudParameters.CloudDensity, 0, 1);
		ImGui::SliderFloat4("Noise Weights", &NoiseWeights.x, 0, 1);
		if (ImGui::Button("Generate Noise"))
		{
			m_UpdateNoise = true;
		}
		ImGui::End();
		}));

	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	{
		Shader shader("WorleyNoise.hlsl", ShaderType::Compute, "WorleyNoiseCS");

		m_pWorleyNoiseRS = std::make_unique<RootSignature>();
		m_pWorleyNoiseRS->FinalizeFromShader("Worley Noise RS", shader, pGraphics->GetDevice());

		m_pWorleyNoisePS = std::make_unique<PipelineState>();
		m_pWorleyNoisePS->SetComputeShader(shader);
		m_pWorleyNoisePS->SetRootSignature(m_pWorleyNoiseRS->GetRootSignature());
		m_pWorleyNoisePS->Finalize("Worley Noise PS", pGraphics->GetDevice());

		m_pWorleyNoiseTexture = std::make_unique<Texture>(pGraphics);
		m_pWorleyNoiseTexture->Create(TextureDesc::Create3D(Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
		m_pWorleyNoiseTexture->SetName("Worley Noise Texture");
	}
	{
		Shader vertexShader("Clouds.hlsl", ShaderType::Vertex, "VSMain");
		Shader pixelShader("Clouds.hlsl", ShaderType::Pixel, "PSMain");
		m_pCloudsRS = std::make_unique<RootSignature>();
		m_pCloudsRS->FinalizeFromShader("Clouds RS", vertexShader, pGraphics->GetDevice());

		D3D12_INPUT_ELEMENT_DESC quadIL[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		m_pCloudsPS = std::make_unique<PipelineState>();
		m_pCloudsPS->SetVertexShader(vertexShader);
		m_pCloudsPS->SetInputLayout(quadIL, 2);
		m_pCloudsPS->SetPixelShader(pixelShader);
		m_pCloudsPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_pCloudsPS->SetDepthEnabled(false);
		m_pCloudsPS->SetDepthWrite(false);
		m_pCloudsPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
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
	}

	m_pVerticalDensityTexture = std::make_unique<Texture>(pGraphics);
	m_pVerticalDensityTexture->Create(pContext, "Resources/Textures/CloudVerticalDensity.png");

	pContext->Execute(true);
}

void Clouds::Render(CommandContext& context, Texture* pSceneTexture, Texture* pDepthTexture, Camera* pCamera, const Light& sunLight)
{
	if (pSceneTexture->GetWidth() != m_pIntermediateColor->GetWidth() || pSceneTexture->GetHeight() != m_pIntermediateColor->GetHeight())
	{
		m_pIntermediateColor->Create(pSceneTexture->GetDesc());
	}

	if(m_UpdateNoise)
	{
		m_UpdateNoise = false;

		GPU_PROFILE_SCOPE("Compute Noise", &context);

		context.SetPipelineState(m_pWorleyNoisePS.get());
		context.SetComputeRootSignature(m_pWorleyNoiseRS.get());

		struct
		{
			Vector4 WorleyNoisePositions[MaxPoints];
			uint32 PointsPerRow[16];
			uint32 Resolution;
		} Constants;

		srand(0);
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
		Constants.PointsPerRow[3] = 18;

		Constants.PointsPerRow[0+4] = 8;
		Constants.PointsPerRow[1+4] = 10;
		Constants.PointsPerRow[2+4] = 12;
		Constants.PointsPerRow[3+4] = 18;

		Constants.PointsPerRow[0+8] = 12;
		Constants.PointsPerRow[1+8] = 14;
		Constants.PointsPerRow[2+8] = 16;
		Constants.PointsPerRow[3+8] = 20;

		Constants.PointsPerRow[0 + 12] = 14;
		Constants.PointsPerRow[1 + 12] = 15;
		Constants.PointsPerRow[2 + 12] = 19;
		Constants.PointsPerRow[3 + 12] = 26;

		context.InsertResourceBarrier(m_pWorleyNoiseTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.FlushResourceBarriers();

		context.SetComputeDynamicConstantBufferView(0, &Constants, sizeof(Constants));
		context.SetDynamicDescriptor(1, 0, m_pWorleyNoiseTexture->GetUAV());

		context.Dispatch(Resolution / 8, Resolution / 8, Resolution / 8);
	}

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

		sCloudParameters.NoiseWeights = NoiseWeights;

		sCloudParameters.FrustumCorners[0] = Vector4(-Vector3::Forward - toRight + toTop);
		sCloudParameters.FrustumCorners[1] = Vector4(-Vector3::Forward + toRight + toTop);
		sCloudParameters.FrustumCorners[2] = Vector4(-Vector3::Forward + toRight - toTop);
		sCloudParameters.FrustumCorners[3] = Vector4(-Vector3::Forward - toRight - toTop);

		sCloudParameters.ViewInverse = pCamera->GetViewInverse();
		sCloudParameters.NearPlane = pCamera->GetNear();
		sCloudParameters.FarPlane = pCamera->GetFar();
		sCloudParameters.MinExtents = Vector4(Vector3(m_CloudBounds.Center) - Vector3(m_CloudBounds.Extents));
		sCloudParameters.MaxExtents = Vector4(Vector3(m_CloudBounds.Center) + Vector3(m_CloudBounds.Extents));

		sCloudParameters.SunDirection = Vector4(sunLight.Direction);
		sCloudParameters.SunColor = sunLight.Colour;

		context.SetDynamicConstantBufferView(0, &sCloudParameters, sizeof(CloudParameters));

		context.SetDynamicDescriptor(1, 0, pSceneTexture->GetSRV());
		context.SetDynamicDescriptor(1, 1, pDepthTexture->GetSRV());
		context.SetDynamicDescriptor(1, 2, m_pWorleyNoiseTexture->GetSRV());
		context.SetDynamicDescriptor(1, 3, m_pVerticalDensityTexture->GetSRV());

		context.SetVertexBuffer(m_pQuadVertexBuffer.get());

		context.Draw(0, 6);

		context.EndRenderPass();
	}

	{
		GPU_PROFILE_SCOPE("Blit to Main Render Target", &context);
		context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_COPY_DEST);
		context.InsertResourceBarrier(m_pIntermediateColor.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.FlushResourceBarriers();
		context.CopyTexture(m_pIntermediateColor.get(), pSceneTexture);
		context.InsertResourceBarrier(pSceneTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
		context.FlushResourceBarriers();
	}
}
