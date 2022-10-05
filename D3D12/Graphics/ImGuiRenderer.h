#pragma once

#include "RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class RGGraph;
class Texture;
struct SceneView;

using WindowHandle = HWND;

namespace ImGui
{
	ImVec2 ImageAutoSize(Texture* textureId, const ImVec2& imageDimensions);
}

namespace ImGuiRenderer
{
	void Initialize(GraphicsDevice* pDevice, WindowHandle window);
	void Shutdown(GraphicsDevice* pDevice);

	void NewFrame();
	void Render(RGGraph& graph, RGTexture* pRenderTarget);
};

