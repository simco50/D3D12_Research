#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;

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

struct GPUDebugRenderData
{
	uint32 RenderDataUAV;
	uint32 FontDataSRV;
};

class ShaderDebugRenderer
{
public:
	ShaderDebugRenderer(GraphicsDevice* pDevice, const FontCreateSettings& fontSettings);

	void Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget, RGTexture* pDepth);

	void GetGlobalIndices(GPUDebugRenderData* pData) const;

private:
	struct Line
	{
		Vector2 A, B;
	};

	struct FontGlyph
	{
		uint32 Letter;
		std::vector<Line> Lines;
		Vector2i OriginOffset;
		Vector2i Blackbox;
		uint32 Width;
		uint32 Height;
		uint32 AdvanceWidth;
		uint32 LeftBearing;
		uint32 RightBearing;
		Vector2i AtlasLocation;
		Vector2i Inc;
	};

	struct Font
	{
		const char* pName;
		std::vector<FontGlyph> Glyphs;
		uint32 Ascent;
		uint32 Descent;
		uint32 Height;
	};

	bool ProcessFont(Font& outFont, const FontCreateSettings& config);
	void BuildFontAtlas(GraphicsDevice* pDevice, const Vector2i& resolution);

	Font m_Font;

	RefCountPtr<RootSignature> m_pCommonRS;

	RefCountPtr<PipelineState> m_pRasterizeGlyphPSO;
	RefCountPtr<PipelineState> m_pBuildIndirectDrawArgsPSO;
	RefCountPtr<PipelineState> m_pRenderTextPSO;
	RefCountPtr<PipelineState> m_pRenderLinesPSO;

	RefCountPtr<Buffer> m_pRenderDataBuffer;

	RefCountPtr<Texture> m_pFontAtlas;
	RefCountPtr<Buffer> m_pGlyphData;
};
