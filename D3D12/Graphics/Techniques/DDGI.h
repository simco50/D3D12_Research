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
	RefCountPtr<Texture> pIrradianceHistory;
	RefCountPtr<Texture> pDepthHistory;
	RefCountPtr<Buffer> pProbeOffset;
	RefCountPtr<Buffer> pProbeStates;
};

class DDGI
{
public:
	DDGI(GraphicsDevice* pDevice);
	~DDGI();

	void Execute(RGGraph& graph, const SceneView* pView, World* pWorld);
	void RenderVisualization(RGGraph& graph, const SceneView* pView, const World* pWorld, const SceneTextures& sceneTextures);

private:
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<StateObject> m_pDDGITraceRaysSO;
	RefCountPtr<PipelineState> m_pDDGIUpdateIrradianceColorPSO;
	RefCountPtr<PipelineState> m_pDDGIUpdateIrradianceDepthPSO;
	RefCountPtr<PipelineState> m_pDDGIUpdateProbeStatesPSO;
	RefCountPtr<PipelineState> m_pDDGIVisualizePSO;
};
