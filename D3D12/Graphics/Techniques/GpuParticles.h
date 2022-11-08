#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class Buffer;
class PipelineState;
class RootSignature;
class Texture;
class RGGraph;
struct SceneView;
struct SceneTextures;

class GpuParticles
{
public:
	GpuParticles(GraphicsDevice* pDevice);
	~GpuParticles() = default;

	void Simulate(RGGraph& graph, const SceneView* pView, RGTexture* pDepth);
	void Render(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);
private:

	RefCountPtr<Buffer> m_pAliveList1;
	RefCountPtr<Buffer> m_pAliveList2;
	RefCountPtr<Buffer> m_pDeadList;
	RefCountPtr<Buffer> m_pParticleBuffer;
	RefCountPtr<Buffer> m_pCountersBuffer;

	RefCountPtr<PipelineState> m_pPrepareArgumentsPS;
	RefCountPtr<PipelineState> m_pEmitPS;
	RefCountPtr<RootSignature> m_pSimulateRS;
	RefCountPtr<PipelineState> m_pSimulatePS;
	RefCountPtr<PipelineState> m_pSimulateEndPS;

	RefCountPtr<RootSignature> m_pRenderParticlesRS;
	RefCountPtr<PipelineState> m_pRenderParticlesPS;

	float m_ParticlesToSpawn = 0;
};

