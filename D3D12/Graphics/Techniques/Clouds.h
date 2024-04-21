#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneTextures;
struct SceneView;

class Clouds
{
public:
	Clouds(GraphicsDevice* pDevice);
	RGTexture* Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView);

private:
	Ref<PipelineState> m_pCloudShapeNoisePSO;
	Ref<PipelineState> m_pCloudDetailNoisePSO;
	Ref<PipelineState> m_pCloudHeighDensityLUTPSO;

	Ref<RootSignature> m_pCloudsRS;
	Ref<PipelineState> m_pCloudsPSO;

	Ref<Texture> m_pShapeNoise;
	Ref<Texture> m_pDetailNoise;
	Ref<Texture> m_pCloudHeightDensityLUT;
};
