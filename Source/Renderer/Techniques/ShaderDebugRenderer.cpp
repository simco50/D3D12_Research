#include "stdafx.h"
#include "ShaderDebugRenderer.h"
#include "Core/Image.h"
#include "RHI/CommandContext.h"
#include "RHI/Device.h"
#include "RHI/PipelineState.h"
#include "Renderer/Renderer.h"
#include "RenderGraph/RenderGraph.h"

ShaderDebugRenderer::ShaderDebugRenderer(GraphicsDevice* pDevice)
	: m_FontSize(24)
{
	const char* pDebugRenderPath = "ShaderDebugRender.hlsl";
	m_pBuildIndirectDrawArgsPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, pDebugRenderPath, "BuildIndirectDrawArgsCS");

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pDebugRenderPath, "RenderGlyphVS");
		psoDesc.SetPixelShader(pDebugRenderPath, "RenderGlyphPS");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetName("Render Glyphs");
		m_pRenderTextPSO = pDevice->CreatePipeline(psoDesc);
	}
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pDebugRenderPath, "RenderLineVS");
		psoDesc.SetPixelShader(pDebugRenderPath, "RenderLinePS");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetName("Render Lines");
		m_pRenderLinesPSO = pDevice->CreatePipeline(psoDesc);
	}

	struct Data
	{
		uint32 Counters[4];

		struct PackedCharacterInstance
		{
			uint32 Position		: 32;
			uint32 Character	: 16;
			uint32 Scale		: 16;
			uint32 Color		: 32;
		} Characters[8192];

		struct PackedLineInstance
		{
			Vector3 A;
			uint32 ColorA;
			Vector3 B;
			uint32 ColorB;
		} Lines[32768];
	};

	constexpr uint32 bufferSize = sizeof(Data);
	m_pRenderDataBuffer = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(bufferSize, BufferFlag::UnorderedAccess), "Shader Debug Render Data");

	BuildFontAtlas(pDevice);
}

void ShaderDebugRenderer::Render(RGGraph& graph, const RenderView* pView, RGTexture* pTarget, RGTexture* pDepth)
{
	RG_GRAPH_SCOPE("GPU Debug Render", graph);

	RGBuffer* pRenderData = graph.Import(m_pRenderDataBuffer);

	struct DrawArgs
	{
		D3D12_DRAW_ARGUMENTS TextArgs;
		D3D12_DRAW_ARGUMENTS LineArgs;
	};

	RGBuffer* pDrawArgs = graph.Create("Indirect Draw Args", BufferDesc::CreateIndirectArguments<DrawArgs>());

	graph.AddPass("Build Draw Args", RGPassFlag::Compute)
		.Write({ pDrawArgs, pRenderData })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.InsertUAVBarrier();

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pBuildIndirectDrawArgsPSO);

				context.BindResources(BindingSlot::UAV, {
					resources.GetUAV(pRenderData),
					resources.GetUAV(pDrawArgs),
					});
				context.Dispatch(1);
			});

	graph.AddPass("Render Lines", RGPassFlag::Raster)
		.Read({ pRenderData, pDrawArgs, pDepth })
		.RenderTarget(pTarget)
		.DepthStencil(pDepth, RenderPassDepthFlags::ReadOnly)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pRenderLinesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

				context.BindRootCBV(BindingSlot::PerView, pView->ViewCB);
				context.BindResources(BindingSlot::SRV, {
					m_pFontAtlas->GetSRV(),
					m_pGlyphData->GetSRV(),
					resources.GetSRV(pRenderData),
					resources.GetSRV(pDepth),
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, resources.Get(pDrawArgs), nullptr, offsetof(DrawArgs, LineArgs));
			});

	graph.AddPass("Render Text", RGPassFlag::Raster)
		.Read({ pRenderData, pDrawArgs })
		.RenderTarget(pTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pRenderTextPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

				struct
				{
					Vector2 AtlasDimensionsInv;
					Vector2 TargetDimensionsInv;
				} parameters;
				parameters.AtlasDimensionsInv = Vector2::One / Vector2(m_pFontAtlas->GetDesc().Size2D());
				parameters.TargetDimensionsInv = Vector2::One / Vector2(pTarget->GetDesc().Size2D());
				context.BindRootCBV(BindingSlot::PerInstance, parameters);
				context.BindResources(BindingSlot::SRV, {
					m_pFontAtlas->GetSRV(),
					m_pGlyphData->GetSRV(),
					resources.GetSRV(pRenderData)
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, resources.Get(pDrawArgs), nullptr, offsetof(DrawArgs, TextArgs));
			});

	graph.AddPass("Transition Draw Data", RGPassFlag::Raster)
		.Write(pRenderData);
}

void ShaderDebugRenderer::GetGPUData(GPUDebugRenderData* pData) const
{
	pData->RenderDataUAV = m_pRenderDataBuffer->GetUAVIndex();
	pData->FontDataSRV = m_pGlyphData->GetSRVIndex();
	pData->FontSize = m_FontSize;
}

void ShaderDebugRenderer::BuildFontAtlas(GraphicsDevice* pDevice)
{
	struct GlyphData
	{
		Vector2 MinUV;
		Vector2 MaxUV;
		Vector2 Dimensions;
		Vector2 Offset;
		float AdvanceX;
	};

	Array<GlyphData> glyphData;

	ImFontAtlas fontAtlas;

	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;
	ImFont* pFont = fontAtlas.AddFontFromFileTTF("Resources/Fonts/JetBrainsMono-Regular.ttf", (float)m_FontSize, &fontConfig);

	{
		unsigned char* pPixels;
		int width, height;
		fontAtlas.GetTexDataAsRGBA32(&pPixels, &width, &height);
		D3D12_SUBRESOURCE_DATA data;
		data.pData = pPixels;
		data.RowPitch = RHI::GetRowPitch(ResourceFormat::RGBA8_UNORM, width);
		data.SlicePitch = RHI::GetSlicePitch(ResourceFormat::RGBA8_UNORM, width, height);
		m_pFontAtlas = pDevice->CreateTexture(TextureDesc::Create2D(width, height, ResourceFormat::RGBA8_UNORM, 1, TextureFlag::ShaderResource), "Font Atlas", data);
	}

	{
		glyphData.resize((int)fontAtlas.GetGlyphRangesDefault()[1]);

		for(int i = 0; i < (int)glyphData.size(); ++i)
		{
			const ImFontGlyph* pGlyph = pFont->FindGlyph((ImWchar)i);
			if (pGlyph)
			{
				GlyphData& data = glyphData[i];
				data.MinUV = Vector2(pGlyph->U0, pGlyph->V0);
				data.MaxUV = Vector2(pGlyph->U1, pGlyph->V1);
				data.Dimensions = Vector2(pGlyph->X1 - pGlyph->X0, pGlyph->Y1 - pGlyph->Y0);
				data.Offset = Vector2(pGlyph->X0, pGlyph->Y0);
				data.AdvanceX = pGlyph->AdvanceX;
			}
		}

		m_pGlyphData = pDevice->CreateBuffer(BufferDesc::CreateStructured((uint32)glyphData.size(), sizeof(GlyphData)), "Glyph Data", glyphData.data());
	}
}
