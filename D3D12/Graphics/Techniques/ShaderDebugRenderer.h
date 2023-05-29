#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

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

	void BuildFontAtlas(GraphicsDevice* pDevice, CommandContext& context);

	RefCountPtr<RootSignature> m_pCommonRS;

	RefCountPtr<PipelineState> m_pBuildIndirectDrawArgsPSO;
	RefCountPtr<PipelineState> m_pRenderTextPSO;
	RefCountPtr<PipelineState> m_pRenderLinesPSO;

	RefCountPtr<Buffer> m_pRenderDataBuffer;

	uint32 m_FontSize;
	RefCountPtr<Texture> m_pFontAtlas;
	RefCountPtr<Buffer> m_pGlyphData;
};
