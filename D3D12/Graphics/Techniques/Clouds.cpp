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

static bool shaderDirty = false;

GlobalResource<RootSignature> CloudShapeRS;
GlobalResource<PipelineState> CloudShapeNoisePSO;
GlobalResource<PipelineState> CloudDetailNoisePSO;

GlobalResource<RootSignature> CloudsRS;
GlobalResource<PipelineState> CloudsPSO;

Clouds::Clouds(GraphicsDevice* pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	{
		CloudShapeRS = new RootSignature(pDevice);
		CloudShapeRS->AddConstantBufferView(0);
		CloudShapeRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
		CloudShapeRS->Finalize("Worley Noise RS");

		CloudShapeNoisePSO = pDevice->CreateComputePipeline(CloudShapeRS, "CloudsShapes.hlsl", "CloudShapeNoiseCS");
		CloudDetailNoisePSO = pDevice->CreateComputePipeline(CloudShapeRS, "CloudsShapes.hlsl", "CloudDetailNoiseCS");
	}
	{
		CloudsRS = new RootSignature(pDevice);
		CloudsRS->AddConstantBufferView(0);
		CloudsRS->AddConstantBufferView(100);
		CloudsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
		CloudsRS->Finalize("Clouds RS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		VertexElementLayout quadIL;
		quadIL.AddVertexElement("POSITION", DXGI_FORMAT_R32G32B32_FLOAT);
		quadIL.AddVertexElement("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(CloudsRS);
		psoDesc.SetVertexShader("Clouds.hlsl", "VSMain");
		psoDesc.SetPixelShader("Clouds.hlsl", "PSMain");
		psoDesc.SetInputLayout(quadIL);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetRenderTargetFormats(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, 1);
		CloudsPSO = pDevice->CreatePipeline(psoDesc);
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

	pDevice->GetShaderManager()->OnShaderRecompiledEvent().AddLambda([](Shader*, Shader*) {
		shaderDirty = true;
		});
}

RGTexture* Clouds::Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView)
{
	struct CloudParameters
	{
		float Density = 0.5f;
		int32 NoiseSeed = 0;

		int32 ShapeNoiseFrequency = 4;
		int32 ShapeNoiseResolution = 128;
		float ShapeNoiseScale = 0.6f;

		int32 DetailNoiseFrequency = 3;
		int32 DetailNoiseResolution = 32;
		float DetailNoiseScale = 7.0f;
	};
	static CloudParameters parameters;

	bool isDirty = !m_pShapeNoise || !m_pDetailNoise || shaderDirty;
	shaderDirty = false;

	ImGui::Begin("Parameters");
	isDirty |= ImGui::SliderInt("Seed", &parameters.NoiseSeed, 0, 100);
	isDirty |= ImGui::SliderInt("Shape Noise Frequency", &parameters.ShapeNoiseFrequency, 1, 10);
	isDirty |= ImGui::SliderInt("Shape Noise Resolution", &parameters.ShapeNoiseResolution, 32, 256);
	ImGui::SliderFloat("Shape Noise Scale", &parameters.ShapeNoiseScale, 0.1f, 5.0f);

	isDirty |= ImGui::SliderInt("Detail Noise Frequency", &parameters.DetailNoiseFrequency, 1, 10);
	isDirty |= ImGui::SliderInt("Detail Noise Resolution", &parameters.DetailNoiseResolution, 8, 64);
	ImGui::SliderFloat("Detail Noise Scale", &parameters.DetailNoiseScale, 2.0f, 12.0f);

	ImGui::SliderFloat("Density", &parameters.Density, 0, 1);
	ImGui::End();

	RGTexture* pNoiseTexture = RGUtils::CreatePersistentTexture(graph, "Shape Noise",
		TextureDesc::Create3D(parameters.ShapeNoiseResolution, parameters.ShapeNoiseResolution, parameters.ShapeNoiseResolution, DXGI_FORMAT_R8G8B8A8_UNORM), &m_pShapeNoise, true);
	RGTexture* pDetailNoiseTexture = RGUtils::CreatePersistentTexture(graph, "Detail Noise",
		TextureDesc::Create3D(parameters.DetailNoiseResolution, parameters.DetailNoiseResolution, parameters.DetailNoiseResolution, DXGI_FORMAT_R8G8B8A8_UNORM), &m_pDetailNoise, true);

	if (isDirty)
	{
		struct NoiseParams
		{
			uint32 Frequency;
			float ResolutionInv;
			uint32 Seed;
		};

		graph.AddPass("Compute Shape Noise", RGPassFlag::Compute)
			.Write(pNoiseTexture)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.SetPipelineState(CloudShapeNoisePSO);
					context.SetComputeRootSignature(CloudShapeRS);

					NoiseParams Constants;
					Constants.Seed = parameters.NoiseSeed;
					Constants.ResolutionInv = 1.0f / parameters.ShapeNoiseResolution;
					Constants.Frequency = parameters.ShapeNoiseFrequency;

					context.SetRootCBV(0, Constants);
					context.BindResources(1, pNoiseTexture->Get()->GetUAV());

					context.Dispatch(
						ComputeUtils::GetNumThreadGroups(parameters.ShapeNoiseResolution, 8, parameters.ShapeNoiseResolution, 8, parameters.ShapeNoiseResolution, 8));
				});

		graph.AddPass("Compute Detail Noise", RGPassFlag::Compute)
			.Write(pDetailNoiseTexture)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					context.SetPipelineState(CloudDetailNoisePSO);
					context.SetComputeRootSignature(CloudShapeRS);

					NoiseParams Constants;
					Constants.Seed = parameters.NoiseSeed;
					Constants.ResolutionInv = 1.0f / parameters.DetailNoiseResolution;
					Constants.Frequency = parameters.DetailNoiseFrequency;

					context.SetRootCBV(0, Constants);
					context.BindResources(1, pDetailNoiseTexture->Get()->GetUAV());

					context.Dispatch(
						ComputeUtils::GetNumThreadGroups(parameters.DetailNoiseResolution, 8, parameters.DetailNoiseResolution, 8, parameters.DetailNoiseResolution, 8));
				});
	}

	RGTexture* pIntermediateColor = graph.CreateTexture("Intermediate Color", sceneTextures.pColorTarget->GetDesc());

	graph.AddPass("Clouds", RGPassFlag::Raster)
		.Read({ pNoiseTexture, pDetailNoiseTexture, sceneTextures.pColorTarget, sceneTextures.pDepth })
		.RenderTarget(pIntermediateColor, RenderTargetLoadAction::Load)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.BeginRenderPass(resources.GetRenderPassInfo());
				context.SetPipelineState(CloudsPSO);
				context.SetGraphicsRootSignature(CloudsRS);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				struct
				{
					float ShapeNoiseScale;
					float DetailNoiseScale;
					float CloudDensity;
				} constants;

				constants.ShapeNoiseScale = parameters.ShapeNoiseScale;
				constants.DetailNoiseScale = parameters.DetailNoiseScale;
				constants.CloudDensity = parameters.Density;

				context.SetRootCBV(0, constants);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pIntermediateColor->Get()));
				context.BindResources(2,
					{
						sceneTextures.pColorTarget->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						m_pVerticalDensityTexture->GetSRV(),
						pNoiseTexture->Get()->GetSRV(),
						pDetailNoiseTexture->Get()->GetSRV(),
					});
				context.SetVertexBuffers(VertexBufferView(m_pQuadVertexBuffer));
				context.Draw(0, 6);
				context.EndRenderPass();
			});

	sceneTextures.pColorTarget = pIntermediateColor;

	return pDetailNoiseTexture;
}
