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
GlobalResource<PipelineState> CloudHeighDensityLUTPSO;

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
		CloudHeighDensityLUTPSO = pDevice->CreateComputePipeline(CloudShapeRS, "CloudsShapes.hlsl", "CloudHeightDensityCS");
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

	pContext->Execute(true);

	pDevice->GetShaderManager()->OnShaderRecompiledEvent().AddLambda([](Shader*, Shader*) {
		shaderDirty = true;
		});
}

RGTexture* Clouds::Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView)
{
	struct CloudParameters
	{
		float Density = 0.9f;
		int32 NoiseSeed = 0;

		float RaymarchStepSize = 15.0f;
		int32 LightMarchSteps = 8;

		int32 ShapeNoiseFrequency = 4;
		int32 ShapeNoiseResolution = 128;
		float ShapeNoiseScale = 0.3f;

		int32 DetailNoiseFrequency = 3;
		int32 DetailNoiseResolution = 32;
		float DetailNoiseScale = 3.0f;
		float DetailNoiseInfluence = 0.4f;

		float CloudType = 0.9f;
		float PlanetRadius = 60000;
		Vector2 AtmosphereHeightRange = Vector2(350.0f, 700.0f);
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
	ImGui::SliderFloat("Detail Noise Influence", &parameters.DetailNoiseInfluence, 0.0f, 1.0f);

	ImGui::SliderFloat("Raymarch Step Size", &parameters.RaymarchStepSize, 1.0f, 40.0f);
	ImGui::SliderInt("Light March Steps", &parameters.LightMarchSteps, 1, 20);
	ImGui::SliderFloat("Density", &parameters.Density, 0, 1);
	ImGui::SliderFloat("Cloud Type", &parameters.CloudType, 0, 1);

	ImGui::SliderFloat("Planet Size", &parameters.PlanetRadius, 100, 100000);
	ImGui::DragFloatRange2("Atmosphere Height", &parameters.AtmosphereHeightRange.x, &parameters.AtmosphereHeightRange.y, 1.0f, 10, 1000);
	ImGui::End();

	RGTexture* pNoiseTexture = RGUtils::CreatePersistentTexture(graph, "Shape Noise",
		TextureDesc::Create3D(parameters.ShapeNoiseResolution, parameters.ShapeNoiseResolution, parameters.ShapeNoiseResolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::None, 1, 4), &m_pShapeNoise, true);
	RGTexture* pDetailNoiseTexture = RGUtils::CreatePersistentTexture(graph, "Detail Noise",
		TextureDesc::Create3D(parameters.DetailNoiseResolution, parameters.DetailNoiseResolution, parameters.DetailNoiseResolution, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::None, 1, 4), &m_pDetailNoise, true);

	if (isDirty)
	{
		struct NoiseParams
		{
			uint32 Frequency;
			float ResolutionInv;
			uint32 Seed;
		};

		for (uint32 i = 0; i < pNoiseTexture->GetDesc().Mips; ++i)
		{
			graph.AddPass("Compute Shape Noise", RGPassFlag::Compute)
				.Write(pNoiseTexture)
				.Bind([=](CommandContext& context, const RGPassResources& resources)
					{
						uint32 resolution = pNoiseTexture->GetDesc().Width >> i;

						context.SetPipelineState(CloudShapeNoisePSO);
						context.SetComputeRootSignature(CloudShapeRS);

						NoiseParams Constants;
						Constants.Seed = parameters.NoiseSeed;
						Constants.ResolutionInv = 1.0f / resolution;
						Constants.Frequency = parameters.ShapeNoiseFrequency;

						// #hack - RG has no subresource resource view support yet :(
						RefCountPtr<UnorderedAccessView> pUAV = pNoiseTexture->Get()->GetParent()->CreateUAV(pNoiseTexture->Get(), TextureUAVDesc((uint8)i));

						context.SetRootCBV(0, Constants);
						context.BindResources(1, pUAV.Get());

						context.Dispatch(
							ComputeUtils::GetNumThreadGroups(IntVector3(resolution), IntVector3(8)));
					});
		}
		for (uint32 i = 0; i < pDetailNoiseTexture->GetDesc().Mips; ++i)
		{

			graph.AddPass("Compute Detail Noise", RGPassFlag::Compute)
				.Write(pDetailNoiseTexture)
				.Bind([=](CommandContext& context, const RGPassResources& resources)
					{
						uint32 resolution = pDetailNoiseTexture->GetDesc().Width >> i;

						context.SetPipelineState(CloudDetailNoisePSO);
						context.SetComputeRootSignature(CloudShapeRS);

						NoiseParams Constants;
						Constants.Seed = parameters.NoiseSeed;
						Constants.ResolutionInv = 1.0f / resolution;
						Constants.Frequency = parameters.DetailNoiseFrequency;

						// #hack - RG has no subresource resource view support yet :(
						RefCountPtr<UnorderedAccessView> pUAV = pDetailNoiseTexture->Get()->GetParent()->CreateUAV(pDetailNoiseTexture->Get(), TextureUAVDesc((uint8)i));

						context.SetRootCBV(0, Constants);
						context.BindResources(1, pUAV.Get());

						context.Dispatch(
							ComputeUtils::GetNumThreadGroups(IntVector3(resolution), IntVector3(8)));
					});
		}
	}

	RGTexture* pHeightGradient = graph.CreateTexture("Height Gradient", TextureDesc::Create2D(128, 128, DXGI_FORMAT_R8_UNORM));

	graph.AddPass("Height Gradient", RGPassFlag::Compute)
		.Write(pHeightGradient)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = pHeightGradient->Get();

				context.SetPipelineState(CloudHeighDensityLUTPSO);
				context.SetComputeRootSignature(CloudShapeRS);

				struct
				{
					uint32 Seed;
					float ResolutionInv;
					uint32 Frequency;
				} Constants;

				Constants.Seed = parameters.NoiseSeed;
				Constants.ResolutionInv = 1.0f / pTarget->GetWidth();
				Constants.Frequency = parameters.DetailNoiseFrequency;

				context.SetRootCBV(0, Constants);
				context.BindResources(1, pTarget->GetUAV());

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(IntVector3(pTarget->GetWidth()), IntVector3(8)));
			});

	RGTexture* pIntermediateColor = graph.CreateTexture("Intermediate Color", sceneTextures.pColorTarget->GetDesc());

	graph.AddPass("Clouds", RGPassFlag::Raster)
		.Read({ pNoiseTexture, pDetailNoiseTexture, pHeightGradient, sceneTextures.pColorTarget, sceneTextures.pDepth })
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
					float RayStepSize;
					uint32 LightMarchSteps;
					float PlanetRadius;
					float AtmosphereHeightStart;
					float AtmosphereHeightEnd;
					float DetailNoiseInfluence;
					float CloudType;
				} constants;

				constants.ShapeNoiseScale = parameters.ShapeNoiseScale;
				constants.DetailNoiseScale = parameters.DetailNoiseScale;
				constants.CloudDensity = parameters.Density;
				constants.RayStepSize = parameters.RaymarchStepSize;
				constants.LightMarchSteps = parameters.LightMarchSteps;
				constants.PlanetRadius = parameters.PlanetRadius;
				constants.AtmosphereHeightStart = parameters.AtmosphereHeightRange.x;
				constants.AtmosphereHeightEnd = parameters.AtmosphereHeightRange.y;
				constants.DetailNoiseInfluence = parameters.DetailNoiseInfluence;
				constants.CloudType = parameters.CloudType;

				context.SetRootCBV(0, constants);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pIntermediateColor->Get()));
				context.BindResources(2,
					{
						sceneTextures.pColorTarget->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						pHeightGradient->Get()->GetSRV(),
						pNoiseTexture->Get()->GetSRV(),
						pDetailNoiseTexture->Get()->GetSRV(),
					});
				context.SetVertexBuffers(VertexBufferView(m_pQuadVertexBuffer));
				context.Draw(0, 6);
				context.EndRenderPass();
			});

	sceneTextures.pColorTarget = pIntermediateColor;

	return pNoiseTexture;
}
