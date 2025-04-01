#include "stdafx.h"
#include "ImGuiRenderer.h"
#include "Core/Paths.h"
#include "Core/Profiler.h"
#include "RHI/CommandContext.h"
#include "RHI/Device.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/Texture.h"
#include "Renderer/Renderer.h"

#include <IconsFontAwesome4.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

namespace ImGui
{
	ImVec2 GetAutoSize(const ImVec2& dimensions)
	{
		ImVec2 windowSize = GetContentRegionAvail();
		float width = windowSize.x;
		float height = windowSize.x * dimensions.y / dimensions.x;
		if (dimensions.x / windowSize.x < dimensions.y / windowSize.y)
		{
			width = dimensions.x / dimensions.y * windowSize.y;
			height = windowSize.y;
		}
		return ImVec2(width, height);
	}

	bool ToggleButton(const char* pText, bool* pValue, const ImVec2& size)
	{
		PushStyleColor(ImGuiCol_Button, *pValue ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
		PushStyleColor(ImGuiCol_ButtonHovered, *pValue ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		PushStyleColor(ImGuiCol_ButtonActive, *pValue ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
		bool clicked = false;
		if (Button(pText, size))
		{
			*pValue = !*pValue;
			clicked = true;
		}
		PopStyleColor(3);
		return clicked;
	}

	void AddText(ImDrawList* pDrawList, const char* pText, ImVec2 pos, ImU32 inColor, float angleRadians)
	{
		int primitiveOffset = pDrawList->VtxBuffer.Size;
		pDrawList->AddText(pos, inColor, pText);

		// If the angle is not 0, rotate the last N submitted vertices
		if (angleRadians != 0)
		{
			float sinAngle = sin(angleRadians);
			float cosAngle = cos(angleRadians);

			for (int i = primitiveOffset; i < pDrawList->VtxBuffer.Size; ++i)
				pDrawList->VtxBuffer[i].pos = ImRotate(pDrawList->VtxBuffer[i].pos - pos, cosAngle, sinAngle) + pos;
		}
	}
}

void ApplyImGuiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();

	style.FrameRounding =		0.0f;
	style.GrabRounding =		1.0f;
	style.WindowRounding =		0.0f;
	style.IndentSpacing =		10.0f;
	style.ScrollbarSize =		12.0f;
	style.WindowPadding =		ImVec2(2, 2);
	style.FramePadding =		ImVec2(2, 2);
	style.ItemSpacing =			ImVec2(6, 2);

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] =							ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled] =					ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
	colors[ImGuiCol_WindowBg] =						ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
	colors[ImGuiCol_ChildBg] =						ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_PopupBg] =						ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] =						ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_BorderShadow] =					ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] =						ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] =				ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_FrameBgActive] =				ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_TitleBg] =						ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
	colors[ImGuiCol_TitleBgActive] =				ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] =				ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
	colors[ImGuiCol_MenuBarBg] =					ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] =					ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
	colors[ImGuiCol_ScrollbarGrab] =				ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] =			ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] =			ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	colors[ImGuiCol_CheckMark] =					ImVec4(0.14f, 0.71f, 0.83f, 0.95f);
	colors[ImGuiCol_SliderGrab] =					ImVec4(0.26f, 0.67f, 0.82f, 0.83f);
	colors[ImGuiCol_SliderGrabActive] =				ImVec4(0.42f, 0.80f, 0.96f, 1.00f);
	colors[ImGuiCol_Button] =						ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_ButtonHovered] =				ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_ButtonActive] =					ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
	colors[ImGuiCol_Header] =						ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
	colors[ImGuiCol_HeaderHovered] =				ImVec4(0.35f, 0.35f, 0.35f, 0.58f);
	colors[ImGuiCol_HeaderActive] =					ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
	colors[ImGuiCol_Separator] =					ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] =				ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	colors[ImGuiCol_SeparatorActive] =				ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_ResizeGrip] =					ImVec4(1.00f, 1.00f, 1.00f, 0.23f);
	colors[ImGuiCol_ResizeGripHovered] =			ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] =				ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
	colors[ImGuiCol_Tab] =							ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_TabHovered] =					ImVec4(0.37f, 0.37f, 0.37f, 0.80f);
	colors[ImGuiCol_TabSelected] =					ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_TabDimmed] =					ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_TabDimmedSelected] =			ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_DockingPreview] =				ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
	colors[ImGuiCol_DockingEmptyBg] =				ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_PlotLines] =					ImVec4(0.73f, 0.29f, 0.29f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] =				ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] =				ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] =			ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg] =				ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong] =			ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
	colors[ImGuiCol_TableBorderLight] =				ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
	colors[ImGuiCol_TableRowBg] =					ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] =				ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
	colors[ImGuiCol_TextSelectedBg] =				ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] =				ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] =					ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] =		ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] =			ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] =				ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

static Ref<PipelineState> sImGuiPSO;
static Ref<Texture> sFontTexture;

static void RenderDrawData(const ImDrawData* pDrawData, CommandContext& context)
{
	context.SetGraphicsRootSignature(GraphicsCommon::pCommonRSV2);
	context.SetPipelineState(sImGuiPSO);
	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	context.SetViewport(FloatRect(0.0f, 0.0f, pDrawData->DisplaySize.x, pDrawData->DisplaySize.y));

	struct
	{
		Vector4 ScaleOffset;
		TextureView Texture;
	} params;

	params.ScaleOffset = Vector4(
		2.0f / pDrawData->DisplaySize.x,
		-2.0f / pDrawData->DisplaySize.y,
		-(pDrawData->DisplayPos.x + pDrawData->DisplayPos.x + pDrawData->DisplaySize.x) / pDrawData->DisplaySize.x,
		(pDrawData->DisplayPos.y + pDrawData->DisplayPos.y + pDrawData->DisplaySize.y) / pDrawData->DisplaySize.y);

	uint32 vertexOffset = 0;
	ScratchAllocation vertexData = context.AllocateScratch(sizeof(ImDrawVert) * pDrawData->TotalVtxCount);
	context.SetVertexBuffers(VertexBufferView(vertexData.GpuHandle, pDrawData->TotalVtxCount, sizeof(ImDrawVert), 0));

	uint32 indexOffset = 0;
	ScratchAllocation indexData = context.AllocateScratch(sizeof(ImDrawIdx) * pDrawData->TotalIdxCount);
	context.SetIndexBuffer(IndexBufferView(indexData.GpuHandle, pDrawData->TotalIdxCount, ResourceFormat::R16_UINT, 0));

	ImVec2 clipOff = pDrawData->DisplayPos;
	for (int cmdList = 0; cmdList < pDrawData->CmdListsCount; ++cmdList)
	{
		const ImDrawList* pList = pDrawData->CmdLists[cmdList];

		memcpy((char*)vertexData.pMappedMemory + vertexOffset * sizeof(ImDrawVert), pList->VtxBuffer.Data, pList->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy((char*)indexData.pMappedMemory + indexOffset * sizeof(ImDrawIdx), pList->IdxBuffer.Data, pList->IdxBuffer.Size * sizeof(ImDrawIdx));

		for (int cmd = 0; cmd < pList->CmdBuffer.Size; ++cmd)
		{
			const ImDrawCmd* pCmd = &pList->CmdBuffer[cmd];
			if (pCmd->UserCallback)
			{
				pCmd->UserCallback(pList, pCmd);
			}
			else
			{
				ImVec2 clip_min(pCmd->ClipRect.x - clipOff.x, pCmd->ClipRect.y - clipOff.y);
				ImVec2 clip_max(pCmd->ClipRect.z - clipOff.x, pCmd->ClipRect.w - clipOff.y);
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
					continue;

				if ((int)pCmd->ClipRect.x >= (int)pCmd->ClipRect.z || (int)pCmd->ClipRect.y >= (int)pCmd->ClipRect.w)
					continue;

				Texture* pTexture = (Texture*)pCmd->GetTexID();
				if (!pTexture)
					pTexture = sFontTexture;

				gAssert(pTexture->GetSRV());
				params.Texture = pTexture->GetSRV();

				context.BindRootSRV(BindingSlot::PerInstance, params);
				context.SetScissorRect(FloatRect(clip_min.x, clip_min.y, clip_max.x, clip_max.y));
				context.DrawIndexedInstanced(pCmd->ElemCount, pCmd->IdxOffset + indexOffset, 1, pCmd->VtxOffset + vertexOffset, 0);
			}
		}

		vertexOffset += pList->VtxBuffer.Size;
		indexOffset += pList->IdxBuffer.Size;
	}
}

namespace ViewportImpl
{
	static SwapChain* GetSwapChain(const ImGuiViewport* pViewport)
	{
		return static_cast<SwapChain*>(pViewport->RendererUserData);
	}

	static void Viewport_CreateWindow(ImGuiViewport* pViewport)
	{
		GraphicsDevice* pDevice = static_cast<GraphicsDevice*>(ImGui::GetIO().BackendRendererUserData);

		HWND hwnd = pViewport->PlatformHandleRaw ? (HWND)pViewport->PlatformHandleRaw : (HWND)pViewport->PlatformHandle;
		IM_ASSERT(hwnd != 0);
		pViewport->RendererUserData = new SwapChain(pDevice, DisplayMode::SDR, 3, hwnd);
	}

	static void Viewport_DestroyWindow(ImGuiViewport* pViewport)
	{
		delete GetSwapChain(pViewport);
		pViewport->RendererUserData = nullptr;
	}

	static void Viewport_Resize(ImGuiViewport* pViewport, ImVec2 size)
	{
		GetSwapChain(pViewport)->OnResizeOrMove((uint32)size.x, (uint32)size.y);
	}

	static void Viewport_RenderWindow(ImGuiViewport* pViewport, void* pCmd)
	{
		Texture* pBackBuffer = GetSwapChain(pViewport)->GetBackBuffer();

		{
			CommandContext* pContext = static_cast<CommandContext*>(pCmd);
			PROFILE_GPU_SCOPE(pContext->GetCommandList());

			pContext->InsertResourceBarrier(pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			pContext->BeginRenderPass(RenderPassInfo(pBackBuffer, RenderPassColorFlags::Clear, nullptr, RenderPassDepthFlags::None));

			RenderDrawData(pViewport->DrawData, *pContext);

			pContext->EndRenderPass();
			pContext->InsertResourceBarrier(pBackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			pContext->FlushResourceBarriers();
		}
	}


	static void Viewport_Present(ImGuiViewport* pViewport, void*)
	{
		PROFILE_CPU_SCOPE();
		GetSwapChain(pViewport)->Present();
	}

	static void Setup(GraphicsDevice* pDevice)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
		io.BackendRendererUserData = pDevice;

		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		platform_io.Renderer_CreateWindow = Viewport_CreateWindow;
		platform_io.Renderer_DestroyWindow = Viewport_DestroyWindow;
		platform_io.Renderer_SetWindowSize = Viewport_Resize;
		platform_io.Renderer_RenderWindow = Viewport_RenderWindow;
		platform_io.Renderer_SwapBuffers = Viewport_Present;
	}

	static void Shutdown()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendFlags &= ~ImGuiBackendFlags_RendererHasViewports;
		io.BackendRendererUserData = nullptr;

		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		platform_io.Renderer_CreateWindow = nullptr;
		platform_io.Renderer_DestroyWindow = nullptr;
		platform_io.Renderer_SetWindowSize = nullptr;
		platform_io.Renderer_RenderWindow = nullptr;
		platform_io.Renderer_SwapBuffers = nullptr;
	}
}

void ImGuiRenderer::Initialize(GraphicsDevice* pDevice, WindowHandle window)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.ConfigViewportsNoDefaultParent = true;

	ImGui_ImplWin32_Init(window);

	ViewportImpl::Setup(pDevice);

	Paths::CreateDirectoryTree(Paths::SavedDir());
	static String imguiPath = Paths::SavedDir() + "imgui.ini";
	io.IniFilename = imguiPath.c_str();

	{
		ImFontConfig fontConfig;
		fontConfig.OversampleH = 2;
		fontConfig.OversampleV = 2;
		io.Fonts->AddFontFromFileTTF("Resources/Fonts/NotoSans-Regular.ttf", 20.0f, &fontConfig);
	}

	{
		ImFontConfig fontConfig;
		fontConfig.MergeMode = true;
		fontConfig.GlyphMinAdvanceX = 15.0f; // Use if you want to make the icon monospaced
		static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
		io.Fonts->AddFontFromFileTTF("Resources/Fonts/" FONT_ICON_FILE_NAME_FA, 15.0f, &fontConfig, icon_ranges);
	}

	ResourceFormat pixelFormat = ResourceFormat::RGBA8_UNORM;
	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);

	D3D12_SUBRESOURCE_DATA data;
	data.pData = pPixels;
	data.RowPitch = RHI::GetRowPitch(pixelFormat, width);
	data.SlicePitch = RHI::GetSlicePitch(pixelFormat, width, height);
	sFontTexture = pDevice->CreateTexture(TextureDesc::Create2D(width, height, pixelFormat, 1, TextureFlag::ShaderResource), "ImGui Font", data);

	PipelineStateInitializer psoDesc;
	psoDesc.SetInputLayout({
		{ "POSITION", ResourceFormat::RG32_FLOAT },
		{ "TEXCOORD", ResourceFormat::RG32_FLOAT },
		{ "COLOR", ResourceFormat::RGBA8_UNORM },
		});
	psoDesc.SetRootSignature(GraphicsCommon::pCommonRSV2);
	psoDesc.SetVertexShader("ImGui.hlsl", "VSMain");
	psoDesc.SetPixelShader("ImGui.hlsl", "PSMain");
	psoDesc.SetBlendMode(BlendMode::Alpha, false);
	psoDesc.SetDepthWrite(false);
	psoDesc.SetDepthEnabled(false);
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
	psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
	psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
	psoDesc.SetName("ImGui");
	sImGuiPSO = pDevice->CreatePipeline(psoDesc);

	ApplyImGuiStyle();
}

void ImGuiRenderer::Shutdown()
{
	sFontTexture = nullptr;
	sImGuiPSO = nullptr;

	ImGui::DestroyPlatformWindows();
	ViewportImpl::Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame()
{
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void ImGuiRenderer::Render(CommandContext& context, Texture* pRenderTarget)
{
	PROFILE_GPU_SCOPE(context.GetCommandList());

	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), "ImGui::Render()");
		ImGui::Render();
	}

	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), "Transitions");
		ImDrawData* pDrawData = ImGui::GetDrawData();
		ImVec2 clip_off = pDrawData->DisplayPos;
		for (int cmdList = 0; cmdList < pDrawData->CmdListsCount; ++cmdList)
		{
			const ImDrawList* pList = pDrawData->CmdLists[cmdList];
			for (int cmd = 0; cmd < pList->CmdBuffer.Size; ++cmd)
			{
				const ImDrawCmd* pCmd = &pList->CmdBuffer[cmd];
				Texture* pTexture = (Texture*)pCmd->GetTexID();
				if (pTexture && pTexture->UseStateTracking())
					context.InsertResourceBarrier(pTexture, D3D12_RESOURCE_STATE_UNKNOWN, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}
		}
	}

	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), "Render");
		ImDrawData* pDrawData = ImGui::GetDrawData();

		context.InsertResourceBarrier(pRenderTarget, D3D12_RESOURCE_STATE_UNKNOWN, D3D12_RESOURCE_STATE_RENDER_TARGET);
		context.BeginRenderPass(RenderPassInfo(pRenderTarget, RenderPassColorFlags::Clear, nullptr, RenderPassDepthFlags::None));
		RenderDrawData(pDrawData, context);
		context.EndRenderPass();
	}

	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), "Render Viewports");

		// Update and Render additional Platform Windows
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();

			// Skip the main viewport (index 0), which is always fully handled by the application!
			ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
			for (int i = 1; i < platform_io.Viewports.Size; i++)
			{
				ImGuiViewport* viewport = platform_io.Viewports[i];
				if (viewport->Flags & ImGuiViewportFlags_IsMinimized)
					continue;
				if (platform_io.Platform_RenderWindow) platform_io.Platform_RenderWindow(viewport, nullptr);
				if (platform_io.Renderer_RenderWindow) platform_io.Renderer_RenderWindow(viewport, &context);
			}
		}
	}
}

void ImGuiRenderer::PresentViewports()
{
	// Update and Render additional Platform Windows
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		// Skip the main viewport (index 0), which is always fully handled by the application!
		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		for (int i = 1; i < platform_io.Viewports.Size; i++)
		{
			ImGuiViewport* viewport = platform_io.Viewports[i];
			if (viewport->Flags & ImGuiViewportFlags_IsMinimized)
				continue;
			if (platform_io.Platform_SwapBuffers) platform_io.Platform_SwapBuffers(viewport, nullptr);
			if (platform_io.Renderer_SwapBuffers) platform_io.Renderer_SwapBuffers(viewport, nullptr);
		}
	}
}
