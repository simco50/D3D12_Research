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

private:
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pBuildRasterArgsPSO;
	RefCountPtr<PipelineState> m_pRasterPSO;
	RefCountPtr<PipelineState> m_pRasterVisualizePSO;
};
