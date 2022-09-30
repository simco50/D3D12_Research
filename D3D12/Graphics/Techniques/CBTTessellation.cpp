#include "stdafx.h"
#include "CBTTessellation.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/SceneView.h"
#include "Graphics/Profiler.h"
#include "Core/Input.h"
#include "CBT.h"
#include "imgui_internal.h"

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
	static float ScreenSizeBias = 10.5f;
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

	CreateResources(pDevice);
	SetupPipelines(pDevice);
}

void CBTTessellation::Execute(RGGraph& graph, CBTData& data, const SceneView* pView, SceneTextures& sceneTextures)
{
	float scale = 100;
	Matrix terrainTransform = Matrix::CreateScale(scale, scale * CBTSettings::HeightScale, scale) * Matrix::CreateTranslation(-scale * 0.5f, -1.5f, -scale * 0.5f);

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("CBT"))
		{
			ImGui::SliderFloat("Height Scale", &CBTSettings::HeightScale, 0.1f, 2.0f);
			if (ImGui::SliderInt("CBT Depth", &CBTSettings::CBTDepth, 10, 28))
			{
				data.pCBTBuffer = nullptr;
			}
			int& subd = CBTSettings::MeshShader ? CBTSettings::MeshShaderSubD : CBTSettings::GeometryShaderSubD;
			int maxSubD = CBTSettings::MeshShader ? 3 : 2;
			if (ImGui::SliderInt("Triangle SubD", &subd, 0, maxSubD))
			{
				SetupPipelines(m_pDevice);
			}
			ImGui::SliderFloat("Screen Size Bias", &CBTSettings::ScreenSizeBias, 0, 15);
			ImGui::SliderFloat("Heightmap Variance Bias", &CBTSettings::HeightmapVarianceBias, 0, 0.1f);
			ImGui::Checkbox("Debug Visualize", &CBTSettings::DebugVisualize);
			ImGui::Checkbox("CPU Demo", &CBTSettings::CpuDemo);
			if (m_pDevice->GetCapabilities().SupportsMeshShading())
			{
				ImGui::Checkbox("Mesh Shader", &CBTSettings::MeshShader);
			}
			if (ImGui::Checkbox("Wireframe", &CBTSettings::Wireframe))
			{
				SetupPipelines(m_pDevice);
			}
			if (ImGui::Checkbox("Color Levels", &CBTSettings::ColorLevels))
			{
				SetupPipelines(m_pDevice);
			}
			if (ImGui::Checkbox("Frustum Cull", &CBTSettings::FrustumCull))
			{
				SetupPipelines(m_pDevice);
			}
			if (ImGui::Checkbox("Displacement LOD", &CBTSettings::DisplacementLOD))
			{
				SetupPipelines(m_pDevice);
			}
			if (ImGui::Checkbox("Distance LOD", &CBTSettings::DistanceLOD))
			{
				SetupPipelines(m_pDevice);
			}
			if (ImGui::Checkbox("Always Subdivide", &CBTSettings::AlwaysSubdivide))
			{
				SetupPipelines(m_pDevice);
			}
		}
	}
	ImGui::End();

	if (CBTSettings::CpuDemo)
	{
		CBTDemo();
	}

	RG_GRAPH_SCOPE("CBT", graph);

	RGBuffer* pCBTBuffer = graph.TryImportBuffer(data.pCBTBuffer);

	if (!pCBTBuffer)
	{
		uint32 size = CBT::ComputeSize(CBTSettings::CBTDepth);
		data.pCBTBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(size, BufferFlag::ShaderResource | BufferFlag::UnorderedAccess), "CBT");
		pCBTBuffer = graph.ImportBuffer(data.pCBTBuffer);

		graph.AddPass("CBT Upload", RGPassFlag::Compute)
			.Write({ pCBTBuffer })
			.Bind([=](CommandContext& context)
				{
					CBT cbt;
					cbt.InitBare(CBTSettings::CBTDepth, 1);
					context.InsertResourceBarrier(pCBTBuffer->Get(), D3D12_RESOURCE_STATE_COPY_DEST);
					context.WriteBuffer(pCBTBuffer->Get(), cbt.GetData(), size);
				});
	}

	struct CommonArgs
	{
		uint32 NumElements;
		int32 HeightmapIndex;
		int32 CBTIndex;
		int32 IndirectArgsIndex;
	} commonArgs;
	commonArgs.NumElements = (uint32)data.pCBTBuffer->GetSize() / sizeof(uint32);
	commonArgs.HeightmapIndex = data.pHeightmap->GetSRVIndex();
	commonArgs.CBTIndex = data.pCBTBuffer->GetUAVIndex();
	commonArgs.IndirectArgsIndex = data.pCBTIndirectArgs->GetUAVIndex();

	struct UpdateData
	{
		Matrix World;
		float HeightmapSizeInv;
		float ScreenSizeBias;
		float HeightmapVarianceBias;
		uint32 SplitMode;
	} updateData;
	updateData.World = terrainTransform;
	updateData.HeightmapSizeInv = 1.0f / data.pHeightmap->GetWidth();
	updateData.ScreenSizeBias = CBTSettings::ScreenSizeBias;
	updateData.HeightmapVarianceBias = CBTSettings::HeightmapVarianceBias;
	updateData.SplitMode = data.SplitMode;
	data.SplitMode = 1 - data.SplitMode;

	RGBuffer* pIndirectArgs = graph.ImportBuffer(data.pCBTIndirectArgs);

	if (!CBTSettings::MeshShader)
	{
		graph.AddPass("CBT Update", RGPassFlag::Compute)
			.Write({ pCBTBuffer })
			.Read({ pIndirectArgs })
			.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCBTRS);

				context.SetRootConstants(0, commonArgs);
				context.SetRootCBV(1, updateData);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView));

				context.SetPipelineState(m_pCBTUpdatePSO);
				context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pIndirectArgs->Get(), nullptr, IndirectDispatchArgsOffset);
				context.InsertUavBarrier(pCBTBuffer->Get());
			});
	}

	graph.AddPass("CBT Update Indirect Args", RGPassFlag::Compute)
		.Read({ pCBTBuffer })
		.Write({ pIndirectArgs })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCBTRS);
				context.SetRootConstants(0, commonArgs);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView));

				context.SetPipelineState(m_pCBTIndirectArgsPSO);
				context.Dispatch(1);
			});

	graph.AddPass("CBT Render", RGPassFlag::Raster)
		.Write(pCBTBuffer)
		.Read(pIndirectArgs)
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, true)
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::Load)
		.RenderTarget(sceneTextures.pNormals, RenderTargetLoadAction::Load)
		.RenderTarget(sceneTextures.pRoughness, RenderTargetLoadAction::Load)
		.Bind([=](CommandContext& context)
			{
				context.SetGraphicsRootSignature(m_pCBTRS);
				context.SetPipelineState(CBTSettings::MeshShader ? m_pCBTRenderMeshShaderPSO : m_pCBTRenderPSO);

				context.SetRootConstants(0, commonArgs);
				context.SetRootCBV(1, updateData);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));

				if (CBTSettings::MeshShader)
				{
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pIndirectArgs->Get(), nullptr, IndirectDispatchMeshArgsOffset);
				}
				else
				{
					context.SetPrimitiveTopology(CBTSettings::GeometryShaderSubD > 0 ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pIndirectArgs->Get(), nullptr, IndirectDrawArgsOffset);
				}
			});

	// No longer need to compute the sum reduction tree for the last 5 layers. Instead, bits in the bitfield are counted directly
#if 0
	graph.AddPass("CBT Sum Reduction Prepass", RGPassFlag::Compute)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.SetComputeRootSignature(m_pCBTRS);
				context.SetComputeRootConstants(0, commonArgs);

				struct SumReductionData
				{
					uint32 Depth;
				} reductionArgs;
				int32 currentDepth = CBTSettings::CBTDepth;

				reductionArgs.Depth = currentDepth;
				context.SetComputeDynamicConstantBufferView(1, reductionArgs);

				context.SetPipelineState(m_pCBTSumReductionFirstPassPSO);
				context.Dispatch(ComputeUtils::GetNumThreadGroups(1u << currentDepth, 256 * 32));
				context.InsertUavBarrier(m_pCBTBuffer);
			});
#endif

	// Because the bits in the bitfield are counted directly, we need a snapshot of the bitfield before subdivision starts
	// Cache the bitfield in the second to last layer as it is unused memory now.

	graph.AddPass("CBT Cache Bitfield", RGPassFlag::Compute)
		.Write(pCBTBuffer)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCBTRS);
				context.SetRootConstants(0, commonArgs);

				struct SumReductionData
				{
					uint32 Depth;
				} reductionArgs;
				int32 currentDepth = CBTSettings::CBTDepth;

				reductionArgs.Depth = currentDepth;
				context.SetRootCBV(1, reductionArgs);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView));

				context.SetPipelineState(m_pCBTCacheBitfieldPSO);
				context.Dispatch(ComputeUtils::GetNumThreadGroups(1u << currentDepth, 256 * 32));
			});

	graph.AddPass("CBT Sum Reduction", RGPassFlag::Compute)
		.Write(pCBTBuffer)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCBTRS);
				context.SetRootConstants(0, commonArgs);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView));

				struct SumReductionData
				{
					uint32 Depth;
				} reductionArgs;
				int32 currentDepth = CBTSettings::CBTDepth - 5;

				for (currentDepth = currentDepth - 1; currentDepth >= 0; --currentDepth)
				{
					reductionArgs.Depth = currentDepth;
					context.SetRootCBV(1, reductionArgs);

					context.SetPipelineState(m_pCBTSumReductionPSO);
					context.Dispatch(ComputeUtils::GetNumThreadGroups(1 << currentDepth, 256));
					context.InsertUavBarrier(pCBTBuffer->Get());
				}
			});

	if (CBTSettings::DebugVisualize)
	{
		ImGui::Begin("CBT");
		ImGui::ImageAutoSize(data.pDebugVisualizeTexture, ImVec2((float)data.pDebugVisualizeTexture->GetWidth(), (float)data.pDebugVisualizeTexture->GetHeight()));
		ImGui::End();

		graph.AddPass("CBT Debug Visualize", RGPassFlag::Raster)
			.Read({ pCBTBuffer, pIndirectArgs })
			.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(data.pDebugVisualizeTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);

				context.BeginRenderPass(RenderPassInfo(data.pDebugVisualizeTexture, RenderPassAccess::Load_Store, nullptr, RenderPassAccess::NoAccess, false));

				context.SetGraphicsRootSignature(m_pCBTRS);
				context.SetPipelineState(m_pCBTDebugVisualizePSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				context.SetRootConstants(0, commonArgs);
				context.SetRootCBV(1, updateData);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView, data.pDebugVisualizeTexture));

				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pIndirectArgs->Get(), nullptr, IndirectDrawArgsOffset);
				context.EndRenderPass();
			});
	}
}

void CBTTessellation::SetupPipelines(GraphicsDevice* pDevice)
{
	std::vector<ShaderDefine> defines = {
		ShaderDefine("RENDER_WIREFRAME", CBTSettings::Wireframe ? 1 : 0),
		ShaderDefine("FRUSTUM_CULL", CBTSettings::FrustumCull ? 1 : 0),
		ShaderDefine("DISPLACEMENT_LOD", CBTSettings::DisplacementLOD ? 1 : 0),
		ShaderDefine("DISTANCE_LOD", CBTSettings::DistanceLOD ? 1 : 0),
		ShaderDefine("DEBUG_ALWAYS_SUBDIVIDE", CBTSettings::AlwaysSubdivide ? 1 : 0),
		ShaderDefine("MESH_SHADER_SUBD_LEVEL", Math::Min(CBTSettings::MeshShaderSubD * 2, 6)),
		ShaderDefine("GEOMETRY_SHADER_SUBD_LEVEL", Math::Min(CBTSettings::GeometryShaderSubD * 2, 4)),
		ShaderDefine("AMPLIFICATION_SHADER_SUBD_LEVEL", Math::Max(CBTSettings::MeshShaderSubD * 2 - 6, 0)),
		ShaderDefine("COLOR_LEVELS", CBTSettings::ColorLevels ? 1 : 0),
	};

	m_pCBTRS = new RootSignature(pDevice);
	m_pCBTRS->AddRootConstants<Vector4i>(0);
	m_pCBTRS->AddConstantBufferView(1);
	m_pCBTRS->AddConstantBufferView(100);
	m_pCBTRS->Finalize("CBT");

	{
		m_pCBTIndirectArgsPSO = pDevice->CreateComputePipeline(m_pCBTRS, "CBT.hlsl", "PrepareDispatchArgsCS", defines);
		m_pCBTSumReductionFirstPassPSO = pDevice->CreateComputePipeline(m_pCBTRS, "CBT.hlsl", "SumReductionFirstPassCS", defines);
		m_pCBTSumReductionPSO = pDevice->CreateComputePipeline(m_pCBTRS, "CBT.hlsl", "SumReductionCS", defines);
		m_pCBTCacheBitfieldPSO = pDevice->CreateComputePipeline(m_pCBTRS, "CBT.hlsl", "CacheBitfieldCS", defines);
		m_pCBTUpdatePSO = pDevice->CreateComputePipeline(m_pCBTRS, "CBT.hlsl", "UpdateCS", defines);
	}

	constexpr ResourceFormat formats[] = {
		ResourceFormat::RGBA16_FLOAT,
		ResourceFormat::RG16_FLOAT,
		ResourceFormat::R8_UNORM,
	};

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS);
		psoDesc.SetVertexShader("CBT.hlsl", "RenderVS", defines);
		if (CBTSettings::GeometryShaderSubD > 0)
		{
			psoDesc.SetGeometryShader("CBT.hlsl", "RenderGS", defines);
			psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		}
		else
		{
			psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		}
		psoDesc.SetPixelShader("CBT.hlsl", "RenderPS", defines);
		psoDesc.SetRenderTargetFormats(formats, ResourceFormat::D32_FLOAT, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Draw CBT");
		m_pCBTRenderPSO = pDevice->CreatePipeline(psoDesc);
	}

	if (pDevice->GetCapabilities().SupportsMeshShading())
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS);
		psoDesc.SetAmplificationShader("CBT.hlsl", "UpdateAS", defines);
		psoDesc.SetMeshShader("CBT.hlsl", "RenderMS", defines);
		psoDesc.SetPixelShader("CBT.hlsl", "RenderPS", defines);
		psoDesc.SetRenderTargetFormats(formats, ResourceFormat::D32_FLOAT, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Draw CBT");
		m_pCBTRenderMeshShaderPSO = pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCBTRS);
		psoDesc.SetPixelShader("CBT.hlsl", "DebugVisualizePS", defines);
		psoDesc.SetVertexShader("CBT.hlsl", "DebugVisualizeVS", defines);
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("Debug Visualize CBT");
		m_pCBTDebugVisualizePSO = pDevice->CreatePipeline(psoDesc);
	}
}

void CBTTessellation::CreateResources(GraphicsDevice* pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext();
	m_CBTData.pHeightmap = GraphicsCommon::CreateTextureFromFile(*pContext, "Resources/Terrain.dds", false, "Terrain Heightmap");
	pContext->Execute(true);

	m_CBTData.pDebugVisualizeTexture = pDevice->CreateTexture(TextureDesc::CreateRenderTarget(1024, 1024, ResourceFormat::RGBA8_UNORM, TextureFlag::ShaderResource), "CBT Visualize Texture");
	m_CBTData.pCBTIndirectArgs = pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(10), "CBT Indirect Args");
}

void CBTTessellation::CBTDemo()
{
	PROFILE_SCOPE("CPU CBT Demo");

	ImGui::Begin("CBT Demo");

	static float scale = 600;
	static int maxDepth = 7;
	static bool init = false;
	static bool splitMode = true;

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

	ImGui::Text("Size: %s", Math::PrettyPrintDataSize(CBT::ComputeSize(cbt.GetMaxDepth())).c_str());

	ImVec2 cPos = ImGui::GetCursorScreenPos();

	const float itemWidth = 20;
	const float itemSpacing = 3;
	ImDrawList* bgList = ImGui::GetWindowDrawList();

	if (maxDepth < 10)
	{
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
	}

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
				if (splitMode)
				{
					if (splitting && LEB::PointInTriangle(mousePos, heapIndex, scale))
					{
						LEB::CBTSplitConformed(cbt, heapIndex);
					}
				}
				else
				{

					if (!CBT::IsRootNode(heapIndex))
					{
						LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
						if (merging && !LEB::PointInTriangle(mousePos, diamond.Base, scale) && !LEB::PointInTriangle(mousePos, diamond.Top, scale))
						{
							LEB::CBTMergeConformed(cbt, heapIndex);
						}
					}
				}
			});

		splitMode = !splitMode;

		cbt.SumReduction();
	}

	auto LEBTriangle = [&](uint32 heapIndex, Color color, float scale)
	{
		Vector3 a, b, c;
		LEB::GetTriangleVertices(heapIndex, a, b, c);
		a *= scale;
		b *= scale;
		c *= scale;

		srand(CBT::GetDepth(heapIndex));

		ImGui::GetWindowDrawList()->AddTriangleFilled(
			cPos + ImVec2(a.x, a.y),
			cPos + ImVec2(b.x, b.y),
			cPos + ImVec2(c.x, c.y),
			ImColor(Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), 0.5f)
		);

		ImGui::GetWindowDrawList()->AddTriangle(
			cPos + ImVec2(a.x, a.y),
			cPos + ImVec2(b.x, b.y),
			cPos + ImVec2(c.x, c.y),
			ImColor(color.x, color.y, color.z, color.w),
			2.0f
		);

		if (maxDepth < 10)
		{
		ImVec2 pos = (ImVec2(a.x, a.y) + ImVec2(b.x, b.y) + ImVec2(c.x, c.y)) / 3;
		std::string text = Sprintf("%d", heapIndex);
		ImGui::GetWindowDrawList()->AddText(cPos + pos - ImGui::CalcTextSize(text.c_str()) * 0.5f, ImColor(1.0f, 1.0f, 1.0f, 0.3f), text.c_str());
		}
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
