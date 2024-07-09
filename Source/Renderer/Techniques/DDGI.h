#pragma once

#include "RenderGraph/RenderGraphDefinitions.h"
#include "RHI/RHI.h"

class RGGraph;
struct World;
struct RenderView;
struct SceneTextures;

struct DDGIVolume
{
	Vector3 Extents;
	Vector3i NumProbes;
	int32 MaxNumRays;
	int32 NumRays;
	Ref<Texture> pIrradianceHistory;
	Ref<Texture> pDepthHistory;
	Ref<Buffer> pProbeOffset;
	Ref<Buffer> pProbeStates;
};

class DDGI
{
public:
	DDGI(GraphicsDevice* pDevice);
	~DDGI();

	void Execute(RGGraph& graph, const RenderView* pView);
	void RenderVisualization(RGGraph& graph, const RenderView* pView, RGTexture* pColorTarget, RGTexture* pDepth);

private:
	Ref<StateObject> m_pDDGITraceRaysSO;
	Ref<PipelineState> m_pDDGIUpdateIrradianceColorPSO;
	Ref<PipelineState> m_pDDGIUpdateIrradianceDepthPSO;
	Ref<PipelineState> m_pDDGIUpdateProbeStatesPSO;
	Ref<PipelineState> m_pDDGIVisualizePSO;
};
