#include "stdafx.h"
#include "Clouds.h"
#include "Graphics/RHI/Shader.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RenderGraph/RenderGraph.h"
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
	Vector4 MinExtents;
	Vector4 MaxExtents;

	Vector3 CloudOffset;

	float CloudScale = 0.004f;
	float CloudThreshold = 0.4f;
	float CloudDensity = 0.3f;
};

static CloudParameters sCloudParameters;

Clouds::Clouds(GraphicsDevice* pDevice)
{
	m_CloudBounds.Center = Vector3(0, 200, 0);
	m_CloudBounds.Extents = Vector3(300, 20, 300);

	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	{
		m_pWorleyNoiseRS = new RootSignature(pDevice);
		m_pWorleyNoiseRS->AddConstantBufferView(0);
		m_pWorleyNoiseRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
		m_pWorleyNoiseRS->Finalize("Worley Noise RS");

		m_pWorleyNoisePS = pDevice->CreateComputePipeline(m_pWorleyNoiseRS, "WorleyNoise.hlsl", "WorleyNoiseCS");
	}
	{
		m_pCloudsRS = new RootSignature(pDevice);
		m_pCloudsRS->AddConstantBufferView(0);
		m_pCloudsRS->AddConstantBufferView(100);
		m_pCloudsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4);
		m_pCloudsRS->Finalize("Clouds RS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		VertexElementLayout quadIL;
		quadIL.AddVertexElement("POSITION", DXGI_FORMAT_R32G32B32_FLOAT);
		quadIL.AddVertexElement("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCloudsRS);
		psoDesc.SetVertexShader("Clouds.hlsl", "VSMain");
		psoDesc.SetPixelShader("Clouds.hlsl", "PSMain");
		psoDesc.SetInputLayout(quadIL);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetRenderTargetFormats(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, 1);
		m_pCloudsPS = pDevice->CreatePipeline(psoDesc);
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

		m_pQuadVertexBuffer = pDevice->CreateBuffer(BufferDesc::CreateVertexBuffer(6, sizeof(Vertex)), "Quad Vertex Buffer");
		pContext->WriteBuffer(m_pQuadVertexBuffer, vertices, ARRAYSIZE(vertices) * sizeof(Vertex));
	}

	m_pVerticalDensityTexture = GraphicsCommon::CreateTextureFromFile(*pContext, "Resources/Textures/CloudVerticalDensity.png", false);

	pContext->Execute(true);
}

void Clouds::Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView)
{
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

	RGTexture* pIntermediateColor = graph.CreateTexture("Intermediate Color", sceneTextures.pColorTarget->GetDesc());
	RGTexture* pNoiseTexture = RGUtils::CreatePersistentTexture(graph, "Worley Noise", TextureDesc::Create3D(Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM), &m_pWorleyNoiseTexture, true);

	if(m_UpdateNoise)
	{
		m_UpdateNoise = false;

		graph.AddPass("Compute Noise", RGPassFlag::Compute)
			.Write(pNoiseTexture)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.SetPipelineState(m_pWorleyNoisePS);
					context.SetComputeRootSignature(m_pWorleyNoiseRS);

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

					Constants.PointsPerRow[0 + 4] = 8;
					Constants.PointsPerRow[1 + 4] = 10;
					Constants.PointsPerRow[2 + 4] = 12;
					Constants.PointsPerRow[3 + 4] = 18;

					Constants.PointsPerRow[0 + 8] = 12;
					Constants.PointsPerRow[1 + 8] = 14;
					Constants.PointsPerRow[2 + 8] = 16;
					Constants.PointsPerRow[3 + 8] = 20;

					Constants.PointsPerRow[0 + 12] = 14;
					Constants.PointsPerRow[1 + 12] = 15;
					Constants.PointsPerRow[2 + 12] = 19;
					Constants.PointsPerRow[3 + 12] = 26;

					context.SetRootCBV(0, Constants);
					context.BindResources(1, pNoiseTexture->Get()->GetUAV());

					context.Dispatch(Resolution / 8, Resolution / 8, Resolution / 8);
				});
	}

	float fov = pView->View.FoV;
	float aspect = (float)pView->GetDimensions().x / pView->GetDimensions().y;
	float halfFoV = fov * 0.5f;
	float tanFoV = tan(halfFoV);
	Vector3 toRight = Vector3::Right * tanFoV * aspect;
	Vector3 toTop = Vector3::Up * tanFoV;

	sCloudParameters.FrustumCorners[0] = Vector4(-Vector3::Forward - toRight + toTop);
	sCloudParameters.FrustumCorners[1] = Vector4(-Vector3::Forward + toRight + toTop);
	sCloudParameters.FrustumCorners[2] = Vector4(-Vector3::Forward + toRight - toTop);
	sCloudParameters.FrustumCorners[3] = Vector4(-Vector3::Forward - toRight - toTop);
	sCloudParameters.NoiseWeights = NoiseWeights;
	sCloudParameters.MinExtents = Vector4(Vector3(m_CloudBounds.Center) - Vector3(m_CloudBounds.Extents));
	sCloudParameters.MaxExtents = Vector4(Vector3(m_CloudBounds.Center) + Vector3(m_CloudBounds.Extents));

	{
		graph.AddPass("Clouds", RGPassFlag::Compute)
			.Read({ pNoiseTexture, sceneTextures.pColorTarget, sceneTextures.pDepth })
			.RenderTarget(pIntermediateColor, RenderTargetLoadAction::Load)
			.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.BeginRenderPass(resources.GetRenderPassInfo());
					context.SetPipelineState(m_pCloudsPS);
					context.SetGraphicsRootSignature(m_pCloudsRS);
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.SetRootCBV(0, sCloudParameters);
					context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pIntermediateColor->Get()));
					context.BindResources(2,
						{
							sceneTextures.pColorTarget->Get()->GetSRV(),
							sceneTextures.pDepth->Get()->GetSRV(),
							pNoiseTexture->Get()->GetSRV(),
							m_pVerticalDensityTexture->GetSRV(),
						});
					context.SetVertexBuffers(VertexBufferView(m_pQuadVertexBuffer));
					context.Draw(0, 6);
					context.EndRenderPass();
				});
	}

	sceneTextures.pColorTarget = pIntermediateColor;
}
