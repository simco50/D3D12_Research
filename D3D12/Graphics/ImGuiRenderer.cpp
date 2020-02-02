#include "stdafx.h"
#include "ImGuiRenderer.h"
#include <fstream>
#include "CommandContext.h"
#include "Graphics.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Shader.h"
#include "GraphicsResource.h"
#include "DescriptorAllocator.h"
#include "Core/Input.h"
#include "Texture.h"
#include "Profiler.h"

ImGuiRenderer::ImGuiRenderer(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	CreatePipeline();
	InitializeImGui();
}

ImGuiRenderer::~ImGuiRenderer()
{
	ImGui::DestroyContext();
}

void ImGuiRenderer::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());

	io.MouseDown[0] = Input::Instance().IsMouseDown(0);
	io.MouseDown[1] = Input::Instance().IsMouseDown(1);
	io.MouseDown[2] = Input::Instance().IsMouseDown(2);

	Vector2 mousePos = Input::Instance().GetMousePosition();
	io.MousePos.x = mousePos.x;
	io.MousePos.y = mousePos.y;

	ImGui::NewFrame();
}

void ImGuiRenderer::InitializeImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();
	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);
	m_pFontTexture = std::make_unique<Texture>();
	m_pFontTexture->Create(m_pGraphics, TextureDesc::Create2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource, 1));

	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pFontTexture->SetData(pContext, pPixels);
	io.Fonts->TexID = m_pFontTexture.get();
	pContext->Execute(true);
}

void ImGuiRenderer::CreatePipeline()
{
	//Shaders
	Shader vertexShader("Resources/Shaders/ImGui.hlsl", Shader::Type::VertexShader, "VSMain");
	Shader pixelShader("Resources/Shaders/ImGui.hlsl", Shader::Type::PixelShader, "PSMain");

	//Root signature
	m_pRootSignature = std::make_unique<RootSignature>();
	m_pRootSignature->FinalizeFromShader("ImGui", vertexShader, m_pGraphics->GetDevice());

	//Input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> elementDesc = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	m_pPipelineState = std::make_unique<GraphicsPipelineState>();
	m_pPipelineState->SetBlendMode(BlendMode::Alpha, false);
	m_pPipelineState->SetDepthWrite(false);
	m_pPipelineState->SetDepthEnabled(false);
	m_pPipelineState->SetCullMode(D3D12_CULL_MODE_NONE);
	m_pPipelineState->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
	m_pPipelineState->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
	m_pPipelineState->SetRootSignature(m_pRootSignature->GetRootSignature());
	m_pPipelineState->SetInputLayout(elementDesc.data(), (uint32)elementDesc.size());
	m_pPipelineState->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
	m_pPipelineState->Finalize("ImGui Pipeline", m_pGraphics->GetDevice());
}

void ImGuiRenderer::Render(CommandContext& context)
{
	ImGui::Render();
	ImDrawData* pDrawData = ImGui::GetDrawData();

	if (pDrawData->CmdListsCount == 0)
	{
		return;
	}

	context.SetGraphicsPipelineState(m_pPipelineState.get());
	context.SetGraphicsRootSignature(m_pRootSignature.get());
	Matrix projectionMatrix = XMMatrixOrthographicOffCenterLH(0.0f, (float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight(), 0.0f, 0.0f, 1.0f);
	context.SetDynamicConstantBufferView(0, &projectionMatrix, sizeof(Matrix));
	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.SetViewport(FloatRect(0, 0, (float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight()), 0, 1);

	Profiler::Instance()->Begin("Render UI", &context);
	context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::DontCare_DontCare));

	for (int n = 0; n < pDrawData->CmdListsCount; n++)
	{
		const ImDrawList* pCmdList = pDrawData->CmdLists[n];
		context.SetDynamicVertexBuffer(0, pCmdList->VtxBuffer.Size, sizeof(ImDrawVert), pCmdList->VtxBuffer.Data);
		context.SetDynamicIndexBuffer(pCmdList->IdxBuffer.Size, pCmdList->IdxBuffer.Data, true);

		int indexOffset = 0;
		for (int i = 0; i < pCmdList->CmdBuffer.Size; i++)
		{
			const ImDrawCmd* pcmd = &pCmdList->CmdBuffer[i];
			if (pcmd->UserCallback)
				pcmd->UserCallback(pCmdList, pcmd);
			else
			{
				context.SetScissorRect(FloatRect(pcmd->ClipRect.x, pcmd->ClipRect.y, pcmd->ClipRect.z, pcmd->ClipRect.w));
				if (pcmd->TextureId != nullptr)
				{
					context.SetDynamicDescriptor(1, 0, static_cast<Texture*>(pcmd->TextureId)->GetSRV());
				}
				context.DrawIndexed(pcmd->ElemCount, indexOffset, 0);
			}
			indexOffset += pcmd->ElemCount;
		}
	}
	context.EndRenderPass();
	Profiler::Instance()->End(&context);
}