#pragma once
#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct SceneTextures;
struct RenderView;

class Clouds
{
public:
	Clouds(GraphicsDevice* pDevice);
	RGTexture* Render(RGGraph& graph, const RenderView* pView, RGTexture* pColorTarget, RGTexture* pDepth);

private:
	Ref<PipelineState> m_pCloudShapeNoisePSO;
	Ref<PipelineState> m_pCloudDetailNoisePSO;
	Ref<PipelineState> m_pCloudHeighDensityLUTPSO;

	Ref<PipelineState> m_pCloudsPSO;

	Ref<Texture> m_pShapeNoise;
	Ref<Texture> m_pDetailNoise;
	Ref<Texture> m_pCloudHeightDensityLUT;
};
