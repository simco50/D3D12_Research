#include "stdafx.h"
#include "JumpFlood.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/SceneView.h"
#include "Graphics/RenderGraph/RenderGraph.h"

JumpFlood::JumpFlood(GraphicsDevice* pDevice)
{
	m_pInit = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRootSignature, "JumpFlood.hlsl", "JumpFloodInitCS");
	m_pJumpFlood = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRootSignature, "JumpFlood.hlsl", "JumpFloodCS");
}

JumpFlood::~JumpFlood()
{
}

RGTexture* JumpFlood::Execute(RGGraph& graph, RGTexture* pInput, uint32 size)
{
	Vector2u dimensions(pInput->GetDesc().Width, pInput->GetDesc().Height);

	TextureDesc floodDesc = TextureDesc::Create2D(dimensions.x, dimensions.y, ResourceFormat::RG16_UINT);
	RGTexture* pJumpFloodInit = graph.Create("JumpFlood.Init", floodDesc);
	graph.AddPass("JumpFlood.Init", RGPassFlag::Compute)
		.Read(pInput)
		.Write(pJumpFloodInit)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(GraphicsCommon::pCommonRootSignature);
				context.SetPipelineState(m_pInit);

				struct
				{
					Vector2u Dimensions;
					Vector2 DimensionsInv;
					uint32 SampleDilation;
				} params;
				params.Dimensions = dimensions;
				params.DimensionsInv = Vector2(1.0f / dimensions.x, 1.0f / dimensions.y);

				context.BindRootCBV(0, params);

				context.BindResources(2, pJumpFloodInit->Get()->GetUAV());
				context.BindResources(3, pInput->Get()->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(dimensions.x, 8, dimensions.y, 8));
			});

	RGTexture* pFloodFillSource = pJumpFloodInit;

	int passes = (int)Math::Ceil(log2f(size + 1.0f));
	for (int pass = 0; pass < passes; ++pass)
	{
		int sampleDilation = 1u << (passes - pass - 1);

		// Split horizontal and vertical passes
		for (int i = 0; i < 2; ++i)
		{
			const char* pDirection = i == 0 ? "Horizontal" : "Vertical";
			RGTexture* pFloodFillTarget = graph.Create(Sprintf("JumpFlood.Target%d_%s", pass, pDirection).c_str(), floodDesc);

			graph.AddPass(Sprintf("JumpFlood.Iteration%d_%s", pass, pDirection, pass).c_str(), RGPassFlag::Compute)
				.Read(pFloodFillSource)
				.Write(pFloodFillTarget)
				.Bind([=](CommandContext& context)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRootSignature);
						context.SetPipelineState(m_pJumpFlood);

						struct
						{
							Vector2u Dimensions;
							Vector2 DimensionsInv;
							Vector2u SampleDilation;
						} params;
						params.Dimensions = dimensions;
						params.DimensionsInv = Vector2(1.0f / dimensions.x, 1.0f / dimensions.y);
						params.SampleDilation = Vector2u((1 - i) * sampleDilation, i * sampleDilation);

						context.BindRootCBV(0, params);

						context.BindResources(2, pFloodFillTarget->Get()->GetUAV());
						context.BindResources(3, pFloodFillSource->Get()->GetSRV());

						context.Dispatch(ComputeUtils::GetNumThreadGroups(dimensions.x, 8, dimensions.y, 8));
					});
			pFloodFillSource = pFloodFillTarget;
		}
	};

	return pFloodFillSource;
}
