#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

class RGGraph;
struct SceneView;
struct RasterContext;

class SoftwareRaster
{
public:
	SoftwareRaster(GraphicsDevice* pDevice);

	void Render(RGGraph& graph, const SceneView* pView, const RasterContext& rasterContext);

	static void RasterizeTest();

private:
	Ref<RootSignature> m_pCommonRS;
	Ref<PipelineState> m_pBuildRasterArgsPSO;
	Ref<PipelineState> m_pRasterPSO;
	Ref<PipelineState> m_pRasterVisualizePSO;
};
