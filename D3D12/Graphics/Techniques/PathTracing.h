#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;

class PathTracing
{
public:
	PathTracing(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget);
	void Reset();
	bool IsSupported();

private:
	Ref<StateObject> m_pSO;
	Ref<PipelineState> m_pBlitPSO;

	Ref<Texture> m_pAccumulationTexture;
	DelegateHandle m_OnShaderCompiledHandle;
	int m_NumAccumulatedFrames = 1;
	Matrix m_LastViewProjection;
};
