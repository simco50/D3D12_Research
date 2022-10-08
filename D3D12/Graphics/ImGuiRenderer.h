#pragma once

class GraphicsDevice;
class RGGraph;
class Texture;
struct SceneView;

using WindowHandle = HWND;

namespace ImGui
{
	void Image(Texture* pTexture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
	ImVec2 ImageAutoSize(Texture* textureId, const ImVec2& imageDimensions);
}

namespace ImGuiRenderer
{
	void Initialize(GraphicsDevice* pDevice, WindowHandle window);
	void Shutdown(GraphicsDevice* pDevice);

	void NewFrame();
	void Render(RGGraph& graph, Texture* pRenderTarget);
};

