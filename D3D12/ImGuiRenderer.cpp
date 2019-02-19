#include "stdafx.h"
#include "ImGuiRenderer.h"
#include <fstream>
#include "CommandContext.h"
#include "External/Imgui/imgui.h"
#include "Graphics.h"

ImGuiRenderer::ImGuiRenderer(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	ComPtr<ID3DBlob> pVertexShader, pPixelShader;
	LoadShaders("Resources/ImGui.hlsl", &pVertexShader, &pPixelShader);
	CreateRootSignature();
	CreatePipelineState(pVertexShader, pPixelShader);
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

	context.GetCommandList()->SetPipelineState(m_pPipelineState.Get());
	context.GetCommandList()->SetGraphicsRootSignature(m_pRootSignature.Get());
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

void ImGuiRenderer::LoadShaders(const char* pFilePath, ComPtr<ID3DBlob>* pVertexShaderCode, ComPtr<ID3DBlob>* pPixelShaderCode)
{
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	uint32 compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32 compileFlags = 0;
#endif
	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

	std::ifstream file(pFilePath, std::ios::ate);
	if (file.fail())
	{
		return;
	}
	int size = (int)file.tellg();
	std::vector<char> data(size);
	file.seekg(0);
	file.read(data.data(), data.size());
	file.close();

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, 0, nullptr, 0, (*pVertexShaderCode).GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return;
	}
	pErrorBlob.Reset();

	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, 0, nullptr, 0, (*pPixelShaderCode).GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return;
	}
}

void ImGuiRenderer::CreateRootSignature()
{
	//Root signature
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_STATIC_SAMPLER_DESC samplerDesc = CD3DX12_STATIC_SAMPLER_DESC(0);
	desc.Init_1_1(1, rootParameters, 1, &samplerDesc, rootSignatureFlags);

	ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
	HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
	HR(m_pGraphics->GetDevice()->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
}

void ImGuiRenderer::CreatePipelineState(const ComPtr<ID3DBlob>& pVertexShaderCode, const ComPtr<ID3DBlob>& pPixelShaderCode)
{
	//Input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> elementDesc;
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	elementDesc.push_back(D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });

	//PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState.AlphaToCoverageEnable = false;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.NumRenderTargets = 1;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderCode.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderCode.Get());
	psoDesc.InputLayout.NumElements = (uint32)elementDesc.size();
	psoDesc.InputLayout.pInputElementDescs = elementDesc.data();
	psoDesc.pRootSignature = m_pRootSignature.Get();
	psoDesc.SampleMask = UINT_MAX;

	HR(m_pGraphics->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf())));
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
