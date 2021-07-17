#pragma once

class RootSignature;
class StateObject;
class Texture;
class GraphicsDevice;
class RGGraph;
struct SceneData;

class PathTracing
{
public:
	PathTracing(GraphicsDevice* pDevice);
	~PathTracing();
	void Render(RGGraph& graph, const SceneData& scene);
	void OnResize(uint32 width, uint32 height);
	void Reset();
	bool IsSupported();

private:
	GraphicsDevice* m_pDevice;
	std::unique_ptr<RootSignature> m_pRS;
	StateObject* m_pSO = nullptr;

	std::unique_ptr<Texture> m_pAccumulationTexture;
	DelegateHandle m_OnShaderCompiledHandle;
	int m_NumAccumulatedFrames = 1;
};
