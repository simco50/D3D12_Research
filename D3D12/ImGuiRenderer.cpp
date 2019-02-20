#include "stdafx.h"
#include "ImGuiRenderer.h"
#include <fstream>
#include "CommandContext.h"
#include "External/Imgui/imgui.h"
#include "Graphics.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Shader.h"

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
	io.DisplaySize = ImVec2((float)1240, (float)720);
	ImGui::NewFrame();
}

void ImGuiRenderer::Render(CommandContext& context)
{
	//Copy the new data to the buffers
	ImGui::Render();
	ImDrawData* pDrawData = ImGui::GetDrawData();

	if (pDrawData->CmdListsCount == 0)
	{
		return;
	}

	context.GetCommandList()->SetPipelineState(m_pPipelineState->GetPipelineState());
	context.GetCommandList()->SetGraphicsRootSignature(m_pRootSignature->GetRootSignature());
	Matrix projectionMatrix = XMMatrixOrthographicOffCenterLH(0.0f, (float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight(), 0.0f, 0.0f, 1.0f);
	context.SetDynamicConstantBufferView(0, &projectionMatrix, sizeof(Matrix));
	context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.SetViewport(FloatRect(0, 0, (float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight()), 0, 1);

	int vertexOffset = 0;
	int indexOffset = 0;
	for (int n = 0; n < pDrawData->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = pDrawData->CmdLists[n];
		context.SetDynamicVertexBuffer(0, cmd_list->VtxBuffer.Size, sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
		context.SetDynamicIndexBuffer(cmd_list->IdxBuffer.Size, cmd_list->IdxBuffer.Data);

		const ImDrawList* pCmdList = pDrawData->CmdLists[n];
		for (int cmd_i = 0; cmd_i < pCmdList->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &pCmdList->CmdBuffer[cmd_i];
			if (pcmd->UserCallback)
				pcmd->UserCallback(pCmdList, pcmd);
			else
			{
				context.SetScissorRect(FloatRect(pcmd->ClipRect.x, pcmd->ClipRect.y, pcmd->ClipRect.z, pcmd->ClipRect.w));
				context.DrawIndexed(pcmd->ElemCount, indexOffset, vertexOffset);
			}
			indexOffset += pcmd->ElemCount;
		}
		vertexOffset += pCmdList->VtxBuffer.Size;
	}
}

void ImGuiRenderer::CreatePipeline()
{
	//Shaders
	Shader vertexShader, pixelShader;
	vertexShader.Load("Resources/ImGui.hlsl", Shader::Type::VertexShader, "VSMain");
	pixelShader.Load("Resources/ImGui.hlsl", Shader::Type::PixelShader, "PSMain");

	//Root signature
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	m_pRootSignature = std::make_unique<RootSignature>(1);
	(*m_pRootSignature)[0].AsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_pRootSignature->Finalize(m_pGraphics->GetDevice(), rootSignatureFlags);

	//Input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> elementDesc;
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });

	m_pPipelineState = std::make_unique<PipelineState>();
	m_pPipelineState->SetBlendMode(BlendMode::ALPHA, false);
	m_pPipelineState->SetDepthWrite(false);
	m_pPipelineState->SetDepthEnabled(true);
	m_pPipelineState->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
	m_pPipelineState->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
	m_pPipelineState->SetRootSignature(m_pRootSignature->GetRootSignature());
	m_pPipelineState->SetInputLayout(elementDesc.data(), elementDesc.size());
	m_pPipelineState->Finalize(m_pGraphics->GetDevice());
}

void ImGuiRenderer::InitializeImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();
	unsigned char* pPixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pPixels, &width, &height);
}
