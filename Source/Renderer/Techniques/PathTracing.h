#pragma once
#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct RenderView;

class PathTracing
{
public:
	PathTracing(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const RenderView* pView, RGTexture* pTarget);
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
