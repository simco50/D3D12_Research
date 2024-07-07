#pragma once

#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

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
	Ref<PipelineState> m_pBuildRasterArgsPSO;
	Ref<PipelineState> m_pRasterPSO;
	Ref<PipelineState> m_pRasterVisualizePSO;
};
