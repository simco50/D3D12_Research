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
	RefCountPtr<RootSignature> m_pRS;
	RefCountPtr<StateObject> m_pSO;

	RefCountPtr<Texture> m_pAccumulationTexture;
	DelegateHandle m_OnShaderCompiledHandle;
	int m_NumAccumulatedFrames = 1;
};
