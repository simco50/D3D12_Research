#pragma once

#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Content/Image.h"

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

struct Timer
{
	Timer(const std::string& name)
		: Name(name)
	{
		QueryPerformanceCounter(&Start);
		QueryPerformanceFrequency(&Freq);
	}

	~Timer()
	{
		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);
		E_LOG(Info, "'%s' - %.3f ms", Name.c_str(), (float)(end.QuadPart - Start.QuadPart) / Freq.QuadPart * 1000);
	}

	LARGE_INTEGER Freq;
	LARGE_INTEGER Start;
	std::string Name;
};

struct Line
{
	Vector2 A, B;
};

struct FontGlyph
{
	char Letter;
	std::vector<Line> Lines;
	Vector2i OriginOffset;
	Vector2i Blackbox;
	uint32 Width;
	uint32 Height;
	uint32 AdvanceWidth;
	uint32 LeftBearing;
	uint32 RightBearing;
	Vector2i AtlasLocation;
};

struct Font
{
	std::vector<FontGlyph> Glyphs;
	uint32 Ascent;
	uint32 Descent;
	uint32 Height;
};

struct FontCreateSettings
{
	const char* pName = "";
	bool Bold = false;
	bool Italic = false;
	bool StrikeThrough = false;
	bool Underline = false;
	uint32 BezierRefinement = 5;
	uint32 Height = 100;

	void* (*pAllocateFn)(size_t size) = [](size_t size) -> void* { return new char[size]; };
	void (*pFreeFn)(void* pMemory) = [](void* pMemory) -> void { delete[] pMemory; };
};

static bool ProcessFont(Font& outFont, const FontCreateSettings& config)
{
	auto ConvertPt = [](const POINTFX& point, const Vector2& origin) {
		Vector2 p;
		p.x = origin.x + (float)point.x.value + (float)point.x.fract * 1.0f / 65536.0f;
		p.y = origin.y + (float)point.y.value + (float)point.y.fract * 1.0f / 65536.0f;
		return p;
	};

	auto SolveBezierCubic = [](const Vector2& a, const Vector2& b, const Vector2& c, const Vector2& d, float t) {
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
		glyph.Letter = (char)letter;
		glyph.OriginOffset = offset;
		glyph.Height = config.Height;
		glyph.Blackbox = Vector2i(metrics.gmBlackBoxX, metrics.gmBlackBoxY);
		glyph.LeftBearing = abc.abcA;
		glyph.AdvanceWidth = abc.abcB;
		glyph.RightBearing = abc.abcC;
		glyph.Width = abc.abcA + abc.abcB + abc.abcC;

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

bool RasterizeGlyph(const FontGlyph& glyph, Vector2i resolution, float scale)
{
	auto IsInside = [&](float x, float y)
	{
		bool inside = false;
		for (const Line& line : glyph.Lines)
		{
			if (line.A.y > y)
				break;

			float dy = line.A.y - line.B.y;
			if (dy == 0)
				continue;

			if (y >= line.A.y && y < line.B.y)
			{
				bool isLeft = (line.B.x - line.A.x) * (y - line.A.y) - (line.B.y - line.A.y) * (x - line.A.x) > 0;
				if (isLeft)
				{
					inside = !inside;
				}
			}
		}
		return inside;
	};

	uint32* pData = new uint32[resolution.x * resolution.y];

	for (int y = 0; y < resolution.y; ++y)
	{
		float pY = resolution.y - ((float)y + 0.5f);

		for (int x = 0; x < resolution.x; ++x)
		{
			float pX = (float)x + 0.5f;

#if 1
			Vector2 sampleLocations[] = {
				Vector2(1, 1), Vector2(-1, -3), Vector2(-3, 2), Vector2(4, -1),
				Vector2(-5, -2), Vector2(2, 5), Vector2(5, 3), Vector2(3, -5),
				Vector2(-2, 6), Vector2(0, -7), Vector2(-4, -6), Vector2(-6, 4),
				Vector2(-8, 0), Vector2(7, -4), Vector2(6, 7), Vector2(-7, 8),
			};
#else
			Vector2 sampleLocations[] = {
				Vector2(0, 0)
			};
#endif

			float shade = 0;
			for (int subPixel = 0; subPixel < ARRAYSIZE(sampleLocations); ++subPixel)
			{
				const Vector2& location = sampleLocations[subPixel];
				shade += IsInside((pX + location.x / 16.0f) / scale, (pY + location.y / 16.0f) / scale) ? 1 : 0;
			}

			shade /= ARRAYSIZE(sampleLocations);
			pData[y * resolution.x + x] = Math::EncodeRGBA(1, shade, shade, shade);
		}
	}

	Image img(resolution.x, resolution.y, ImageFormat::RGBA, pData);
	img.Save("OutputCPU.png");
	delete[] pData;

	return true;
}

void RasterizeGlyphGPU(CommandContext* pContext, const FontGlyph& glyph, float scale, Vector2i location)
{
	struct Parameters
	{
		Vector2i Location;
		uint32 pad[2];
		Vector2i GlyphDimensions;
		uint32 NumLines;
		float Scale;
		Line Lines[1024];
	} parameters;

	parameters.Location = location;
	parameters.GlyphDimensions = Vector2i((uint32)(glyph.AdvanceWidth * scale), (uint32)(glyph.Height * scale));
	parameters.NumLines = (uint32)glyph.Lines.size();
	parameters.Scale = scale;
	memcpy(parameters.Lines, glyph.Lines.data(), sizeof(Line) * glyph.Lines.size());

	pContext->SetRootCBV(0, parameters);
	pContext->Dispatch(ComputeUtils::GetNumThreadGroups(parameters.GlyphDimensions.x, 8, parameters.GlyphDimensions.y, 8));
}

void RasterTestGPU(GraphicsDevice* pDevice, Font& font, const Vector2i& resolution, float scale)
{
	PIXBeginCapture(PIX_CAPTURE_GPU, nullptr);

	RefCountPtr<ID3D12QueryHeap> pTimingHeap;
	RefCountPtr<Buffer> pTimingBuffer = pDevice->CreateBuffer(BufferDesc::CreateReadback(sizeof(uint64) * 2), "Timing Readback Buffer");
	{
		D3D12_QUERY_HEAP_DESC desc{};
		desc.Count = 2;
		desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		desc.NodeMask = 0;
		VERIFY_HR(pDevice->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(pTimingHeap.GetAddressOf())));
	}
	RefCountPtr<ID3D12QueryHeap> pStatsHeap;
	RefCountPtr<Buffer> pStatsBuffer = pDevice->CreateBuffer(BufferDesc::CreateReadback(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * 1), "Stats Readback Buffer");
	{
		D3D12_QUERY_HEAP_DESC desc{};
		desc.Count = 1;
		desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1;
		desc.NodeMask = 0;
		VERIFY_HR(pDevice->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(pStatsHeap.GetAddressOf())));
	}

	RefCountPtr<RootSignature> pRS = new RootSignature(pDevice);
	pRS->AddConstantBufferView(0);
	pRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
	pRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
	pRS->Finalize("RasterizeGlyph");
	RefCountPtr<PipelineState> pRasterGlyphPSO = pDevice->CreateComputePipeline(pRS, "RasterizeGlyph.hlsl", "RasterizeGlyphCS");

	PipelineStateInitializer psoDesc;
	psoDesc.SetVertexShader("RasterizeGlyph.hlsl", "RenderGlyphVS");
	psoDesc.SetPixelShader("RasterizeGlyph.hlsl", "RenderGlyphPS");
	psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
	psoDesc.SetDepthEnabled(false);
	psoDesc.SetRootSignature(pRS);
	psoDesc.SetName("TestAtlasCS");
	RefCountPtr<PipelineState> pTestAtlasPSO = pDevice->CreatePipeline(psoDesc);

	RefCountPtr<Texture> pGlyph = pDevice->CreateTexture(TextureDesc::Create2D(resolution.x, resolution.y, ResourceFormat::RGBA8_UNORM, TextureFlag::UnorderedAccess), "Glyph");

	CommandContext* pContext = pDevice->AllocateCommandContext();

	{
		uint32 values[] = { 0, 0 ,0 , 0xFFFFFFFF };
		pContext->InsertResourceBarrier(pGlyph, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pContext->ClearUavUInt(pGlyph, pGlyph->GetUAV(), values);
	}

	::PIXBeginEvent(pContext->GetCommandList(), 0, MULTIBYTE_TO_UNICODE("Raster"));
	pContext->SetComputeRootSignature(pRS);

	{
		pContext->SetPipelineState(pRasterGlyphPSO);
		pContext->BindResources(1, pGlyph->GetUAV());

		pContext->GetCommandList()->EndQuery(pTimingHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
		pContext->GetCommandList()->BeginQuery(pStatsHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);

		const uint32 spacing = 1;
		Vector2i location;

		location.y += spacing;
		uint32 count = 0;
		for (FontGlyph& glyph : font.Glyphs)
		{
			if (location.x + glyph.AdvanceWidth * scale + spacing > resolution.x)
			{
				location.x = 0;
				location.y += (uint32)(font.Height * scale) + spacing;
			}

			location.x += spacing;
			RasterizeGlyphGPU(pContext, glyph, scale, location);
			glyph.AtlasLocation = location;
			location.x += (uint32)(glyph.AdvanceWidth * scale);
			count += (uint32)(glyph.AdvanceWidth * scale) * font.Height;
		}
	}

	::PIXEndEvent(pContext->GetCommandList());


	struct GlyphData
	{
		Vector2i Location;
		Vector2i Offset;
		Vector2i Dimensions;
	};
	std::vector<GlyphData> glyphData;
	for (const FontGlyph& glyph : font.Glyphs)
	{
		GlyphData& data = glyphData.emplace_back();
		data.Dimensions = Vector2i(glyph.AdvanceWidth, glyph.Height);
		data.Offset = glyph.OriginOffset;
		data.Location = glyph.AtlasLocation;
	}
	RefCountPtr<Buffer> pGlyphDataBuffer = pDevice->CreateBuffer(BufferDesc::CreateStructured(256, sizeof(GlyphData)), "Glyph Data");
	pContext->InsertResourceBarrier(pGlyphDataBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	pContext->WriteBuffer(pGlyphDataBuffer, glyphData.data(), glyphData.size() * sizeof(GlyphData));

	RefCountPtr<Texture> pTestTarget = pDevice->CreateTexture(TextureDesc::CreateRenderTarget(1024, 256, ResourceFormat::RGBA8_UNORM, TextureFlag::UnorderedAccess), "Test Target");
	{
		::PIXBeginEvent(pContext->GetCommandList(), 0, MULTIBYTE_TO_UNICODE("Render"));

		pContext->InsertResourceBarrier(pGlyph, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		pContext->InsertResourceBarrier(pGlyphDataBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		pContext->InsertResourceBarrier(pTestTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

		pContext->SetGraphicsRootSignature(pRS);

		pContext->BeginRenderPass(RenderPassInfo(pTestTarget, RenderPassAccess::Clear_Store, nullptr, RenderPassAccess::NoAccess, false));
		pContext->SetPipelineState(pTestAtlasPSO);
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		struct GlyphInstance
		{
			Vector2 Position;
			uint32 GlyphIndex;
			uint32 pad;
		};

		struct
		{
			GlyphInstance Instances[128];
			Vector2 AtlasDimensionsInv;
			Vector2 TargetDimensions;
			Vector2 TargetDimensionsInv;
		} parameters;

		uint32 instanceIdx = 0;
		float width = 0;
		const char* pText = "Lieeeefje liefje liefje";
		while (*pText)
		{
			GlyphInstance& instance = parameters.Instances[instanceIdx++];
			instance.GlyphIndex = *pText++;
			FontGlyph& glyph = font.Glyphs[instance.GlyphIndex];
			instance.Position = Vector2(width - glyph.OriginOffset.x, glyph.OriginOffset.y);
			width += font.Glyphs[instance.GlyphIndex].Width;
		}

		parameters.AtlasDimensionsInv = Vector2(1.0f / pGlyph->GetWidth(), 1.0f / pGlyph->GetHeight());
		parameters.TargetDimensions = Vector2((float)pTestTarget->GetWidth(), (float)pTestTarget->GetHeight());
		parameters.TargetDimensionsInv = Vector2(1.0f / pTestTarget->GetWidth(), 1.0f / pTestTarget->GetHeight());

		pContext->SetRootCBV(0, parameters);
		pContext->BindResources(2, {
			pGlyph->GetSRV(),
			pGlyphDataBuffer->GetSRV(),
			});

		pContext->Draw(0, 4, instanceIdx);

		pContext->EndRenderPass();
		::PIXEndEvent(pContext->GetCommandList());
	}

	pContext->GetCommandList()->EndQuery(pTimingHeap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
	pContext->GetCommandList()->EndQuery(pStatsHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);

	pContext->GetCommandList()->ResolveQueryData(pTimingHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, pTimingBuffer->GetResource(), 0);
	pContext->GetCommandList()->ResolveQueryData(pStatsHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0, 1, pStatsBuffer->GetResource(), 0);

	D3D12_RESOURCE_DESC resourceDesc = pTestTarget->GetResource()->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
	pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
	RefCountPtr<Buffer> pReadbackBuffer = pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
	pContext->InsertResourceBarrier(pTestTarget, D3D12_RESOURCE_STATE_COPY_SOURCE);
	pContext->InsertResourceBarrier(pReadbackBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	pContext->CopyTexture(pTestTarget, pReadbackBuffer, CD3DX12_BOX(0, 0, pTestTarget->GetWidth(), pTestTarget->GetHeight()));
	pContext->Execute(true);

	char* pData = (char*)pReadbackBuffer->GetMappedData();
	Image img;
	img.SetSize(pTestTarget->GetWidth(), pTestTarget->GetHeight(), 4);
	uint32 imageRowPitch = pTestTarget->GetWidth() * 4;
	uint32 targetOffset = 0;
	for (uint32 i = 0; i < pTestTarget->GetHeight(); ++i)
	{
		img.SetData((uint32*)pData, targetOffset, imageRowPitch);
		pData += textureFootprint.Footprint.RowPitch;
		targetOffset += imageRowPitch;
	}
	img.Save("OutputGPU.png");

	uint64 * pQueryResults = (uint64*)pTimingBuffer->GetMappedData();
	uint64 freq = pContext->GetParent()->GetCommandQueue(pContext->GetType())->GetTimestampFrequency();
	float time = (float)(pQueryResults[1] - pQueryResults[0]) / freq * 1000.0f;
	E_LOG(Info, "Raster Time: %f ms", time);
	
	//D3D12_QUERY_DATA_PIPELINE_STATISTICS1 * pStatsResults = (D3D12_QUERY_DATA_PIPELINE_STATISTICS1*)pStatsBuffer->GetMappedData();

	PIXEndCapture(false);
}

inline void FontTest()
{
	Font font;
	FontCreateSettings config;
	config.pName = "Verdana";
	config.BezierRefinement = 4;
	config.Height = 90;
	ProcessFont(font, config);

	float scale = 1.0f;
	Vector2i resolution(1024, 1024);

	GraphicsDeviceOptions options;
	options.UseDebugDevice = true;
	//options.UseDRED = true;
	//options.LoadPIX = true;
	RefCountPtr<GraphicsDevice> pDevice = new GraphicsDevice(options);

	//RasterizeGlyph(glyph, resolution, scale);
	RasterTestGPU(pDevice, font, resolution, scale);
}
