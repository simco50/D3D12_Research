#include "stdafx.h"
#include "ShaderDebugRenderer.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Content/Image.h"
#include "Graphics/SceneView.h"

ShaderDebugRenderer::ShaderDebugRenderer(GraphicsDevice* pDevice)
	: m_FontSize(24)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 4, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 4, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Common");

	const char* pDebugRenderPath = "ShaderDebugRender.hlsl";
	m_pBuildIndirectDrawArgsPSO = pDevice->CreateComputePipeline(m_pCommonRS, pDebugRenderPath, "BuildIndirectDrawArgsCS");

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pDebugRenderPath, "RenderGlyphVS");
		psoDesc.SetPixelShader(pDebugRenderPath, "RenderGlyphPS");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetRootSignature(m_pCommonRS);
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
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetName("Render Lines");
		m_pRenderLinesPSO = pDevice->CreatePipeline(psoDesc);
	}

	struct Data
	{
		uint32 Counters[4];

		struct PackedCharacterInstance
		{
			uint32 Position;
			uint32 Character;
			uint32 Color;
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

	CommandContext* pContext = pDevice->AllocateCommandContext();
	BuildFontAtlas(pDevice, *pContext);
	pContext->Execute();
}

void ShaderDebugRenderer::Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget, RGTexture* pDepth)
{
	RG_GRAPH_SCOPE("GPU Debug Render", graph);

	RGBuffer* pRenderData = graph.Import(m_pRenderDataBuffer);

	RGBuffer* pDrawArgs = graph.Create("Indirect Draw Args", BufferDesc::CreateIndirectArguments<D3D12_DRAW_ARGUMENTS>(2));

	graph.AddPass("Build Draw Args", RGPassFlag::Compute)
		.Write({ pDrawArgs, pRenderData })
		.Bind([=](CommandContext& context)
			{
				context.InsertUAVBarrier();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pBuildIndirectDrawArgsPSO);

				context.BindResources(2, {
					pRenderData->Get()->GetUAV(),
					pDrawArgs->Get()->GetUAV(),
					});
				context.Dispatch(1);
			});

	graph.AddPass("Render Lines", RGPassFlag::Raster)
		.Read({ pRenderData, pDrawArgs, pDepth })
		.RenderTarget(pTarget, RenderTargetLoadAction::Load)
		.DepthStencil(pDepth, RenderTargetLoadAction::Load, false)
		.Bind([=](CommandContext& context)
			{
				context.SetGraphicsRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pRenderLinesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget->Get()));
				context.BindResources(3, {
					m_pFontAtlas->GetSRV(),
					m_pGlyphData->GetSRV(),
					pRenderData->Get()->GetSRV(),
					pDepth->Get()->GetSRV(),
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pDrawArgs->Get(), nullptr, sizeof(D3D12_DRAW_ARGUMENTS) * 1);
			});

	graph.AddPass("Render Text", RGPassFlag::Raster)
		.Read({ pRenderData, pDrawArgs })
		.RenderTarget(pTarget, RenderTargetLoadAction::Load)
		.Bind([=](CommandContext& context)
			{
				context.SetGraphicsRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pRenderTextPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

				struct
				{
					Vector2 AtlasDimensionsInv;
					Vector2 TargetDimensionsInv;
				} parameters;
				parameters.AtlasDimensionsInv = Vector2::One / Vector2(m_pFontAtlas->GetDesc().Size2D());
				parameters.TargetDimensionsInv = Vector2::One / Vector2(pTarget->GetDesc().Size2D());
				context.BindRootCBV(0, parameters);
				context.BindResources(3, {
					m_pFontAtlas->GetSRV(),
					m_pGlyphData->GetSRV(),
					pRenderData->Get()->GetSRV()
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pDrawArgs->Get(), nullptr, sizeof(D3D12_DRAW_ARGUMENTS) * 0);
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

void ShaderDebugRenderer::BuildFontAtlas(GraphicsDevice* pDevice, CommandContext& context)
{
	struct GlyphData
	{
		Vector2 MinUV;
		Vector2 MaxUV;
		Vector2 Dimensions;
		Vector2 Offset;
		float AdvanceX;
	};

	std::vector<GlyphData> glyphData;

	ImFontAtlas fontAtlas;
	
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;
	ImFont* pFont = fontAtlas.AddFontFromFileTTF("Resources/Fonts/JetBrainsMono-Regular.ttf", (float)m_FontSize, &fontConfig);

	{
		unsigned char* pPixels;
		int width, height;
		fontAtlas.GetTexDataAsRGBA32(&pPixels, &width, &height);
		m_pFontAtlas = pDevice->CreateTexture(TextureDesc::Create2D(width, height, ResourceFormat::RGBA8_UNORM), "Font Atlas");
		D3D12_SUBRESOURCE_DATA data;
		data.pData = pPixels;
		data.RowPitch = RHI::GetRowPitch(ResourceFormat::RGBA8_UNORM, width);
		data.SlicePitch = RHI::GetSlicePitch(ResourceFormat::RGBA8_UNORM, width, height);
		context.WriteTexture(m_pFontAtlas, data, 0);
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

		m_pGlyphData = pDevice->CreateBuffer(BufferDesc::CreateStructured((uint32)glyphData.size(), sizeof(GlyphData)), "Glyph Data");
		context.WriteBuffer(m_pGlyphData, glyphData.data(), glyphData.size() * sizeof(GlyphData));
	}
}
