#include "stdafx.h"
#include "ImGuiRenderer.h"
#include "RHI/CommandContext.h"
#include "RHI/Graphics.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/Texture.h"
#include "RHI/OfflineDescriptorAllocator.h"
#include "SceneView.h"
#include "RenderGraph/RenderGraph.h"
#include "ImGuizmo.h"
#include "Core/Paths.h"
#include "IconsFontAwesome4.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

void ImGui::Image(Texture* pTexture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
{
	ImGui::Image((void*)pTexture->GetSRV()->GetGPUView(), size, uv0, uv1, tint_col, border_col);
}

ImVec2 ImGui::ImageAutoSize(Texture* textureId, const ImVec2& imageDimensions)
{
	ImVec2 windowSize = GetContentRegionAvail();
	float width = windowSize.x;
	float height = windowSize.x * imageDimensions.y / imageDimensions.x;
	if (imageDimensions.x / windowSize.x < imageDimensions.y / windowSize.y)
	{
		width = imageDimensions.x / imageDimensions.y * windowSize.y;
		height = windowSize.y;
	}
	ImVec2 size(width, height);
	Image(textureId, size);
	return size;
}

void ApplyImGuiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();

	style.FrameRounding = 0.0f;
	style.GrabRounding = 1.0f;
	style.WindowRounding = 0.0f;
	style.IndentSpacing = 10.0f;
	style.ScrollbarSize = 16.0f;
	style.WindowPadding = ImVec2(5, 5);
	style.FramePadding = ImVec2(2, 2);

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.14f, 0.71f, 0.83f, 0.95f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.67f, 0.82f, 0.83f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.80f, 0.96f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.58f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.23f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.37f, 0.37f, 0.37f, 0.80f);
	colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
	colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.73f, 0.29f, 0.29f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void ImGuiRenderer::Initialize(GraphicsDevice* pDevice, WindowHandle window)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	Paths::CreateDirectoryTree(Paths::SavedDir());
	static std::string imguiPath = Paths::SavedDir() + "imgui.ini";
	io.IniFilename = imguiPath.c_str();

	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;
	io.Fonts->AddFontFromFileTTF("Resources/Fonts/NotoSans-Regular.ttf", 20.0f, &fontConfig);

	fontConfig.MergeMode = true;
	fontConfig.GlyphMinAdvanceX = 15.0f; // Use if you want to make the icon monospaced
	static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	io.Fonts->AddFontFromFileTTF("Resources/Fonts/" FONT_ICON_FILE_NAME_FA, 15.0f, &fontConfig, icon_ranges);

	ApplyImGuiStyle();

	ImGui_ImplWin32_Init(window);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = pDevice->AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	DescriptorHandle handle = pDevice->StoreViewDescriptor(srvHandle);
	ImGui_ImplDX12_Init(pDevice->GetDevice(), SwapChain::NUM_FRAMES + 1, DXGI_FORMAT_R8G8B8A8_UNORM, pDevice->GetGlobalViewHeap()->GetHeap(), handle.CpuHandle, handle.GpuHandle);
}

void ImGuiRenderer::Shutdown()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void ImGuiRenderer::Render(RGGraph& graph, Texture* pRenderTarget)
{
	graph.AddPass("Render UI", RGPassFlag::Raster | RGPassFlag::NeverCull | RGPassFlag::NoRenderPass)
		.Bind([=](CommandContext& context)
			{
				context.InsertResourceBarrier(pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.BeginRenderPass(RenderPassInfo(pRenderTarget, RenderPassAccess::Load_Store, nullptr, RenderPassAccess::NoAccess, false));
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context.GetCommandList());
				context.EndRenderPass();
			});

	// Update and Render additional Platform Windows
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		graph.AddPass("Update Platform UI", RGPassFlag::Raster | RGPassFlag::NeverCull | RGPassFlag::NoRenderPass)
			.Bind([=](CommandContext& context)
				{
					ImGui::RenderPlatformWindowsDefault(NULL, (void*)context.GetCommandList());
				});
	}
	ImGui::Render();
	ImGui::UpdatePlatformWindows();
}
