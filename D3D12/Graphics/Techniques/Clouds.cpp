#include "stdafx.h"
#include "Clouds.h"
#include "Graphics/RHI/Shader.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Scene/Camera.h"

Clouds::Clouds(GraphicsDevice* pDevice)
{
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

RGTexture* Clouds::Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView)
{
	static int noiseSeed = 0;
	static Vector4 noisePersistence = Vector4(0.5f, 0.5f, 0.5f, 0.5f);
	static Vector4 noiseWeights = Vector4(0.625f, 0.225f, 0.15f, 0.05f);
	static IntVector4 noiseDimensions[]
	{
		IntVector4(4, 8, 10, 18),
		IntVector4(8, 10, 12, 18),
		IntVector4(12, 14, 16, 20),
		IntVector4(14, 15, 19, 26),
	};
	static bool inverseNoise[]
	{
		false,
		false,
		false,
		false,
	};

	static Vector3 center = Vector3(0, 10, 0);
	static Vector3 extents = Vector3(10, 4, 10);
	static float scale = 0.05f;
	static float threshold = 0.4f;
	static float density = 0.4f;
	static Vector3 offset = Vector3::Zero;

	bool isDirty = !m_pWorleyNoiseTexture;

	ImGui::Begin("Parameters");
	ImGui::Text("Noise");
	isDirty |= ImGui::SliderInt("Noise Seed", &noiseSeed, 0, 500);
	isDirty |= ImGui::SliderFloat4("Noise Persistence", &noisePersistence.x, 0, 1);

	for (uint32 i = 0; i < ARRAYSIZE(noiseDimensions); ++i)
	{
		isDirty |= ImGui::Checkbox(Sprintf("Invert %d", i).c_str(), &inverseNoise[i]);
		ImGui::SameLine();
		isDirty |= ImGui::SliderInt4(Sprintf("Dimensions %d", i).c_str(), &noiseDimensions[i].x, 4, 40);
	}

	ImGui::Text("Clouds");
	ImGui::InputFloat3("Position", &center.x);
	ImGui::InputFloat3("Extents", &extents.x);
	ImGui::InputFloat3("Offset", &offset.x);
	ImGui::SliderFloat("Scale", &scale, 0.05f, 0.5f);
	ImGui::SliderFloat("Cloud Threshold", &threshold, 0.0f, 1.0f);
	ImGui::SliderFloat("Density", &density, 0, 1);
	ImGui::SliderFloat4("Noise Weights", &noiseWeights.x, -1, 1);
	ImGui::End();

	static const int Resolution = 128;
	RGTexture* pNoiseTexture = RGUtils::CreatePersistentTexture(graph, "Worley Noise", TextureDesc::Create3D(Resolution, Resolution, Resolution, DXGI_FORMAT_R8G8B8A8_UNORM), &m_pWorleyNoiseTexture, true);

	if (isDirty)
	{
		graph.AddPass("Compute Noise", RGPassFlag::Compute)
			.Write(pNoiseTexture)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.SetPipelineState(m_pWorleyNoisePS);
					context.SetComputeRootSignature(m_pWorleyNoiseRS);

					struct
					{
						IntVector4 PointsPerRow[4];
						IntVector4 Invert;
						Vector4 Persistence;
						uint32 Resolution;
						uint32 Seed;
					} Constants;

					Constants.Seed = noiseSeed;
					Constants.Resolution = Resolution;
					for (uint32 i = 0; i < ARRAYSIZE(noiseDimensions); ++i)
					{
						Constants.PointsPerRow[i] = noiseDimensions[i];
					}
					Constants.Invert = IntVector4(inverseNoise[0], inverseNoise[1], inverseNoise[2], inverseNoise[3]);
					Constants.Persistence = noisePersistence;

					context.SetRootCBV(0, Constants);
					context.BindResources(1, pNoiseTexture->Get()->GetUAV());

					context.Dispatch(
						ComputeUtils::GetNumThreadGroups(Resolution, 8, Resolution, 8, Resolution, 8));
				});
	}


	RGTexture* pIntermediateColor = graph.CreateTexture("Intermediate Color", sceneTextures.pColorTarget->GetDesc());

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

				struct
				{
					Vector4 NoiseWeights;
					Vector4 MinExtents;
					Vector4 MaxExtents;

					Vector3 CloudOffset;

					float CloudScale;
					float CloudThreshold;
					float CloudDensity;
				} parameters;

				parameters.NoiseWeights = noiseWeights;
				parameters.MinExtents = Vector4(center - extents);
				parameters.MaxExtents = Vector4(center + extents);
				parameters.CloudOffset = offset;
				parameters.CloudScale = scale;
				parameters.CloudThreshold = threshold;
				parameters.CloudDensity = density;

				context.SetRootCBV(0, parameters);
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

	sceneTextures.pColorTarget = pIntermediateColor;

	return pNoiseTexture;
}
