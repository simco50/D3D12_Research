#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

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

	Ref<Buffer> m_pAliveList;
	Ref<Buffer> m_pDeadList;
	Ref<Buffer> m_pParticleBuffer;
	Ref<Buffer> m_pCountersBuffer;

	Ref<RootSignature> m_pCommonRS;

	Ref<PipelineState> m_pInitializeBuffersPSO;
	Ref<PipelineState> m_pPrepareArgumentsPS;
	Ref<PipelineState> m_pEmitPS;
	Ref<PipelineState> m_pSimulatePS;
	Ref<PipelineState> m_pSimulateEndPS;

	Ref<PipelineState> m_pRenderParticlesPS;

	float m_ParticlesToSpawn = 0;
};

