#pragma once

#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
#include "Graphics/RHI/RHI.h"

class RGGraph;
struct World;
struct SceneView;
struct SceneTextures;

struct DDGIVolume
{
	Vector3 Origin;
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

	void Execute(RGGraph& graph, const SceneView* pView, World* pWorld);
	void RenderVisualization(RGGraph& graph, const SceneView* pView, const World* pWorld, const SceneTextures& sceneTextures);

private:
	Ref<RootSignature> m_pCommonRS;
	Ref<StateObject> m_pDDGITraceRaysSO;
	Ref<PipelineState> m_pDDGIUpdateIrradianceColorPSO;
	Ref<PipelineState> m_pDDGIUpdateIrradianceDepthPSO;
	Ref<PipelineState> m_pDDGIUpdateProbeStatesPSO;
	Ref<PipelineState> m_pDDGIVisualizePSO;
};
