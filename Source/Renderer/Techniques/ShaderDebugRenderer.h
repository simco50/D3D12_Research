#pragma once

#include "RHI/RHI.h"
#include "Renderer/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;

struct GPUDebugRenderData
{
	uint32 RenderDataUAV;
	uint32 FontDataSRV;
	uint32 FontSize;
};

class ShaderDebugRenderer
{
public:
	ShaderDebugRenderer(GraphicsDevice* pDevice);

	void Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget, RGTexture* pDepth);

	void GetGPUData(GPUDebugRenderData* pData) const;

private:
	struct Line
	{
		Vector2 A, B;
	};

	struct Glyph
	{
		uint32 Letter;
		Array<Line> Lines;
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

	void BuildFontAtlas(GraphicsDevice* pDevice);

	Ref<PipelineState> m_pBuildIndirectDrawArgsPSO;
	Ref<PipelineState> m_pRenderTextPSO;
	Ref<PipelineState> m_pRenderLinesPSO;

	Ref<Buffer> m_pRenderDataBuffer;

	uint32 m_FontSize;
	Ref<Texture> m_pFontAtlas;
	Ref<Buffer> m_pGlyphData;
};
