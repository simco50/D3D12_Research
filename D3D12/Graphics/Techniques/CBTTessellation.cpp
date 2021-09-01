#include "stdafx.h"
#include "CBTTessellation.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/SceneView.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "Core/Input.h"
#include "CBT.h"
#include "Imgui/imgui_internal.h"

CBTTessellation::CBTTessellation(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	SetupPipelines();
	m_CBT.Init(m_MaxDepth, 1);
}

void CBTTessellation::Execute(RGGraph& graph, Texture* pRenderTarget, Texture* pDepthTexture, const SceneView& resources)
{
	static bool debugVisualize = false;
	static bool cpuDemo = false;

	Matrix terrainTransform = Matrix::CreateScale(30, 3, 30) * Matrix::CreateTranslation(-15, 0, -15);

	ImGui::Begin("Parameters");
	ImGui::Text("CBT");
	if (ImGui::SliderInt("CBT Depth", &m_MaxDepth, 10, 30))
	{
		m_CBT.InitBare(m_MaxDepth, 1);
		m_IsDirty = true;
	}
	ImGui::Checkbox("Debug Visualize", &debugVisualize);
	ImGui::Checkbox("CPU Demo", &cpuDemo);
	ImGui::End();

	if (cpuDemo)
	{
		DemoCpuCBT();
	}

	RG_GRAPH_SCOPE("CBT", graph);

	if (m_IsDirty)
	{
		RGPassBuilder cbtUpload = graph.AddPass("CBT Upload");
		cbtUpload.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				uint32 size = m_CBT.GetMemoryUse();
				m_pCBTBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(size, BufferFlag::ShaderResource | BufferFlag::UnorderedAccess), "CBT");
				m_pCBTBuffer->SetData(&context, m_CBT.GetData(), size);
				context.FlushResourceBarriers();
			});
		m_IsDirty = false;
	}

	RGPassBuilder cbtUpdate = graph.AddPass("CBT Update");
	cbtUpdate.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(m_pCBTBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.SetComputeRootSignature(m_pCBTRS.get());

			struct CommonArgs
			{
				uint32 NumElements;
			} commonArgs;
			commonArgs.NumElements = (uint32)m_pCBTBuffer->GetSize() / sizeof(uint32);
			context.SetComputeDynamicConstantBufferView(0, commonArgs);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
			context.BindResource(3, 0, m_pHeightmap->GetSRV());

			struct SubdivisionData
			{
				Matrix Transform;
				Matrix View;
				Matrix ViewProjection;
				Vector4 FrustumPlanes[6];
			} subdivisionData;
			subdivisionData.View = resources.pCamera->GetView();
			subdivisionData.ViewProjection = resources.pCamera->GetViewProjection();
			subdivisionData.Transform = terrainTransform;
			DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
			resources.pCamera->GetFrustum().GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
			subdivisionData.FrustumPlanes[0] = Vector4(nearPlane);
			subdivisionData.FrustumPlanes[1] = Vector4(farPlane);
			subdivisionData.FrustumPlanes[2] = Vector4(left);
			subdivisionData.FrustumPlanes[3] = Vector4(right);
			subdivisionData.FrustumPlanes[4] = Vector4(top);
			subdivisionData.FrustumPlanes[5] = Vector4(bottom);

			context.SetComputeDynamicConstantBufferView(1, subdivisionData);

			context.SetPipelineState(m_pCBTUpdatePSO);
			context.ExecuteIndirect(m_pDevice->GetIndirectDispatchSignature(), 1, m_pCBTIndirectArgs.get(), nullptr);
			context.InsertUavBarrier();
		});

	RGPassBuilder cbtSumReduction = graph.AddPass("CBT Sum Reduction");
	cbtSumReduction.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct CommonArgs
			{
				uint32 NumElements;
			} commonArgs;
			commonArgs.NumElements = (uint32)m_pCBTBuffer->GetSize() / sizeof(uint32);
			context.SetComputeDynamicConstantBufferView(0, commonArgs);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

			for (int32 currentDepth = m_MaxDepth - 1; currentDepth >= 0; --currentDepth)
			{
				struct SumReductionData
				{
					uint32 Depth;
				} reductionArgs;
				reductionArgs.Depth = currentDepth;
				context.SetComputeDynamicConstantBufferView(1, reductionArgs);

				context.SetPipelineState(m_pCBTSumReductionPSO);
				context.Dispatch(ComputeUtils::GetNumThreadGroups(1 << currentDepth, 64));
				context.InsertUavBarrier();
			}
		});

	RGPassBuilder cbtIndirectArgs = graph.AddPass("CBT Update Indirect Args");
	cbtIndirectArgs.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct CommonArgs
			{
				uint32 NumElements;
			} commonArgs;
			commonArgs.NumElements = (uint32)m_pCBTBuffer->GetSize() / sizeof(uint32);
			context.SetComputeDynamicConstantBufferView(0, commonArgs);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
			context.BindResource(2, 1, m_pCBTIndirectArgs->GetUAV());

			context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.SetPipelineState(m_pCBTIndirectArgsPSO);
			context.Dispatch(1);
		});

	RGPassBuilder cbtRender = graph.AddPass("CBT Render");
	cbtRender.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			context.SetGraphicsRootSignature(m_pCBTRS.get());
			context.SetPipelineState(m_pCBTRenderPSO);
			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			struct CommonArgs
			{
				uint32 NumElements;
			} commonArgs;
			commonArgs.NumElements = (uint32)m_pCBTBuffer->GetSize() / sizeof(uint32);
			context.SetGraphicsDynamicConstantBufferView(0, commonArgs);

			struct RenderArgs
			{
				Matrix Transform;
				Matrix ViewProjection;
				float HeightmapSizeInv;
			} renderArgs;
			renderArgs.Transform = terrainTransform;
			renderArgs.ViewProjection = resources.pCamera->GetViewProjection();
			renderArgs.HeightmapSizeInv = 1.0f / m_pHeightmap->GetWidth();
			context.SetGraphicsDynamicConstantBufferView(1, renderArgs);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
			context.BindResource(3, 0, m_pHeightmap->GetSRV());

			context.BeginRenderPass(RenderPassInfo(pRenderTarget, RenderPassAccess::Load_Store, pDepthTexture, RenderPassAccess::Load_Store, true));
			context.ExecuteIndirect(m_pDevice->GetIndirectDrawSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, sizeof(uint32) * 3);
			context.EndRenderPass();
		});

	if (debugVisualize)
	{
		ImGui::Begin("CBT");
		ImGui::ImageAutoSize(m_pDebugVisualizeTexture.get(), ImVec2((float)m_pDebugVisualizeTexture->GetWidth(), (float)m_pDebugVisualizeTexture->GetHeight()));
		ImGui::End();

		RGPassBuilder cbtDebugVisualize = graph.AddPass("CBT Debug Visualize");
		cbtDebugVisualize.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
				context.InsertResourceBarrier(m_pDebugVisualizeTexture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

				context.SetGraphicsRootSignature(m_pCBTRS.get());
				context.SetPipelineState(m_pCBTDebugVisualizePSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				struct CommonArgs
				{
					uint32 NumElements;
				} commonArgs;
				commonArgs.NumElements = (uint32)m_pCBTBuffer->GetSize() / sizeof(uint32);
				context.SetGraphicsDynamicConstantBufferView(0, commonArgs);

				context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

				context.BeginRenderPass(RenderPassInfo(m_pDebugVisualizeTexture.get(), RenderPassAccess::Load_Store, nullptr, RenderPassAccess::NoAccess, false));
				context.ExecuteIndirect(m_pDevice->GetIndirectDrawSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, sizeof(uint32) * 3);
				context.EndRenderPass();
			});
	}
}

void CBTTessellation::SetupPipelines()
{
	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	m_pHeightmap = std::make_unique<Texture>(m_pDevice);
	m_pHeightmap->Create(pContext, "Resources/Terrain.dds");
	pContext->Execute(true);

	m_pCBTRS = std::make_unique<RootSignature>(m_pDevice);
	m_pCBTRS->FinalizeFromShader("CBT", m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionCS"));

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "PrepareDispatchArgsCS"));
		psoDesc.SetName("CBT Indirect Args");
		m_pCBTIndirectArgsPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionCS"));
		psoDesc.SetName("CBT Sum Reduction");
		m_pCBTSumReductionPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "UpdateCS"));
		psoDesc.SetName("CBT Update");
		m_pCBTUpdatePSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetVertexShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Vertex, "RenderVS"));
		psoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "RenderPS"));
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Draw CBT");
		m_pCBTRenderPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetMeshShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Mesh, "RenderMS"));
		psoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "RenderPS"));
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Draw CBT");
		m_pCBTRenderMeshShaderPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "DebugVisualizePS"));
		psoDesc.SetVertexShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Vertex, "DebugVisualizeVS"));
		psoDesc.SetRenderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Debug Visualize CBT");
		m_pCBTDebugVisualizePSO = m_pDevice->CreatePipeline(psoDesc);
	}

	m_pDebugVisualizeTexture = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource), "CBT Visualize Texture");

	m_pCBTIndirectArgs = m_pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(7), "CBT Indirect Args");
}

void CBTTessellation::DemoCpuCBT()
{
	PROFILE_SCOPE("CPU CBT Demo");

	ImGui::Begin("CBT Demo");

	static int maxDepth = 4;
	static bool init = false;

	static CBT cbt;
	if (ImGui::SliderInt("Max Depth", &maxDepth, 2, 16) || !init)
	{
		cbt.Init(maxDepth, 1);
		init = true;
	}

	static bool splitting = true;
	static bool merging = true;
	static bool alwaysUpdate = false;
	ImGui::Checkbox("Always Update", &alwaysUpdate);
	ImGui::SameLine();
	ImGui::Checkbox("Splitting", &splitting);
	ImGui::SameLine();
	ImGui::Checkbox("Merging", &merging);
	ImGui::SameLine();

	ImGui::Text("Size: %s", Math::PrettyPrintDataSize(cbt.GetMemoryUse()).c_str());

	ImVec2 cPos = ImGui::GetCursorScreenPos();
	float scale = 600;

	const float itemWidth = 20;
	const float itemSpacing = 3;
	ImDrawList* bgList = ImGui::GetWindowDrawList();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing, itemSpacing));

	uint32 heapID = 1;
	for (uint32 d = 0; d < cbt.GetMaxDepth(); ++d)
	{
		ImGui::Spacing();
		for (uint32 j = 0; j < Math::Exp2(d); ++j)
		{
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			cursor += ImVec2(itemWidth, itemWidth * 0.5f);
			float rightChildPos = (itemWidth + itemSpacing) * ((1u << (cbt.GetMaxDepth() - d - 1)) - 0.5f);

			ImGui::PushID(heapID);
			ImGui::Button(Sprintf("%d", cbt.GetData(heapID)).c_str(), ImVec2(itemWidth, itemWidth));
			bgList->AddLine(cursor, ImVec2(cursor.x + rightChildPos, cursor.y), 0xFFFFFFFF);
			bgList->AddLine(ImVec2(cursor.x - itemWidth * 0.5f, cursor.y + itemWidth * 0.5f), ImVec2(cursor.x - itemWidth * 0.5f, cursor.y + itemWidth * 0.5f + itemSpacing), 0xFFFFFFFF);
			bgList->AddLine(ImVec2(cursor.x + rightChildPos, cursor.y), ImVec2(cursor.x + rightChildPos, cursor.y + itemWidth * 0.5f + itemSpacing), 0xFFFFFFFF);
			ImGui::SameLine();
			ImGui::Spacing();
			ImGui::SameLine(0, (itemWidth + itemSpacing) * ((1u << (cbt.GetMaxDepth() - d)) - 1));
			ImGui::PopID();
			++heapID;
		}
	}

	ImGui::Spacing();
	ImGui::Separator();

	for (uint32 leafIndex = 0; leafIndex < cbt.NumBitfieldBits(); ++leafIndex)
	{
		ImGui::PushID(10000 + leafIndex);
		uint32 index = (int)Math::Exp2(cbt.GetMaxDepth()) + leafIndex;
		if (ImGui::Button(Sprintf("%d", cbt.GetData(index)).c_str(), ImVec2(itemWidth, itemWidth)))
		{
			cbt.SetData(index, !cbt.GetData(index));
		}
		ImGui::SameLine();
		ImGui::PopID();
	}

	ImGui::PopStyleVar();
	ImGui::Spacing();

	cPos = ImGui::GetCursorScreenPos();

	ImGui::GetWindowDrawList()->AddQuadFilled(
		cPos + ImVec2(0, 0),
		cPos + ImVec2(scale, 0),
		cPos + ImVec2(scale, scale),
		cPos + ImVec2(0, scale),
		ImColor(1.0f, 1.0f, 1.0f, 0.3f));

	if (alwaysUpdate || Input::Instance().IsMouseDown(VK_LBUTTON))
	{
		PROFILE_SCOPE("CBT Update");
		cbt.IterateLeaves([&](uint32 heapIndex)
			{
				Vector2 relMousePos = Input::Instance().GetMousePosition() - Vector2(cPos.x, cPos.y);

				if (splitting && LEB::PointInTriangle(relMousePos, heapIndex, scale))
				{
					LEB::CBTSplitConformed(cbt, heapIndex);
				}

				if (!CBT::IsRootNode(heapIndex))
				{
					LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
					if (merging && !LEB::PointInTriangle(relMousePos, diamond.Base, scale) && !LEB::PointInTriangle(relMousePos, diamond.Top, scale))
					{
						LEB::CBTMergeConformed(cbt, heapIndex);
					}
				}
			});
	}
	cbt.SumReduction();

	auto LEBTriangle = [&](uint32 heapIndex, Color color, float scale)
	{
		Vector3 a, b, c;
		LEB::GetTriangleVertices(heapIndex, a, b, c);
		a *= scale;
		b *= scale;
		c *= scale;

		ImGui::GetWindowDrawList()->AddTriangle(
			cPos + ImVec2(a.x, a.y),
			cPos + ImVec2(b.x, b.y),
			cPos + ImVec2(c.x, c.y),
			ImColor(color.x, color.y, color.z, color.w));

		ImVec2 pos = (ImVec2(a.x, a.y) + ImVec2(b.x, b.y) + ImVec2(c.x, c.y)) / 3;
		std::string text = Sprintf("%d", heapIndex);
		ImGui::GetWindowDrawList()->AddText(cPos + pos - ImGui::CalcTextSize(text.c_str()) * 0.5f, ImColor(1.0f, 1.0f, 1.0f, 0.1f), text.c_str());
	};

	PROFILE_SCOPE("CBT Draw");
	cbt.IterateLeaves([&](uint32 heapIndex)
		{
			LEBTriangle(heapIndex, Color(1, 0, 0, 0.5f), scale);
		});

	ImGui::End();
}
