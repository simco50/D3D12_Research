#pragma once
class CommandContext;
class GraphicsDevice;
class RootSignature;
class PipelineState;
class Texture;
class RGGraph;
class ShaderManager;
struct SceneData;

DECLARE_MULTICAST_DELEGATE(ImGuiCallback);

class ImGuiRenderer
{
public:
	ImGuiRenderer(GraphicsDevice* pParent);
	~ImGuiRenderer();

	void NewFrame(uint32 width, uint32 height);
	void Render(RGGraph& graph, const SceneData& sceneData, Texture* pRenderTarget);
	void Update();
	DelegateHandle AddUpdateCallback(ImGuiCallbackDelegate&& callback);
	void RemoveUpdateCallback(DelegateHandle handle);

private:
	void CreatePipeline(GraphicsDevice* pDevice);
	void InitializeImGui(GraphicsDevice* pDevice);

	ImGuiCallback m_UpdateCallback;

	PipelineState* m_pPipelineState = nullptr;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<Texture> m_pFontTexture;
};

