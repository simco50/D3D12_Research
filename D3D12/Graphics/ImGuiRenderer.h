#pragma once

class GraphicsDevice;
class RGGraph;
class Texture;
struct SceneView;

using WindowHandle = HWND;

namespace ImGui
{
	void Image(Texture* pTexture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
	void ImageAutoSize(Texture* textureId, const ImVec2& imageDimensions);
}

class ImGuiRenderer
{
public:
	ImGuiRenderer(GraphicsDevice* pParent, WindowHandle window, uint32 numBufferedFrames);
	~ImGuiRenderer();

	void NewFrame(uint32 width, uint32 height);
	void Render(RGGraph& graph, const SceneView& sceneData, Texture* pRenderTarget);
};

