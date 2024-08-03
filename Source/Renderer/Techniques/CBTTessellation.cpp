#include "stdafx.h"
#include "CBTTessellation.h"
#include "Core/Profiler.h"
#include "Core/Input.h"
#include "RHI/RootSignature.h"
#include "RHI/Device.h"
#include "RHI/PipelineState.h"
#include "RHI/Buffer.h"
#include "RHI/CommandContext.h"
#include "RenderGraph/RenderGraph.h"
#include "Renderer/Techniques/CBT.h"
#include "Renderer/Techniques/ImGuiRenderer.h"
#include "Renderer/Renderer.h"
#include "Scene/World.h"

namespace CBTSettings
{
	static int CBTDepth = 25;
	static bool FreezeCamera = false;
	static bool DebugVisualize = false;
	static bool CpuDemo = false;
	static bool MeshShader = true;
	static float ScreenSizeBias = 10.5f;
	static float HeightmapVarianceBias = 0.1f;
	static float HeightScale = 10.0f;
	static float PlaneScale = 100.0f;

	//PSO Settings
	static bool FrustumCull = true;
	static bool DisplacementLOD = true;
	static bool DistanceLOD = true;
	static bool AlwaysSubdivide = false;
	static int SubD = 2;
}

CBTTessellation::CBTTessellation(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (!pDevice->GetCapabilities().SupportsMeshShading())
	{
		CBTSettings::MeshShader = false;
	}

	SetupPipelines(pDevice);
}

void CBTTessellation::SetupPipelines(GraphicsDevice* pDevice)
{
	ShaderDefineHelper defines;
	defines.Set("FRUSTUM_CULL", CBTSettings::FrustumCull);
	defines.Set("DISPLACEMENT_LOD", CBTSettings::DisplacementLOD);
	defines.Set("DISTANCE_LOD", CBTSettings::DistanceLOD);
	defines.Set("DEBUG_ALWAYS_SUBDIVIDE", CBTSettings::AlwaysSubdivide);
	defines.Set("GEOMETRY_SUBD_LEVEL", Math::Min(CBTSettings::SubD * 2, 6));
	defines.Set("AMPLIFICATION_SHADER_SUBD_LEVEL", Math::Max(CBTSettings::SubD * 2 - 6, 0));

	{
		m_pCBTIndirectArgsPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "CBT.hlsl", "PrepareDispatchArgsCS", *defines);
		m_pCBTSumReductionPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "CBT.hlsl", "SumReductionCS", *defines);
		m_pCBTCacheBitfieldPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "CBT.hlsl", "CacheBitfieldCS", *defines);
		m_pCBTUpdatePSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "CBT.hlsl", "UpdateCS", *defines);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetVertexShader("CBT.hlsl", "RenderVS", *defines);
		psoDesc.SetRenderTargetFormats({}, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Raster CBT");
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, 0x0, (uint8)StencilBit::SurfaceTypeMask);
		m_pCBTRenderPSO = pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetVertexShader("FullScreenTriangle.hlsl", "WithTexCoordVS");
		psoDesc.SetPixelShader("CBT.hlsl", "ShadePS", *defines);
		psoDesc.SetRenderTargetFormats(GraphicsCommon::GBufferFormat, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_EQUAL, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, (uint8)StencilBit::Terrain, 0x0);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("CBT Shading");
		m_pCBTShadePSO = m_pDevice->CreatePipeline(psoDesc);
	}

	if (pDevice->GetCapabilities().SupportsMeshShading())
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetAmplificationShader("CBT.hlsl", "UpdateAS", *defines);
		psoDesc.SetMeshShader("CBT.hlsl", "RenderMS", *defines);
		psoDesc.SetRenderTargetFormats({}, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetName("Draw CBT");
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, 0x0, (uint8)StencilBit::SurfaceTypeMask);
		m_pCBTRenderMeshShaderPSO = pDevice->CreatePipeline(psoDesc);
	}

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetPixelShader("CBT.hlsl", "DebugVisualizePS", *defines);
		psoDesc.SetVertexShader("CBT.hlsl", "DebugVisualizeVS", *defines);
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::Unknown, 1);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("Debug Visualize CBT");
		m_pCBTDebugVisualizePSO = pDevice->CreatePipeline(psoDesc);
	}
}

struct IndirectDrawArgs
{
	D3D12_DISPATCH_ARGUMENTS UpdateDispatchArgs;
	D3D12_DISPATCH_MESH_ARGUMENTS DispatchMeshArgs;
	D3D12_DRAW_ARGUMENTS DrawArgs;
	D3D12_DRAW_ARGUMENTS DebugDrawArgs;
};

void CBTTessellation::RasterMain(RGGraph& graph, const RenderView* pView, const SceneTextures& sceneTextures)
{
	if (ImGui::Begin("Settings"))
	{
		if (ImGui::CollapsingHeader("CBT"))
		{
			bool invalidatePSOs = false;

			ImGui::SliderFloat("Height Scale", &CBTSettings::HeightScale, 1.0f, 40.0f);

			invalidatePSOs |= ImGui::SliderInt("Triangle SubD", &CBTSettings::SubD, 0, 3);
			ImGui::SliderFloat("Screen Size Bias", &CBTSettings::ScreenSizeBias, 0, 15);
			ImGui::SliderFloat("Heightmap Variance Bias", &CBTSettings::HeightmapVarianceBias, 0, 1.0f);
			ImGui::Checkbox("Debug Visualize", &CBTSettings::DebugVisualize);
			ImGui::Checkbox("CPU Demo", &CBTSettings::CpuDemo);
			if (m_pDevice->GetCapabilities().SupportsMeshShading())
				ImGui::Checkbox("Mesh Shader", &CBTSettings::MeshShader);

			invalidatePSOs |= ImGui::Checkbox("Frustum Cull", &CBTSettings::FrustumCull);
			invalidatePSOs |= ImGui::Checkbox("Displacement LOD", &CBTSettings::DisplacementLOD);
			invalidatePSOs |= ImGui::Checkbox("Distance LOD", &CBTSettings::DistanceLOD);
			invalidatePSOs |= ImGui::Checkbox("Always Subdivide", &CBTSettings::AlwaysSubdivide);

			if (invalidatePSOs)
				SetupPipelines(m_pDevice);
		}
	}
	ImGui::End();

	if (CBTSettings::CpuDemo)
	{
		CBTDemo();
	}

	RG_GRAPH_SCOPE("CBT", graph);

	auto cbt_view = pView->pWorld->Registry.view<CBTData>();
	cbt_view.each([&](CBTData& cbtData)
		{
			RGBuffer* pCBTBuffer = graph.TryImport(cbtData.pCBTBuffer);
			if (!pCBTBuffer)
			{
				uint32 size = CBT::ComputeSize(CBTSettings::CBTDepth);
				pCBTBuffer = RGUtils::CreatePersistent(graph, "CBT", BufferDesc::CreateByteAddress(size, BufferFlag::ShaderResource | BufferFlag::UnorderedAccess), &cbtData.pCBTBuffer, true);

				graph.AddPass("CBT Upload", RGPassFlag::Copy)
					.Write({ pCBTBuffer })
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							CBT cbt;
							cbt.InitBare(CBTSettings::CBTDepth, 1);
							ScratchAllocation alloc = context.AllocateScratch(size);
							memcpy(alloc.pMappedMemory, cbt.GetData(), size);
							context.CopyBuffer(alloc.pBackingResource, resources.Get(pCBTBuffer), alloc.Size, alloc.Offset, 0);
						});
			}
			cbtData.pCBT = pCBTBuffer;

			struct
			{
				float HeightScale;
				float PlaneScale;
				uint32 NumCBTElements;
			} commonArgs;
			commonArgs.HeightScale = CBTSettings::HeightScale;
			commonArgs.PlaneScale = CBTSettings::PlaneScale;
			commonArgs.NumCBTElements = (uint32)pCBTBuffer->GetDesc().Size / sizeof(uint32);

			RGBuffer* pIndirectArgs = RGUtils::CreatePersistent(graph, "CBT.IndirectArgs", BufferDesc::CreateIndirectArguments<IndirectDrawArgs>(1, BufferFlag::UnorderedAccess), &cbtData.pCBTIndirectArgs, true);

			if (!CBTSettings::MeshShader)
			{
				graph.AddPass("CBT Update", RGPassFlag::Compute)
					.Write({ pCBTBuffer })
					.Read({ pIndirectArgs })
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

							Renderer::BindViewUniforms(context, *pView);
							struct
							{
								float ScreenSizeBias;
								float HeightmapVarianceBias;
								uint32 SplitMode;
							} updateParams;
							updateParams.ScreenSizeBias = CBTSettings::ScreenSizeBias;
							updateParams.HeightmapVarianceBias = CBTSettings::HeightmapVarianceBias;
							updateParams.SplitMode = cbtData.SplitMode;

							context.BindRootCBV(BindingSlot::PerInstance, updateParams);
							context.BindRootCBV(BindingSlot::PerPass, commonArgs);
							context.BindResources(BindingSlot::UAV, {
								resources.GetUAV(pCBTBuffer),
								});

							context.SetPipelineState(m_pCBTUpdatePSO);
							context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, resources.Get(pIndirectArgs), nullptr, offsetof(IndirectDrawArgs, UpdateDispatchArgs));
							context.InsertUAVBarrier(resources.Get(pCBTBuffer));
						});
			}

			// Because the bits in the bitfield are counted directly, we need a snapshot of the bitfield before subdivision starts
			// Cache the bitfield in the second to last layer as it is unused memory now.
			// Also required by sum reduction pass
			graph.AddPass("CBT Cache Bitfield", RGPassFlag::Compute)
				.Write(pCBTBuffer)
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

						Renderer::BindViewUniforms(context, *pView);
						struct
						{
							uint32 Depth;
							uint32 NumCBTElements;
						} reductionArgs;
						int32 currentDepth = CBTSettings::CBTDepth;

						reductionArgs.NumCBTElements = (uint32)pCBTBuffer->GetDesc().Size / sizeof(uint32);
						reductionArgs.Depth = currentDepth;
						context.BindRootCBV(BindingSlot::PerInstance, reductionArgs);
						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pCBTBuffer),
							});

						context.SetPipelineState(m_pCBTCacheBitfieldPSO);
						context.Dispatch(ComputeUtils::GetNumThreadGroups(1u << currentDepth, 256 * 32));
						context.InsertUAVBarrier(resources.Get(pCBTBuffer));
					});

			graph.AddPass("CBT Sum Reduction", RGPassFlag::Compute)
				.Write(pCBTBuffer)
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

						Renderer::BindViewUniforms(context, *pView);

						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pCBTBuffer),
							});

						struct SumReductionData
						{
							uint32 Depth;
							uint32 NumCBTElements;
						} reductionArgs;
						int32 currentDepth = CBTSettings::CBTDepth - 5;

						reductionArgs.NumCBTElements = (uint32)pCBTBuffer->GetDesc().Size / sizeof(uint32);

						for (currentDepth = currentDepth - 1; currentDepth >= 0; --currentDepth)
						{
							reductionArgs.Depth = currentDepth;
							context.BindRootCBV(BindingSlot::PerInstance, reductionArgs);

							context.SetPipelineState(m_pCBTSumReductionPSO);
							context.Dispatch(ComputeUtils::GetNumThreadGroups(1 << currentDepth, 256));
							context.InsertUAVBarrier(resources.Get(pCBTBuffer));
						}
					});

			graph.AddPass("CBT Update Indirect Args", RGPassFlag::Compute)
				.Write({ pCBTBuffer, pIndirectArgs })
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

						Renderer::BindViewUniforms(context, *pView);

						struct
						{
							uint32 NumCBTElements;
						} params;
						params.NumCBTElements = (uint32)pCBTBuffer->GetDesc().Size / sizeof(uint32);

						context.BindRootCBV(BindingSlot::PerInstance, params);
						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pCBTBuffer),
							resources.GetUAV(pIndirectArgs),
							});
						context.SetPipelineState(m_pCBTIndirectArgsPSO);
						context.Dispatch(1);
						context.InsertUAVBarrier(resources.Get(pCBTBuffer));
					});

			// Amplification + Mesh shader variant performs subdivision used for the next frame while rendering with the subdivision state of the previous frame.
			graph.AddPass("CBT Render", RGPassFlag::Raster)
				.Write(pCBTBuffer)
				.Read(pIndirectArgs)
				.DepthStencil(sceneTextures.pDepth)
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(CBTSettings::MeshShader ? m_pCBTRenderMeshShaderPSO : m_pCBTRenderPSO);
						context.SetStencilRef((uint32)StencilBit::Terrain);
						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

						Renderer::BindViewUniforms(context, *pView);

						struct
						{
							float ScreenSizeBias;
							float HeightmapVarianceBias;
							uint32 SplitMode;
						} updateParams;
						updateParams.ScreenSizeBias = CBTSettings::ScreenSizeBias;
						updateParams.HeightmapVarianceBias = CBTSettings::HeightmapVarianceBias;
						updateParams.SplitMode = cbtData.SplitMode;

						context.BindRootCBV(BindingSlot::PerInstance, updateParams);
						context.BindRootCBV(BindingSlot::PerPass, commonArgs);
						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pCBTBuffer),
							});

						if (CBTSettings::MeshShader)
							context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, resources.Get(pIndirectArgs), nullptr, offsetof(IndirectDrawArgs, DispatchMeshArgs));
						else
							context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, resources.Get(pIndirectArgs), nullptr, offsetof(IndirectDrawArgs, DrawArgs));
					});

			if (CBTSettings::DebugVisualize)
			{
				if (cbtData.pDebugVisualizeTexture)
				{
					ImGui::Begin("CBT");
					ImVec2 size = ImGui::GetAutoSize(ImVec2((float)cbtData.pDebugVisualizeTexture->GetWidth(), (float)cbtData.pDebugVisualizeTexture->GetHeight()));
					ImGui::Image(cbtData.pDebugVisualizeTexture, size);
					ImGui::End();
				}

				RGTexture* pVisualizeTarget = RGUtils::CreatePersistent(graph, "CBT Visualize Texture", TextureDesc::Create2D(1024, 1024, ResourceFormat::RGBA8_UNORM, 1, TextureFlag::ShaderResource), &cbtData.pDebugVisualizeTexture, true);
				graph.AddPass("CBT Debug Visualize", RGPassFlag::Raster)
					.Read({ pIndirectArgs })
					.Write({ pCBTBuffer })
					.RenderTarget(pVisualizeTarget)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pCBTDebugVisualizePSO);
							context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

							Renderer::BindViewUniforms(context, *pView);

							struct
							{
								uint32 NumCBTElements;
							} params;
							params.NumCBTElements = (uint32)pCBTBuffer->GetDesc().Size / sizeof(uint32);

							context.BindRootCBV(BindingSlot::PerInstance, params);
							context.BindResources(BindingSlot::UAV, {
								resources.GetUAV(pCBTBuffer),
								});

							context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, resources.Get(pIndirectArgs), nullptr, offsetof(IndirectDrawArgs, DebugDrawArgs));
						});
			}

			cbtData.SplitMode = 1 - cbtData.SplitMode;
		});
}

void CBTTessellation::Shade(RGGraph& graph, const RenderView* pView, const SceneTextures& sceneTextures, RGTexture* pFog)
{
	struct
	{
		float HeightScale;
		float PlaneScale;
		uint32 NumCBTElements;
	} commonArgs;
	commonArgs.HeightScale = CBTSettings::HeightScale;
	commonArgs.PlaneScale = CBTSettings::PlaneScale;

	graph.AddPass("CBT Shade", RGPassFlag::Raster)
		.Read({ pFog, sceneTextures.pDepth })
		.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::ReadOnly)
		.RenderTarget(sceneTextures.pColorTarget)
		.RenderTarget(sceneTextures.pNormals)
		.RenderTarget(sceneTextures.pRoughness)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pCBTShadePSO);
				context.SetStencilRef((uint32)StencilBit::Terrain);

				Renderer::BindViewUniforms(context, *pView);

				context.BindRootCBV(BindingSlot::PerPass, commonArgs);
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(sceneTextures.pDepth),
					resources.GetSRV(pFog),
					});

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.Draw(0, 3);
			});
}

void CBTTessellation::CBTDemo()
{
	PROFILE_CPU_SCOPE();

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
		PROFILE_CPU_SCOPE("CBT Update");
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
		String text = Sprintf("%d", heapIndex);
		ImGui::GetWindowDrawList()->AddText(cPos + pos - ImGui::CalcTextSize(text.c_str()) * 0.5f, ImColor(1.0f, 1.0f, 1.0f, 0.3f), text.c_str());
		}
	};

	{
		PROFILE_CPU_SCOPE("CBT Draw");
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
