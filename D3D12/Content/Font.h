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
	Vector2i Origin;
	Vector2i Blackbox;
	float AdvanceWidth;
	float A;
	float B;
	float C;
};

struct Font
{
	std::vector<FontGlyph> Glyphs;
	int Ascent;
	int Descent;
	float Height;
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
	auto ConvertPt = [](POINTFX point) {
		Vector2 p;
		p.x = (float)point.x.value + (float)point.x.fract * 1.0f / 65536.0f;
		p.y = (float)point.y.value + (float)point.y.fract * 1.0f / 65536.0f;
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

	const uint32 numCharacters = 256;

	ABCFLOAT* pABC = (ABCFLOAT*)config.pAllocateFn(numCharacters * sizeof(ABCFLOAT));
	assert(GetCharABCWidthsFloatA(hdc, 0, numCharacters - 1, pABC) != 0);

	const uint32 bufferSize = 1024 * 64;
	void* pDataBuffer = config.pAllocateFn(bufferSize);

	const MAT2 m2 = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
	for (uint32 letter = 0; letter < numCharacters; ++letter)
	{
		const ABCFLOAT& abc = pABC[letter];

		GLYPHMETRICS metrics;
		DWORD requiredSize = GetGlyphOutlineA(hdc, letter, GGO_UNHINTED | GGO_BEZIER | GGO_NATIVE, &metrics, bufferSize, pDataBuffer, &m2);
		assert(requiredSize <= bufferSize);

		FontGlyph& glyph = outFont.Glyphs.emplace_back();
		glyph.Letter = (char)letter;
		glyph.Origin = Vector2i(metrics.gmptGlyphOrigin.x, metrics.gmptGlyphOrigin.y);
		glyph.Blackbox = Vector2i(metrics.gmBlackBoxX, metrics.gmBlackBoxY);
		glyph.A = abc.abcfA;
		glyph.B = abc.abcfB;
		glyph.C = abc.abcfC;
		glyph.AdvanceWidth = abc.abcfA + abc.abcfB + abc.abcfC;

		BinaryReader reader(pDataBuffer, requiredSize);
		while (!reader.AtTheEnd())
		{
			uint32 bytesRead = 0;
			const TTPOLYGONHEADER* pHeader = reader.Read<TTPOLYGONHEADER>(&bytesRead);
			assert(pHeader->dwType == TT_POLYGON_TYPE);
			Vector2 startPoint = ConvertPt(pHeader->pfxStart);
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
							ConvertPt(pCurve->apfx[j + 0]),
							ConvertPt(pCurve->apfx[j + 1]),
							ConvertPt(pCurve->apfx[j + 2]),
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
						Vector2 point = ConvertPt(pCurve->apfx[j]);
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

GlobalResource<RootSignature> pRS;
GlobalResource<PipelineState> pPSO;

void RasterizeGlyphGPU(CommandContext* pContext, const FontGlyph& glyph, float scale, Vector2i location)
{
	struct Parameters
	{
		Vector2i Location;
		Vector2i PADD;
		Vector2i GlyphDimensions;
		uint32 NumLines;
		float Scale;
		Line Lines[1024];
	} parameters;

	parameters.Location = location;
	parameters.GlyphDimensions = Vector2i((uint32)(glyph.AdvanceWidth * scale), (uint32)(100 * scale));
	parameters.NumLines = (uint32)glyph.Lines.size();
	parameters.Scale = scale;
	memcpy(parameters.Lines, glyph.Lines.data(), sizeof(Line) * glyph.Lines.size());

	pContext->SetRootCBV(0, parameters);
	pContext->Dispatch(ComputeUtils::GetNumThreadGroups(parameters.GlyphDimensions.x, 8, parameters.GlyphDimensions.y, 8));
}

void RasterTestGPU(GraphicsDevice* pDevice, const Font& font, const Vector2i& resolution, float scale)
{
	PIXBeginCapture(PIX_CAPTURE_GPU, PPIXCaptureParameters());

	pRS = new RootSignature(pDevice);
	pRS->AddConstantBufferView(0);
	pRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
	pRS->Finalize("RasterizeGlyph");
	pPSO = pDevice->CreateComputePipeline(pRS, "RasterizeGlyph.hlsl", "RasterizeGlyphCS");

	RefCountPtr<Buffer> pReadbackBuffer;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};


	{
		Timer f("Rasterize Text");

		CommandContext* pContext = pDevice->AllocateCommandContext();
		RefCountPtr<Texture> pGlyph = pDevice->CreateTexture(TextureDesc::Create2D(resolution.x, resolution.y, ResourceFormat::RGBA8_UNORM, TextureFlag::UnorderedAccess), "Glyph");
		uint32 values[4] = { 0 , 0 ,0,  0xFFFFFFFF };
		pContext->InsertResourceBarrier(pGlyph, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pContext->ClearUavUInt(pGlyph, pGlyph->GetUAV(), values);

		pContext->SetComputeRootSignature(pRS);
		pContext->SetPipelineState(pPSO);
		pContext->BindResources(1, pGlyph->GetUAV());

		uint32 width = 0;
		auto WriteLetter = [&](char c, int y)
		{
			const FontGlyph& glyph = *std::find_if(font.Glyphs.begin(), font.Glyphs.end(), [c](const FontGlyph& g) { return g.Letter == c; });
			RasterizeGlyphGPU(pContext, glyph, scale, Vector2i(width, y));
			width += (uint32)(glyph.AdvanceWidth * scale);
		};

		auto WriteText = [&](const char* pText, int y) {
			while (*pText)
			{
				WriteLetter(*pText++, y);
			}
		};

		width = 0;
		WriteText("Hello There", 0);

		D3D12_RESOURCE_DESC resourceDesc = pGlyph->GetResource()->GetDesc();
		pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
		pReadbackBuffer = pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
		pContext->InsertResourceBarrier(pGlyph, D3D12_RESOURCE_STATE_COPY_SOURCE);
		pContext->InsertResourceBarrier(pReadbackBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
		pContext->CopyTexture(pGlyph, pReadbackBuffer, CD3DX12_BOX(0, 0, pGlyph->GetWidth(), pGlyph->GetHeight()));
		pContext->Execute(true);
	}

	char* pData = (char*)pReadbackBuffer->GetMappedData();
	Image img;
	img.SetSize(resolution.x, resolution.y, 4);
	uint32 imageRowPitch = resolution.x * 4;
	uint32 targetOffset = 0;
	for (int i = 0; i < resolution.y; ++i)
	{
		img.SetData((uint32*)pData, targetOffset, imageRowPitch);
		pData += textureFootprint.Footprint.RowPitch;
		targetOffset += imageRowPitch;
	}
	img.Save("OutputGPU.png");

	PIXEndCapture(false);
}

inline void FontTest(GraphicsDevice* pDevice)
{
	Font font;
	FontCreateSettings config;
	config.pName = "Verdana";
	config.BezierRefinement = 5;
	config.Height = 100;
	ProcessFont(font, config);

	float scale = 2;
	Vector2i resolution(1024, 256);

	const FontGlyph& glyph = *std::find_if(font.Glyphs.begin(), font.Glyphs.end(), [](const FontGlyph& g) { return g.Letter == '@'; });
	//RasterizeGlyph(glyph, resolution, scale);
	RasterTestGPU(pDevice, font, resolution, scale);
}
