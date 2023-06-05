#pragma once

#include "Graphics/RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

using WindowHandle = HWND;

namespace ImGui
{
	ImVec2 GetAutoSize(const ImVec2& dimensions);
	bool ToggleButton(const char* pText, bool* pValue, const ImVec2& size = ImVec2(0, 0));
}

namespace ImGuiRenderer
{
	void Initialize(GraphicsDevice* pDevice, WindowHandle window);
	void Shutdown();

	void NewFrame();
	void Render(RGGraph& graph, RGTexture* pRenderTarget);
};

