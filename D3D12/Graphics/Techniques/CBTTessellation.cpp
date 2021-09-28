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

constexpr uint32 IndirectDispatchArgsOffset = 0;
constexpr uint32 IndirectDispatchMeshArgsOffset = IndirectDispatchArgsOffset + sizeof(D3D12_DISPATCH_ARGUMENTS);
constexpr uint32 IndirectDrawArgsOffset = IndirectDispatchMeshArgsOffset + sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);

namespace CBTSettings
{
	static int CBTDepth = 25;
	static bool FreezeCamera = false;
	static bool DebugVisualize = false;
	static bool CpuDemo = false;
	static bool MeshShader = true;
	static float ScreenSizeBias = 8.7f;
	static float HeightmapVarianceBias = 0.01f;
	static float HeightScale = 0.1f;

	//PSO Settings
	static bool ColorLevels = false;
	static bool Wireframe = true;
	static bool FrustumCull = true;
	static bool DisplacementLOD = true;
	static bool DistanceLOD = true;
	static bool AlwaysSubdivide = false;
	static int MeshShaderSubD = 3;
	static int GeometryShaderSubD = 2;
}

CBTTessellation::CBTTessellation(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (!pDevice->GetCapabilities().SupportsMeshShading())
	{
		CBTSettings::MeshShader = false;
	}

	CreateResources();
	SetupPipelines();
	AllocateCBT();
}

void CBTTessellation::Execute(RGGraph& graph, Texture* pRenderTarget, Texture* pDepthTexture, const SceneView& resources)
{
	float scale = 100;
	Matrix terrainTransform = Matrix::CreateScale(scale, scale * CBTSettings::HeightScale, scale) * Matrix::CreateTranslation(-scale * 0.5f, -10, -scale * 0.5f);

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("CBT"))
		{
			ImGui::SliderFloat("Height Scale", &CBTSettings::HeightScale, 0.1f, 2.0f);
			if (ImGui::SliderInt("CBT Depth", &CBTSettings::CBTDepth, 10, 28))
			{
				AllocateCBT();
			}
			int& subd = CBTSettings::MeshShader ? CBTSettings::MeshShaderSubD : CBTSettings::GeometryShaderSubD;
			int maxSubD = CBTSettings::MeshShader ? 3 : 2;
			if (ImGui::SliderInt("Triangle SubD", &subd, 0, maxSubD))
			{
				SetupPipelines();
			}
			ImGui::SliderFloat("Screen Size Bias", &CBTSettings::ScreenSizeBias, 0, 15);
			ImGui::SliderFloat("Heightmap Variance Bias", &CBTSettings::HeightmapVarianceBias, 0, 0.1f);
			ImGui::Checkbox("Debug Visualize", &CBTSettings::DebugVisualize);
			ImGui::Checkbox("CPU Demo", &CBTSettings::CpuDemo);
			if (m_pDevice->GetCapabilities().SupportsMeshShading())
			{
				ImGui::Checkbox("Mesh Shader", &CBTSettings::MeshShader);
			}
			ImGui::Checkbox("Freeze Camera", &CBTSettings::FreezeCamera);
			if (ImGui::Checkbox("Wireframe", &CBTSettings::Wireframe))
			{
				SetupPipelines();
			}
			if (ImGui::Checkbox("Color Levels", &CBTSettings::ColorLevels))
			{
				SetupPipelines();
			}
			if (ImGui::Checkbox("Frustum Cull", &CBTSettings::FrustumCull))
			{
				SetupPipelines();
			}
			if (ImGui::Checkbox("Displacement LOD", &CBTSettings::DisplacementLOD))
			{
				SetupPipelines();
			}
			if (ImGui::Checkbox("Distance LOD", &CBTSettings::DistanceLOD))
			{
				SetupPipelines();
			}
			if (ImGui::Checkbox("Always Subdivide", &CBTSettings::AlwaysSubdivide))
			{
				SetupPipelines();
			}
		}
	}
	ImGui::End();

	if (CBTSettings::CpuDemo)
	{
		DemoCpuCBT();
	}

	RG_GRAPH_SCOPE("CBT", graph);

	if (!CBTSettings::FreezeCamera)
	{
		m_CachedFrustum = resources.pCamera->GetFrustum();
		m_CachedViewMatrix = resources.pCamera->GetView();
	}

	struct CommonArgs
	{
		uint32 NumElements;
	} commonArgs;
	commonArgs.NumElements = (uint32)m_pCBTBuffer->GetSize() / sizeof(uint32);

	struct UpdateData
	{
		Matrix World;
		Matrix WorldView;
		Matrix WorldViewProjection;
		Vector4 FrustumPlanes[6];
		float HeightmapSizeInv;
		float ScreenSizeBias;
		float HeightmapVarianceBias;
	} updateData;
	updateData.WorldView = terrainTransform * m_CachedViewMatrix;
	updateData.WorldViewProjection = terrainTransform * resources.pCamera->GetViewProjection();
	updateData.World = terrainTransform;
	DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
	m_CachedFrustum.GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
	updateData.FrustumPlanes[0] = Vector4(nearPlane);
	updateData.FrustumPlanes[1] = Vector4(farPlane);
	updateData.FrustumPlanes[2] = Vector4(left);
	updateData.FrustumPlanes[3] = Vector4(right);
	updateData.FrustumPlanes[4] = Vector4(top);
	updateData.FrustumPlanes[5] = Vector4(bottom);
	updateData.HeightmapSizeInv = 1.0f / m_pHeightmap->GetWidth();
	updateData.ScreenSizeBias = CBTSettings::ScreenSizeBias;
	updateData.HeightmapVarianceBias = CBTSettings::HeightmapVarianceBias;

	if (m_IsDirty)
	{
		RGPassBuilder cbtUpload = graph.AddPass("CBT Upload");
		cbtUpload.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				m_pCBTBuffer->SetData(&context, m_CBT.GetData(), m_CBT.GetMemoryUse());
				context.FlushResourceBarriers();
			});
		m_IsDirty = false;
	}
	if (!CBTSettings::MeshShader)
	{
		RGPassBuilder cbtUpdate = graph.AddPass("CBT Update");
		cbtUpdate.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(m_pCBTBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pCBTIndirectArgs.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
				context.SetComputeRootSignature(m_pCBTRS.get());

				context.SetComputeDynamicConstantBufferView(0, commonArgs);
				context.SetComputeDynamicConstantBufferView(1, updateData);

				context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
				context.BindResource(3, 0, m_pHeightmap->GetSRV());

				context.SetPipelineState(m_pCBTUpdatePSO);
				context.ExecuteIndirect(m_pDevice->GetIndirectDispatchSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDispatchArgsOffset);
				context.InsertUavBarrier(m_pCBTBuffer.get());
			});
	}

	RGPassBuilder cbtIndirectArgs = graph.AddPass("CBT Update Indirect Args");
	cbtIndirectArgs.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pCBTRS.get());
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
			context.SetPipelineState(CBTSettings::MeshShader ? m_pCBTRenderMeshShaderPSO : m_pCBTRenderPSO);

			context.SetGraphicsDynamicConstantBufferView(0, commonArgs);
			context.SetGraphicsDynamicConstantBufferView(1, updateData);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());
			context.BindResource(3, 0, m_pHeightmap->GetSRV());

			context.BeginRenderPass(RenderPassInfo(pRenderTarget, RenderPassAccess::Load_Store, pDepthTexture, RenderPassAccess::Load_Store, true));
			if (CBTSettings::MeshShader)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.ExecuteIndirect(m_pDevice->GetIndirectDispatchMeshSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDispatchMeshArgsOffset);
			}
			else
			{
				context.SetPrimitiveTopology(CBTSettings::GeometryShaderSubD > 0 ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.ExecuteIndirect(m_pDevice->GetIndirectDrawSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDrawArgsOffset);
			}
			context.EndRenderPass();
		});

	RGPassBuilder cbtSumReductionPrepass = graph.AddPass("CBT Sum Reduction Prepass");
	cbtSumReductionPrepass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pCBTRS.get());
			context.SetComputeDynamicConstantBufferView(0, commonArgs);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

			struct SumReductionData
			{
				uint32 Depth;
			} reductionArgs;
			int32 currentDepth = CBTSettings::CBTDepth;

			reductionArgs.Depth = currentDepth;
			context.SetComputeDynamicConstantBufferView(1, reductionArgs);

			context.SetPipelineState(m_pCBTSumReductionFirstPassPSO);
			context.Dispatch(ComputeUtils::GetNumThreadGroups(1u << currentDepth, 256 * 32));
			context.InsertUavBarrier(m_pCBTBuffer.get());
		});

	RGPassBuilder cbtSumReduction = graph.AddPass("CBT Sum Reduction");
	cbtSumReduction.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pCBTRS.get());
			context.SetComputeDynamicConstantBufferView(0, commonArgs);

			context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

			struct SumReductionData
			{
				uint32 Depth;
			} reductionArgs;
			int32 currentDepth = CBTSettings::CBTDepth - 5;

			for (currentDepth = currentDepth - 1; currentDepth >= 0; --currentDepth)
			{
				reductionArgs.Depth = currentDepth;
				context.SetComputeDynamicConstantBufferView(1, reductionArgs);

				context.SetPipelineState(m_pCBTSumReductionPSO);
				context.Dispatch(ComputeUtils::GetNumThreadGroups(1 << currentDepth, 256));
				context.InsertUavBarrier(m_pCBTBuffer.get());
			}
		});

	if (CBTSettings::DebugVisualize)
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

				context.SetGraphicsDynamicConstantBufferView(0, commonArgs);
				context.SetGraphicsDynamicConstantBufferView(1, updateData);

				context.BindResource(2, 0, m_pCBTBuffer->GetUAV());

				context.BeginRenderPass(RenderPassInfo(m_pDebugVisualizeTexture.get(), RenderPassAccess::Load_Store, nullptr, RenderPassAccess::NoAccess, false));
				context.ExecuteIndirect(m_pDevice->GetIndirectDrawSignature(), 1, m_pCBTIndirectArgs.get(), nullptr, IndirectDrawArgsOffset);
				context.EndRenderPass();
			});
	}
}

void CBTTessellation::AllocateCBT()
{
	m_CBT.InitBare(CBTSettings::CBTDepth, 1);
	uint32 size = m_CBT.GetMemoryUse();
	m_pCBTBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(size, BufferFlag::ShaderResource | BufferFlag::UnorderedAccess), "CBT");
	m_IsDirty = true;
}

void CBTTessellation::SetupPipelines()
{
	std::vector<ShaderDefine> defines = {
		ShaderDefine(Sprintf("RENDER_WIREFRAME=%d", CBTSettings::Wireframe ? 1 : 0).c_str()),
		ShaderDefine(Sprintf("FRUSTUM_CULL=%d", CBTSettings::FrustumCull ? 1 : 0).c_str()),
		ShaderDefine(Sprintf("DISPLACEMENT_LOD=%d", CBTSettings::DisplacementLOD ? 1 : 0).c_str()),
		ShaderDefine(Sprintf("DISTANCE_LOD=%d", CBTSettings::DistanceLOD ? 1 : 0).c_str()),
		ShaderDefine(Sprintf("DEBUG_ALWAYS_SUBDIVIDE=%d", CBTSettings::AlwaysSubdivide ? 1 : 0).c_str()),
		ShaderDefine(Sprintf("MESH_SHADER_SUBD_LEVEL=%du", Math::Min(CBTSettings::MeshShaderSubD * 2, 6)).c_str()),
		ShaderDefine(Sprintf("GEOMETRY_SHADER_SUBD_LEVEL=%du", Math::Min(CBTSettings::GeometryShaderSubD * 2, 4)).c_str()),
		ShaderDefine(Sprintf("AMPLIFICATION_SHADER_SUBD_LEVEL=%du", Math::Max(CBTSettings::MeshShaderSubD * 2 - 6, 0)).c_str()),
		ShaderDefine(Sprintf("COLOR_LEVELS=%d", CBTSettings::ColorLevels ? 1 : 0).c_str()),
	};

	m_pCBTRS = std::make_unique<RootSignature>(m_pDevice);
	m_pCBTRS->FinalizeFromShader("CBT", m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionCS", defines));

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "PrepareDispatchArgsCS", defines));
		psoDesc.SetName("CBT Indirect Args");
		m_pCBTIndirectArgsPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionFirstPassCS", defines));
		psoDesc.SetName("CBT Sum Reduction First Pass");
		m_pCBTSumReductionFirstPassPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "SumReductionCS", defines));
		psoDesc.SetName("CBT Sum Reduction");
		m_pCBTSumReductionPSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetComputeShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Compute, "UpdateCS", defines));
		psoDesc.SetName("CBT Update");
		m_pCBTUpdatePSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetVertexShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Vertex, "RenderVS", defines));
		if (CBTSettings::GeometryShaderSubD > 0)
		{
			psoDesc.SetGeometryShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Geometry, "RenderGS", defines));
			psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		}
		else
		{
			psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		}
		psoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "RenderPS", defines));
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Draw CBT");
		m_pCBTRenderPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	if (m_pDevice->GetCapabilities().SupportsMeshShading())
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetAmplificationShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Amplification, "UpdateAS", defines));
		psoDesc.SetMeshShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Mesh, "RenderMS", defines));
		psoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "RenderPS", defines));
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Draw CBT");
		m_pCBTRenderMeshShaderPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS->GetRootSignature());
		psoDesc.SetPixelShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Pixel, "DebugVisualizePS", defines));
		psoDesc.SetVertexShader(m_pDevice->GetShader("CBT.hlsl", ShaderType::Vertex, "DebugVisualizeVS", defines));
		psoDesc.SetRenderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("Debug Visualize CBT");
		m_pCBTDebugVisualizePSO = m_pDevice->CreatePipeline(psoDesc);
	}
}

void CBTTessellation::CreateResources()
{
	CommandContext* pContext = m_pDevice->AllocateCommandContext();
	m_pHeightmap = std::make_unique<Texture>(m_pDevice);
	m_pHeightmap->Create(pContext, "Resources/Terrain.dds");
	pContext->Execute(true);

	m_pDebugVisualizeTexture = m_pDevice->CreateTexture(TextureDesc::CreateRenderTarget(1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::ShaderResource), "CBT Visualize Texture");
	m_pCBTIndirectArgs = m_pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(10), "CBT Indirect Args");
}

void CBTTessellation::DemoCpuCBT()
{
	PROFILE_SCOPE("CPU CBT Demo");

	ImGui::Begin("CBT Demo");

	static float scale = 600;
	static int maxDepth = 7;
	static bool init = false;

	static CBT cbt;
	if (ImGui::SliderInt("Max Depth", &maxDepth, 5, 12) || !init)
	{
		cbt.Init(maxDepth, maxDepth);
		init = true;
	}
	ImGui::SliderFloat("Scale", &scale, 200, 1200);

	static bool splitting = true;
	static bool merging = true;
	ImGui::Checkbox("Splitting", &splitting);
	ImGui::SameLine();
	ImGui::Checkbox("Merging", &merging);
	ImGui::SameLine();

	ImGui::Text("Size: %s", Math::PrettyPrintDataSize(cbt.GetMemoryUse()).c_str());

	ImVec2 cPos = ImGui::GetCursorScreenPos();

	const float itemWidth = 20;
	const float itemSpacing = 3;
	ImDrawList* bgList = ImGui::GetWindowDrawList();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing, itemSpacing));

	uint32 heapID = 1;
	for (uint32 d = 0; d < cbt.GetMaxDepth(); ++d)
	{
		ImGui::Spacing();
		for (uint32 j = 0; j < 1u << d; ++j)
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
		uint32 index = (1u << cbt.GetMaxDepth()) + leafIndex;
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
	static Vector2 mousePos;
	Vector2 relMousePos = Input::Instance().GetMousePosition() - Vector2(cPos.x, cPos.y);
	bool inBounds = relMousePos.x > 0 && relMousePos.y > 0 && relMousePos.x < scale&& relMousePos.y < scale;
	if (inBounds && Input::Instance().IsMouseDown(VK_LBUTTON))
	{
		mousePos = Input::Instance().GetMousePosition() - Vector2(cPos.x, cPos.y);
	}

	{
		PROFILE_SCOPE("CBT Update");
		cbt.IterateLeaves([&](uint32 heapIndex)
			{
				if (splitting && LEB::PointInTriangle(mousePos, heapIndex, scale))
				{
					LEB::CBTSplitConformed(cbt, heapIndex);
				}

				if (!CBT::IsRootNode(heapIndex))
				{
					LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
					if (merging && !LEB::PointInTriangle(mousePos, diamond.Base, scale) && !LEB::PointInTriangle(mousePos, diamond.Top, scale))
					{
						LEB::CBTMergeConformed(cbt, heapIndex);
					}
				}
			});

		cbt.SumReduction();
	}

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
			ImColor(color.x, color.y, color.z, color.w),
			2.0f);

		ImVec2 pos = (ImVec2(a.x, a.y) + ImVec2(b.x, b.y) + ImVec2(c.x, c.y)) / 3;
		std::string text = Sprintf("%d", heapIndex);
		ImGui::GetWindowDrawList()->AddText(cPos + pos - ImGui::CalcTextSize(text.c_str()) * 0.5f, ImColor(1.0f, 1.0f, 1.0f, 0.3f), text.c_str());
	};

	{
		PROFILE_SCOPE("CBT Draw");
		ImGui::GetWindowDrawList()->AddQuadFilled(
			cPos + ImVec2(0, 0),
			cPos + ImVec2(scale, 0),
			cPos + ImVec2(scale, scale),
			cPos + ImVec2(0, scale),
			ImColor(1.0f, 1.0f, 1.0f, 0.3f));

		cbt.IterateLeaves([&](uint32 heapIndex)
			{
				LEBTriangle(heapIndex, Color(1, 1, 1, 1), scale);
			});

		ImGui::GetWindowDrawList()->AddCircleFilled(cPos + ImVec2(mousePos.x, mousePos.y), 8, 0xFF0000FF, 20);
		ImGui::GetWindowDrawList()->AddCircle(cPos + ImVec2(mousePos.x, mousePos.y), 14, 0xFF0000FF, 20, 2.0f);
	}

	ImGui::End();
}
