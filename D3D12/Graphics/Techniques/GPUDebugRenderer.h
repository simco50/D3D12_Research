#pragma once

#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class Texture;
class Buffer;
class PipelineState;
class RootSignature;

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
	uint32 CharacterInstancesUAV;
	uint32 CharacterCounterUAV;
	uint32 CharacterDataSRV;
};

class GPUDebugRenderer
{
public:
	static constexpr uint32 MAX_CHARACTERS = 256;
	static constexpr uint32 ATLAS_RESOLUTION = 2048;

	GPUDebugRenderer(GraphicsDevice* pDevice, const FontCreateSettings& fontSettings);

	void Render(RGGraph& graph, RGTexture* pTarget);

	void GetGlobalIndices(GPUDebugRenderData* pData) const;

private:
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
	void BuildFontAtlas(CommandContext& context, const Vector2i& resolution, float scale);

	Font m_Font;

	RefCountPtr<RootSignature> m_pCommonRS;

	RefCountPtr<PipelineState> m_pRasterizeGlyphPSO;
	RefCountPtr<PipelineState> m_pRenderGlyphPSO;
	RefCountPtr<PipelineState> m_pBuildIndirectDrawArgsPSO;

	RefCountPtr<Buffer> m_pSubmittedCharacters;
	RefCountPtr<Buffer> m_pSubmittedCharactersCounter;

	RefCountPtr<Texture> m_pFontAtlas;
	RefCountPtr<Buffer> m_pGlyphData;
};
