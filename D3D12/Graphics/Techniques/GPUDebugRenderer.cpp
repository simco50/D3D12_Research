#include "stdafx.h"
#include "GPUDebugRenderer.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Content/Image.h"
#include "Graphics/SceneView.h"
#include "stb_rect_pack.h"

GPUDebugRenderer::GPUDebugRenderer(GraphicsDevice* pDevice, const FontCreateSettings& fontSettings)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4);
	m_pCommonRS->Finalize("Common");
	m_pRasterizeGlyphPSO = pDevice->CreateComputePipeline(m_pCommonRS, "RasterizeFont.hlsl", "RasterizeGlyphCS");

	m_pBuildIndirectDrawArgsPSO = pDevice->CreateComputePipeline(m_pCommonRS, "ShaderDebugRender.hlsl", "BuildIndirectDrawArgsCS");

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader("ShaderDebugRender.hlsl", "RenderGlyphVS");
		psoDesc.SetPixelShader("ShaderDebugRender.hlsl", "RenderGlyphPS");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetName("Render Glyphs");
		m_pRenderTextPSO = pDevice->CreatePipeline(psoDesc);
	}
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader("ShaderDebugRender.hlsl", "RenderLineVS");
		psoDesc.SetPixelShader("ShaderDebugRender.hlsl", "RenderLinePS");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::D32_FLOAT, 1);
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

		struct CharacterInstance
		{
			Vector2 Position;
			uint32 Character;
			uint32 Color;
		} Characters[1024];

		struct LineInstance
		{
			Vector3 A;
			Vector3 B;
			uint32 Color;
			uint32 ScreenSpace;
		} Lines[8192];
	};

	constexpr uint32 bufferSize = sizeof(Data);
	m_pRenderDataBuffer = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(bufferSize), "Shader Debug Render Data");

	CommandContext* pContext = pDevice->AllocateCommandContext();
	ProcessFont(m_Font, fontSettings);

	constexpr uint32 ATLAS_RESOLUTION = 512;
	BuildFontAtlas(*pContext, Vector2i(ATLAS_RESOLUTION, ATLAS_RESOLUTION));
	pContext->Execute(true);
}

void GPUDebugRenderer::Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget, RGTexture* pDepth)
{
	RG_GRAPH_SCOPE("GPU Debug Render", graph);

	RGBuffer* pRenderData = graph.ImportBuffer(m_pRenderDataBuffer);

	RGBuffer* pDrawArgs = graph.CreateBuffer("Indirect Draw Args", BufferDesc::CreateIndirectArguments<D3D12_DRAW_ARGUMENTS>(2));

	graph.AddPass("Build Draw Args", RGPassFlag::Compute)
		.Write({ pDrawArgs, pRenderData })
		.Bind([=](CommandContext& context)
			{
				context.InsertUavBarrier();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pBuildIndirectDrawArgsPSO);

				context.BindResources(2, {
					pRenderData->Get()->GetUAV(),
					pDrawArgs->Get()->GetUAV(),
					});
				context.Dispatch(1);
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
				context.SetRootConstants(0, parameters);
				context.BindResources(3, {
					m_pFontAtlas->GetSRV(),
					m_pGlyphData->GetSRV(),
					pRenderData->Get()->GetSRV()
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pDrawArgs->Get(), nullptr, sizeof(D3D12_DRAW_ARGUMENTS) * 0);
			});

	graph.AddPass("Render Lines", RGPassFlag::Raster)
		.Read({ pRenderData, pDrawArgs })
		.RenderTarget(pTarget, RenderTargetLoadAction::Load)
		.DepthStencil(pDepth, RenderTargetLoadAction::Load, false)
		.Bind([=](CommandContext& context)
			{
				context.SetGraphicsRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pRenderLinesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(3, {
					m_pFontAtlas->GetSRV(),
					m_pGlyphData->GetSRV(),
					pRenderData->Get()->GetSRV()
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pDrawArgs->Get(), nullptr, sizeof(D3D12_DRAW_ARGUMENTS) * 1);

				context.InsertResourceBarrier(pRenderData->Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			});
}

void GPUDebugRenderer::GetGlobalIndices(GPUDebugRenderData* pData) const
{
	pData->RenderDataUAV = m_pRenderDataBuffer->GetUAVIndex();
	pData->FontDataSRV = m_pGlyphData->GetSRVIndex();
}

struct BinaryReader
{
	BinaryReader(const void* pBuffer, uint32 bufferSize)
		: Size(bufferSize), pBuffer(pBuffer), pCurrent(pBuffer)
	{}

	template<typename T>
	const T* Read(uint32* pOutRead = nullptr)
	{
		const T* pV = static_cast<const T*>(pCurrent);
		pCurrent = (char*)pCurrent + sizeof(T);
		if (pOutRead)
		{
			*pOutRead += sizeof(T);
		}
		return pV;
	}

	void Advance(uint32 numBytes)
	{
		pCurrent = (char*)pCurrent + numBytes;
	}

	bool AtTheEnd() const
	{
		return (char*)pCurrent >= (char*)pBuffer + Size;
	}

	uint32 Size;
	const void* pBuffer;
	const void* pCurrent;
};

bool GPUDebugRenderer::ProcessFont(Font& outFont, const FontCreateSettings& config)
{
	auto ConvertPt = [](const POINTFX& point, const Vector2& origin)
	{
		Vector2 p;
		p.x = origin.x + (float)point.x.value + (float)point.x.fract * 1.0f / 65536.0f;
		p.y = origin.y + (float)point.y.value + (float)point.y.fract * 1.0f / 65536.0f;
		return p;
	};

	auto SolveBezierCubic = [](const Vector2& a, const Vector2& b, const Vector2& c, const Vector2& d, float t)
	{
		return
			powf(1 - t, 3) * a +
			t * b * 3 * powf(1 - t, 2) +
			c * 3 * (1 - t) * powf(t, 2) +
			d * powf(t, 3);
	};

	HFONT font = CreateFontA(
		config.Height,
		0,
		0,
		0,
		config.Bold ? FW_BOLD : FW_DONTCARE,
		config.Italic ? TRUE : FALSE,
		config.Underline ? TRUE : FALSE,
		config.StrikeThrough ? TRUE : FALSE,
		DEFAULT_CHARSET,
		OUT_OUTLINE_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		VARIABLE_PITCH,
		config.pName);

	if (!font)
		return false;

	HDC hdc = GetDC(nullptr);
	if (!hdc)
		return false;

	hdc = CreateCompatibleDC(hdc);
	if (!hdc)
		return false;

	SelectObject(hdc, font);

	uint32 metricSize = GetOutlineTextMetricsA(hdc, sizeof(OUTLINETEXTMETRICA), nullptr);
	if (metricSize == 0)
		return false;
	OUTLINETEXTMETRICA* pMetric = (OUTLINETEXTMETRICA*)config.pAllocateFn(metricSize);
	GetOutlineTextMetricsA(hdc, metricSize, pMetric);
	outFont.Ascent = pMetric->otmAscent;
	outFont.Descent = pMetric->otmDescent;
	outFont.Height = config.Height;
	outFont.pName = config.pName;

	const uint32 numCharacters = 256;

	ABC* pABC = (ABC*)config.pAllocateFn(numCharacters * sizeof(ABC));
	assert(GetCharABCWidthsA(hdc, 0, numCharacters - 1, pABC) != 0);

	const uint32 bufferSize = 1024 * 64;
	void* pDataBuffer = config.pAllocateFn(bufferSize);

	const MAT2 m2 = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
	for (uint32 letter = 0; letter < numCharacters; ++letter)
	{
		const ABC& abc = pABC[letter];

		GLYPHMETRICS metrics;
		DWORD requiredSize = GetGlyphOutlineA(hdc, letter, GGO_UNHINTED | GGO_BEZIER | GGO_NATIVE, &metrics, bufferSize, pDataBuffer, &m2);
		assert(requiredSize <= bufferSize);

		Vector2 offset = Vector2((float)-metrics.gmptGlyphOrigin.x, (float)metrics.gmBlackBoxY - metrics.gmptGlyphOrigin.y);

		FontGlyph& glyph = outFont.Glyphs.emplace_back();
		glyph.Letter = letter;
		glyph.OriginOffset = offset;
		glyph.Height = config.Height;
		glyph.Blackbox = Vector2i(metrics.gmBlackBoxX, metrics.gmBlackBoxY);
		glyph.LeftBearing = abc.abcA;
		glyph.AdvanceWidth = abc.abcB;
		glyph.RightBearing = abc.abcC;
		glyph.Width = abc.abcA + abc.abcB + abc.abcC;
		glyph.Inc = Vector2i(metrics.gmCellIncX, metrics.gmCellIncY);

		BinaryReader reader(pDataBuffer, requiredSize);
		while (!reader.AtTheEnd())
		{
			uint32 bytesRead = 0;
			const TTPOLYGONHEADER* pHeader = reader.Read<TTPOLYGONHEADER>(&bytesRead);
			assert(pHeader->dwType == TT_POLYGON_TYPE);
			Vector2 startPoint = ConvertPt(pHeader->pfxStart, offset);
			Vector2 lastPoint = startPoint;

			while (bytesRead < pHeader->cb)
			{
				const TTPOLYCURVE* pCurve = reader.Read<TTPOLYCURVE>(&bytesRead);
				uint32 num = pCurve->cpfx;
				switch (pCurve->wType)
				{
				case TT_PRIM_CSPLINE:
					for (uint32 j = 0; j < num; j += 3)
					{
						const Vector2 points[4] = {
							lastPoint,
							ConvertPt(pCurve->apfx[j + 0], offset),
							ConvertPt(pCurve->apfx[j + 1], offset),
							ConvertPt(pCurve->apfx[j + 2], offset),
						};

						Vector2 prevPt = Vector2::Zero;
						for (uint32 step = 0; step <= config.BezierRefinement; ++step)
						{
							Vector2 pt = SolveBezierCubic(points[0], points[1], points[2], points[3], (float)step / config.BezierRefinement);
							if (step != 0)
								glyph.Lines.push_back({ prevPt, pt });
							prevPt = pt;
						}
						lastPoint = points[3];
					}
					break;
				case TT_PRIM_LINE:
					for (uint32 j = 0; j < num; ++j)
					{
						Vector2 point = ConvertPt(pCurve->apfx[j], offset);
						glyph.Lines.push_back({ lastPoint, point });
						lastPoint = point;
					}
					break;
				case TT_PRIM_QSPLINE:
				default:
					assert(false);
					break;
				}

				num -= 1; // huh?
				reader.Advance(sizeof(POINTFX) * num);
				bytesRead += sizeof(POINTFX) * num;
			}

			if (startPoint != lastPoint)
			{
				glyph.Lines.push_back({ lastPoint, startPoint });
			}
		}

		// Make sure the first point of the line is the lowest
		for (Line& line : glyph.Lines)
		{
			if (line.A.y > line.B.y)
			{
				std::swap(line.A, line.B);
			}
		}

		// Sort lines based on lowest Y point
		std::sort(glyph.Lines.begin(), glyph.Lines.end(), [](const Line& a, const Line& b) { return a.A.y < b.A.y; });
	}

	config.pFreeFn(pMetric);
	config.pFreeFn(pDataBuffer);
	config.pFreeFn(pABC);

	DeleteDC(hdc);
	DeleteObject(font);

	return true;
}

void GPUDebugRenderer::BuildFontAtlas(CommandContext& context, const Vector2i& resolution)
{
	Font& font = m_Font;

	struct GlyphData
	{
		Vector2i Location;
		Vector2i Offset;
		Vector2i Dimensions;
		uint32 Width;
	};
	std::vector<GlyphData> glyphData;

	{
		std::vector<stbrp_rect> packRects;
		for (FontGlyph& glyph : font.Glyphs)
		{
			stbrp_rect& packRect = packRects.emplace_back();
			packRect.id = glyph.Letter;
			packRect.w = (uint16)glyph.AdvanceWidth;
			packRect.h = (uint16)font.Height;
		}

		std::vector<stbrp_node> nodes(resolution.x);
		stbrp_context packContext;

		stbrp_init_target(&packContext, resolution.x, resolution.y, nodes.data(), (int32)nodes.size());
		stbrp_pack_rects(&packContext, packRects.data(), (int32)packRects.size());

		for (const stbrp_rect& packRect : packRects)
		{
			const FontGlyph& glyph = m_Font.Glyphs[packRect.id];
			GlyphData& data = glyphData.emplace_back();
			data.Dimensions.x = packRect.w;
			data.Dimensions.y = packRect.h;
			data.Location.x = packRect.x;
			data.Location.y = packRect.y;
			data.Offset = glyph.OriginOffset;
			data.Width = glyph.Width;
		}

		m_pGlyphData = context.GetParent()->CreateBuffer(BufferDesc::CreateStructured((uint32)glyphData.size(), sizeof(GlyphData)), "Glyph Data");
		context.InsertResourceBarrier(m_pGlyphData, D3D12_RESOURCE_STATE_COPY_DEST);
		context.WriteBuffer(m_pGlyphData, glyphData.data(), glyphData.size() * sizeof(GlyphData));
		context.InsertResourceBarrier(m_pGlyphData, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	}

	{
		m_pFontAtlas = context.GetParent()->CreateTexture(TextureDesc::Create2D(resolution.x, resolution.y, ResourceFormat::R8_UNORM, TextureFlag::UnorderedAccess), "Font Atlas");
		context.InsertResourceBarrier(m_pFontAtlas, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.ClearUavUInt(m_pFontAtlas, m_pFontAtlas->GetUAV(), TVector4<uint32>(0, 0, 0, 0xFFFFFFFF));

		context.SetComputeRootSignature(m_pCommonRS);
		context.SetPipelineState(m_pRasterizeGlyphPSO);

		context.BindResources(2, m_pFontAtlas->GetUAV());

		for (FontGlyph& glyph : font.Glyphs)
		{
			GlyphData& gpuData = glyphData[(uint32)glyph.Letter];

			struct
			{
				Vector2i Location;
				Vector2i GlyphDimensions;
				Line Lines[512];
				uint32 NumLines;
			} parameters;

			parameters.Location = gpuData.Location;
			parameters.GlyphDimensions = gpuData.Dimensions;
			parameters.NumLines = (uint32)glyph.Lines.size();
			check(glyph.Lines.size() <= ARRAYSIZE(parameters.Lines));
			memcpy(parameters.Lines, glyph.Lines.data(), sizeof(Line) * glyph.Lines.size());

			context.SetRootCBV(1, parameters);
			context.Dispatch(ComputeUtils::GetNumThreadGroups(parameters.GlyphDimensions.x, 8, parameters.GlyphDimensions.y, 8));
		}
	}
}
