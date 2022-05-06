#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class Buffer;
class PipelineState;
class RootSignature;
class CommandContext;
class Texture;
class RGGraph;
struct SceneView;
struct SceneTextures;

class GpuParticles
{
public:
	GpuParticles(GraphicsDevice* pDevice);
	~GpuParticles() = default;

	void Simulate(RGGraph& graph, const SceneView& view, RGHandle<Texture> depth);
	void Render(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures);
private:

	GraphicsDevice* m_pDevice;

	RefCountPtr<Buffer> m_pAliveList1;
	RefCountPtr<Buffer> m_pAliveList2;
	RefCountPtr<Buffer> m_pDeadList;
	RefCountPtr<Buffer> m_pParticleBuffer;
	RefCountPtr<Buffer> m_pCountersBuffer;

	RefCountPtr<PipelineState> m_pPrepareArgumentsPS;

	RefCountPtr<PipelineState> m_pEmitPS;
	RefCountPtr<Buffer> m_pEmitArguments;

	RefCountPtr<RootSignature> m_pSimulateRS;
	RefCountPtr<PipelineState> m_pSimulatePS;
	RefCountPtr<Buffer> m_pSimulateArguments;

	RefCountPtr<PipelineState> m_pSimulateEndPS;
	RefCountPtr<Buffer> m_pDrawArguments;

	RefCountPtr<RootSignature> m_pRenderParticlesRS;
	RefCountPtr<PipelineState> m_pRenderParticlesPS;

	float m_ParticlesToSpawn = 0;
};

