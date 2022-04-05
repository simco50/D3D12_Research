#pragma once
#include "Graphics/RHI/StateObject.h"

class RootSignature;
class StateObject;
class Texture;
class GraphicsDevice;
class RGGraph;
struct SceneView;

class PathTracing
{
public:
	PathTracing(GraphicsDevice* pDevice);
	~PathTracing();
	void Render(RGGraph& graph, const SceneView& scene, Texture* pTarget);
	void OnResize(uint32 width, uint32 height);
	void Reset();
	bool IsSupported();

private:
	GraphicsDevice* m_pDevice;
	RefCountPtr<RootSignature> m_pRS;
	RefCountPtr<StateObject> m_pSO;

	RefCountPtr<Texture> m_pAccumulationTexture;
	DelegateHandle m_OnShaderCompiledHandle;
	int m_NumAccumulatedFrames = 1;
};
