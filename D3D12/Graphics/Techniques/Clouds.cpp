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

GlobalResource<PipelineState> CloudShapeNoisePSO;
GlobalResource<PipelineState> CloudDetailNoisePSO;
GlobalResource<PipelineState> CloudHeighDensityLUTPSO;

GlobalResource<RootSignature> CloudsRS;
GlobalResource<PipelineState> CloudsPSO;

Clouds::Clouds(GraphicsDevice* pDevice)
{
	CloudsRS = new RootSignature(pDevice);
	CloudsRS->AddConstantBufferView(0);
	CloudsRS->AddConstantBufferView(100);
	CloudsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
	CloudsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5);
	CloudsRS->Finalize("Clouds RS");

	const char* pCloudShapesShader = "CloudsShapes.hlsl";
	CloudShapeNoisePSO		= pDevice->CreateComputePipeline(CloudsRS, pCloudShapesShader, "CloudShapeNoiseCS");
	CloudDetailNoisePSO		= pDevice->CreateComputePipeline(CloudsRS, pCloudShapesShader, "CloudDetailNoiseCS");
	CloudHeighDensityLUTPSO = pDevice->CreateComputePipeline(CloudsRS, pCloudShapesShader, "CloudHeightDensityCS");

	CloudsPSO = pDevice->CreateComputePipeline(CloudsRS, "Clouds.hlsl", "CSMain");

	pDevice->GetShaderManager()->OnShaderRecompiledEvent().AddLambda([this](Shader*) { m_pShapeNoise = nullptr; });
}

RGTexture* Clouds::Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView)
{
	struct CloudParameters
	{
		int32 NoiseSeed = 0;
		float GlobalScale = 0.001f;
		float GlobalDensity = 0.1f;

		float RaymarchStepSize = 15.0f;
		int32 LightMarchSteps = 6;

		int32 ShapeNoiseFrequency = 4;
		int32 ShapeNoiseResolution = 128;
		float ShapeNoiseScale = 0.3f;

		int32 DetailNoiseFrequency = 3;
		int32 DetailNoiseResolution = 32;
		float DetailNoiseScale = 3.0f;
		float DetailNoiseInfluence = 0.4f;

		float WindAngle = 0;
		float WindSpeed = 0.03f;
		float CloudTopSkew = 10.0f;

		float Coverage = 0.9f;
		float CloudType = 0.5f;
		float PlanetRadius = 60000;
		Vector2 AtmosphereHeightRange = Vector2(200.0f, 900.0f);
	};
	static CloudParameters parameters;

	bool isDirty = !m_pShapeNoise || !m_pDetailNoise || !m_pCloudHeightDensityLUT;

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Clouds"))
		{
			isDirty |= ImGui::SliderInt("Seed", &parameters.NoiseSeed, 0, 100);
			isDirty |= ImGui::SliderInt("Shape Noise Frequency", &parameters.ShapeNoiseFrequency, 1, 10);
			isDirty |= ImGui::SliderInt("Shape Noise Resolution", &parameters.ShapeNoiseResolution, 32, 256);
			ImGui::SliderFloat("Shape Noise Scale", &parameters.ShapeNoiseScale, 0.1f, 5.0f);

			isDirty |= ImGui::SliderInt("Detail Noise Frequency", &parameters.DetailNoiseFrequency, 1, 10);
			isDirty |= ImGui::SliderInt("Detail Noise Resolution", &parameters.DetailNoiseResolution, 8, 64);
			ImGui::SliderFloat("Detail Noise Scale", &parameters.DetailNoiseScale, 2.0f, 12.0f);
			ImGui::SliderFloat("Detail Noise Influence", &parameters.DetailNoiseInfluence, 0.0f, 1.0f);

			ImGui::SliderFloat("Global Scale", &parameters.GlobalScale, 0.01f, 0.0005f);
			ImGui::SliderFloat("Global Density", &parameters.GlobalDensity, 0.0f, 1.0f);
			ImGui::SliderAngle("Wind Direction", &parameters.WindAngle);
			ImGui::SliderFloat("Wind Speed", &parameters.WindSpeed, 0, 1.0f);
			ImGui::SliderFloat("Cloud Top Skew", &parameters.CloudTopSkew, 0, 100.0f);

			ImGui::SliderFloat("Raymarch Step Size", &parameters.RaymarchStepSize, 1.0f, 40.0f);
			ImGui::SliderInt("Light Steps", &parameters.LightMarchSteps, 1, 20);
			ImGui::SliderFloat("Coverage", &parameters.Coverage, 0, 1);
			ImGui::SliderFloat("Cloud Type", &parameters.CloudType, 0, 1);

			ImGui::SliderFloat("Planet Size", &parameters.PlanetRadius, 100, 100000);
			ImGui::DragFloatRange2("Atmosphere Height", &parameters.AtmosphereHeightRange.x, &parameters.AtmosphereHeightRange.y, 1.0f, 10, 1000);
		}
	}
	ImGui::End();

	RGTexture* pNoiseTexture = RGUtils::CreatePersistent(graph, "Shape Noise",
		TextureDesc::Create3D(parameters.ShapeNoiseResolution, parameters.ShapeNoiseResolution, parameters.ShapeNoiseResolution, ResourceFormat::RGBA8_UNORM, TextureFlag::None, 1, 4), &m_pShapeNoise, true);
	RGTexture* pDetailNoiseTexture = RGUtils::CreatePersistent(graph, "Detail Noise",
		TextureDesc::Create3D(parameters.DetailNoiseResolution, parameters.DetailNoiseResolution, parameters.DetailNoiseResolution, ResourceFormat::RGBA8_UNORM, TextureFlag::None, 1, 4), &m_pDetailNoise, true);
	RGTexture* pCloudTypeLUT = RGUtils::CreatePersistent(graph, "Height Gradient",
		TextureDesc::Create2D(128, 128, ResourceFormat::R8_UNORM), &m_pCloudHeightDensityLUT, true);

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

						context.SetComputeRootSignature(CloudsRS);
						context.SetPipelineState(CloudShapeNoisePSO);

						NoiseParams constants;
						constants.Seed = parameters.NoiseSeed;
						constants.ResolutionInv = 1.0f / resolution;
						constants.Frequency = parameters.ShapeNoiseFrequency;

						context.SetRootCBV(0, constants);
						context.BindResources(2, pNoiseTexture->Get()->GetSubResourceUAV(i));

						context.Dispatch(
							ComputeUtils::GetNumThreadGroups(Vector3i(resolution), Vector3i(8)));
					});
		}
		for (uint32 i = 0; i < pDetailNoiseTexture->GetDesc().Mips; ++i)
		{
			graph.AddPass("Compute Detail Noise", RGPassFlag::Compute)
				.Write(pDetailNoiseTexture)
				.Bind([=](CommandContext& context, const RGPassResources& resources)
					{
						uint32 resolution = pDetailNoiseTexture->GetDesc().Width >> i;

						context.SetComputeRootSignature(CloudsRS);
						context.SetPipelineState(CloudDetailNoisePSO);

						NoiseParams constants;
						constants.Seed = parameters.NoiseSeed;
						constants.ResolutionInv = 1.0f / resolution;
						constants.Frequency = parameters.DetailNoiseFrequency;

						context.SetRootCBV(0, constants);
						context.BindResources(2, pDetailNoiseTexture->Get()->GetSubResourceUAV(i));

						context.Dispatch(
							ComputeUtils::GetNumThreadGroups(Vector3i(resolution), Vector3i(8)));
					});
		}

		graph.AddPass("Height Gradient", RGPassFlag::Compute)
			.Write(pCloudTypeLUT)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
				{
					Texture* pTarget = pCloudTypeLUT->Get();

					context.SetComputeRootSignature(CloudsRS);
					context.SetPipelineState(CloudHeighDensityLUTPSO);

					NoiseParams constants;
					constants.ResolutionInv = 1.0f / pTarget->GetWidth();

					context.SetRootCBV(0, constants);
					context.BindResources(2, pTarget->GetUAV());

					context.Dispatch(
						ComputeUtils::GetNumThreadGroups(Vector3i(pTarget->GetWidth()), Vector3i(8)));
				});
	}

	RGTexture* pIntermediateColor = graph.Create("Intermediate Color", sceneTextures.pColorTarget->GetDesc());

	graph.AddPass("Clouds", RGPassFlag::Compute)
		.Read({ pNoiseTexture, pDetailNoiseTexture, pCloudTypeLUT, sceneTextures.pColorTarget, sceneTextures.pDepth })
		.Write(pIntermediateColor)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = pIntermediateColor->Get();

				context.SetComputeRootSignature(CloudsRS);
				context.SetPipelineState(CloudsPSO);

				struct
				{
					float GlobalScale;
					float ShapeNoiseScale;
					float DetailNoiseScale;
					float Coverage;
					float GlobalDensity;
					float RayStepSize;
					uint32 LightMarchSteps;
					float PlanetRadius;
					float AtmosphereHeightStart;
					float AtmosphereHeightEnd;
					float DetailNoiseInfluence;
					float CloudType;
					Vector3 WindDirection;
					float WindSpeed;
					float TopSkew;
				} constants;

				constants.GlobalScale = parameters.GlobalScale;
				constants.ShapeNoiseScale = parameters.ShapeNoiseScale;
				constants.DetailNoiseScale = parameters.DetailNoiseScale;
				constants.Coverage = parameters.Coverage;
				constants.GlobalDensity = parameters.GlobalDensity;
				constants.RayStepSize = parameters.RaymarchStepSize;
				constants.LightMarchSteps = parameters.LightMarchSteps;
				constants.PlanetRadius = parameters.PlanetRadius;
				constants.AtmosphereHeightStart = parameters.AtmosphereHeightRange.x;
				constants.AtmosphereHeightEnd = parameters.AtmosphereHeightRange.y;
				constants.DetailNoiseInfluence = parameters.DetailNoiseInfluence;
				constants.CloudType = parameters.CloudType;
				constants.WindDirection = Vector3(cos(parameters.WindAngle), 0, -sin(parameters.WindAngle));
				constants.WindSpeed = parameters.WindSpeed;
				constants.TopSkew = parameters.CloudTopSkew;

				context.SetRootCBV(0, constants);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3,
					{
						sceneTextures.pColorTarget->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						pCloudTypeLUT->Get()->GetSRV(),
						pNoiseTexture->Get()->GetSRV(),
						pDetailNoiseTexture->Get()->GetSRV(),
					});
				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
			});

	sceneTextures.pColorTarget = pIntermediateColor;

	return pNoiseTexture;
}
