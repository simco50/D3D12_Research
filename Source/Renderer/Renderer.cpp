#include "stdafx.h"
#include "Renderer.h"
#include "Core/Image.h"
#include "Core/TaskQueue.h"
#include "Core/CommandLine.h"
#include "Core/Paths.h"
#include "Core/Input.h"
#include "Core/ConsoleVariables.h"
#include "Core/Utils.h"
#include "Core/Profiler.h"

#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/CommandContext.h"
#include "RHI/Shader.h"
#include "RHI/PipelineState.h"
#include "RHI/ShaderBindingTable.h"
#include "RHI/StateObject.h"
#include "RHI/RingBufferAllocator.h"

#include "Renderer/Mesh.h"
#include "Renderer/Light.h"
#include "Renderer/Techniques/DebugRenderer.h"
#include "Renderer/Techniques/GpuParticles.h"
#include "Renderer/Techniques/RTAO.h"
#include "Renderer/Techniques/ForwardRenderer.h"
#include "Renderer/Techniques/VolumetricFog.h"
#include "Renderer/Techniques/RTReflections.h"
#include "Renderer/Techniques/PathTracing.h"
#include "Renderer/Techniques/SSAO.h"
#include "Renderer/Techniques/CBTTessellation.h"
#include "Renderer/Techniques/Clouds.h"
#include "Renderer/Techniques/ShaderDebugRenderer.h"
#include "Renderer/Techniques/MeshletRasterizer.h"
#include "Renderer/Techniques/VisualizeTexture.h"
#include "Renderer/Techniques/LightCulling.h"
#include "Renderer/Techniques/DDGI.h"
#include "Renderer/Techniques/ImGuiRenderer.h"

#include "RenderGraph/RenderGraph.h"

#include "Scene/World.h"
#include "Scene/Camera.h"

#include <imgui_internal.h>
#include <IconsFontAwesome4.h>

namespace Tweakables
{
	// Post processing
	ConsoleVariable gWhitePoint("r.Exposure.WhitePoint", 1.0f);
	ConsoleVariable gMinLogLuminance("r.Exposure.MinLogLuminance", -4.0f);
	ConsoleVariable gMaxLogLuminance("r.Exposure.MaxLogLuminance", 20.0f);
	ConsoleVariable gTau("r.Exposure.Tau", 2.0f);
	ConsoleVariable gDrawHistogram("r.Histogram", false);
	ConsoleVariable gToneMapper("r.Tonemapper", 2);
	ConsoleVariable gTAA("r.Taa", true);

	// Shadows
	ConsoleVariable gSDSM("r.Shadows.SDSM", false);
	ConsoleVariable gVisualizeShadowCascades("vis.ShadowCascades", false);
	ConsoleVariable gShadowCascades("r.Shadows.CascadeCount", 4);
	ConsoleVariable gPSSMFactor("r.Shadow.PSSMFactor", 0.85f);
	ConsoleVariable gShadowsGPUCull("r.Shadows.GPUCull", true);
	ConsoleVariable gShadowsOcclusionCulling("r.Shadows.OcclusionCull", true);
	ConsoleVariable gCullShadowsDebugStats("r.Shadows.CullingStats", -1);

	// Bloom
	ConsoleVariable gBloom("r.Bloom", true);
	ConsoleVariable gBloomIntensity("r.Bloom.Intensity", 1.0f);
	ConsoleVariable gBloomBlendFactor("r.Bloom.BlendFactor", 0.3f);
	ConsoleVariable gBloomInteralBlendFactor("r.Bloom.InteralBlendFactor", 0.85f);

	// Misc Lighting
	ConsoleVariable gSky("r.Sky", true);
	ConsoleVariable gVolumetricFog("r.VolumetricFog", true);
	ConsoleVariable gClouds("r.Clouds", true);
	ConsoleVariable gRaytracedAO("r.Raytracing.AO", false);
	ConsoleVariable gVisualizeLightDensity("vis.LightDensity", false);
	ConsoleVariable gEnableDDGI("r.DDGI", true);
	ConsoleVariable gVisualizeDDGI("vis.DDGI", false);
	ConsoleVariable gRenderObjectBounds("r.vis.ObjectBounds", false);

	ConsoleVariable gRaytracedReflections("r.Raytracing.Reflections", false);
	ConsoleVariable gSSRSamples("r.SSRSamples", 8);
	ConsoleVariable gRenderTerrain("r.Terrain", true);
	ConsoleVariable gOcclusionCulling("r.OcclusionCulling", true);
	ConsoleVariable gWorkGraph("r.WorkGraph", false);

	// Misc
	ConsoleVariable gVisibilityDebugMode("r.Raster.VisibilityDebug", 0);
	ConsoleVariable gCullDebugStats("r.CullingStats", false);

	// Render Graph
	ConsoleVariable gRenderGraphJobify("r.RenderGraph.Jobify", true);
	ConsoleVariable gRenderGraphResourceAliasing("r.RenderGraph.Aliasing", true);
	ConsoleVariable gRenderGraphPassCulling("r.RenderGraph.PassCulling", true);
	ConsoleVariable gRenderGraphStateTracking("r.RenderGraph.StateTracking", true);
	ConsoleVariable gRenderGraphPassGroupSize("r.RenderGraph.PassGroupSize", 10);
	ConsoleVariable gRenderGraphResourceTracker("r.RenderGraph.ResourceTracker", false);
	ConsoleVariable gRenderGraphPassView("r.RenderGraph.PassView", false);

	bool gDumpRenderGraphNextFrame = false;
	ConsoleCommand<> gDumpRenderGraph("DumpRenderGraph", []() { gDumpRenderGraphNextFrame = true; });

	String VisualizeTextureName = "";
	ConsoleCommand<const char*> gVisualizeTexture("vis", [](const char* pName) { VisualizeTextureName = pName; });
}

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

void Renderer::Init(GraphicsDevice* pDevice, World* pWorld)
{
	m_pDevice = pDevice;
	m_pWorld = pWorld;

	m_RenderGraphPool = std::make_unique<RGResourcePool>(m_pDevice);

	DebugRenderer::Get()->Initialize(m_pDevice);
	m_pShaderDebugRenderer	= std::make_unique<ShaderDebugRenderer>(m_pDevice);
	m_pMeshletRasterizer	= std::make_unique<MeshletRasterizer>(m_pDevice);
	m_pDDGI					= std::make_unique<DDGI>(m_pDevice);
	m_pClouds				= std::make_unique<Clouds>(m_pDevice);
	m_pVolumetricFog		= std::make_unique<VolumetricFog>(m_pDevice);
	m_pLightCulling			= std::make_unique<LightCulling>(m_pDevice);
	m_pForwardRenderer		= std::make_unique<ForwardRenderer>(m_pDevice);
	m_pRTReflections		= std::make_unique<RTReflections>(m_pDevice);
	m_pRTAO					= std::make_unique<RTAO>(m_pDevice);
	m_pSSAO					= std::make_unique<SSAO>(m_pDevice);
	m_pParticles			= std::make_unique<GpuParticles>(m_pDevice);
	m_pPathTracing			= std::make_unique<PathTracing>(m_pDevice);
	m_pCBTTessellation		= std::make_unique<CBTTessellation>(m_pDevice);
	m_pCaptureTextureSystem	= std::make_unique<CaptureTextureSystem>(m_pDevice);

	InitializePipelines();

	m_pShaderDebugRenderer->GetGPUData(&m_DebugRenderData);

	m_MainView.pRenderer	= this;
	m_MainView.pWorld		= pWorld;
	m_AccelerationStructure.Init(m_pDevice);

	m_pLensDirtTexture = GraphicsCommon::CreateTextureFromFile(m_pDevice, "Resources/Textures/LensDirt.dds", true, "Lens Dirt");
}

void Renderer::Shutdown()
{
	DebugRenderer::Get()->Shutdown();
}

void Renderer::Render(const Transform& cameraTransform, const Camera& camera, Texture* pTarget)
{
	uint32 w = pTarget->GetWidth();
	uint32 h = pTarget->GetHeight();

	if (w != m_MainView.Viewport.GetWidth() || h != m_MainView.Viewport.GetHeight())
	{
		m_MainView.Viewport = FloatRect(0, 0, (float)w, (float)h);
		m_MainView.CameraCut = true;
	}

	{
		PROFILE_CPU_SCOPE("Update");

		constexpr RenderPath defaultRenderPath = RenderPath::Clustered;
		if (m_RenderPath == RenderPath::Visibility)
			m_RenderPath = m_pDevice->GetCapabilities().SupportsMeshShading() ? m_RenderPath : defaultRenderPath;
		if (m_RenderPath == RenderPath::PathTracing)
			m_RenderPath = m_pDevice->GetCapabilities().SupportsRaytracing() ? m_RenderPath : defaultRenderPath;

		m_pDevice->GetShaderManager()->ConditionallyReloadShaders();

		m_RenderGraphPool->Tick();

		RenderPath newRenderPath = m_RenderPath;
		if (!ImGui::IsAnyItemActive())
		{
			if (Input::Instance().IsKeyPressed('1'))
				newRenderPath = RenderPath::Clustered;
			else if (Input::Instance().IsKeyPressed('2'))
				newRenderPath = RenderPath::Tiled;
			else if (Input::Instance().IsKeyPressed('3'))
				newRenderPath = RenderPath::Visibility;
			else if (Input::Instance().IsKeyPressed('4'))
				newRenderPath = RenderPath::VisibilityDeferred;
			else if (Input::Instance().IsKeyPressed('5'))
				newRenderPath = RenderPath::PathTracing;
		}
		if (newRenderPath == RenderPath::Visibility && !m_pDevice->GetCapabilities().SupportsMeshShading())
			newRenderPath = RenderPath::Clustered;
		if (newRenderPath == RenderPath::PathTracing && !m_pDevice->GetCapabilities().SupportsRaytracing())
			newRenderPath = RenderPath::Clustered;
		m_RenderPath = newRenderPath;

		Tweakables::gRaytracedAO = m_pDevice->GetCapabilities().SupportsRaytracing() && Tweakables::gRaytracedAO;
		Tweakables::gRaytracedReflections = m_pDevice->GetCapabilities().SupportsRaytracing() && Tweakables::gRaytracedReflections;

		if (Tweakables::gRenderObjectBounds)
		{
			for (const Batch& b : m_Batches)
			{
				DebugRenderer::Get()->AddBoundingBox(b.Bounds, Color(0.2f, 0.2f, 0.9f, 1.0f));
				DebugRenderer::Get()->AddSphere(b.Bounds.Center, b.Radius, 5, 5, Color(0.2f, 0.6f, 0.2f, 1.0f));
			}
		}

		{
			bool jitter = Tweakables::gTAA && m_RenderPath != RenderPath::PathTracing;
			ViewTransform& transform = m_MainView;

			// Update previous data
			transform.FoV				= camera.FOV;
			transform.PositionPrev		= transform.Position;
			transform.WorldToClipPrev	= transform.WorldToClip;
			transform.JitterPrev		= transform.Jitter;

			// Update current data
			transform.ViewToWorld = Matrix::CreateFromQuaternion(cameraTransform.Rotation) * Matrix::CreateTranslation(cameraTransform.Position);
			transform.ViewToWorld.Invert(transform.WorldToView);
			float aspect = transform.Viewport.GetWidth() / transform.Viewport.GetHeight();
			transform.ViewToClip				= Math::CreatePerspectiveMatrix(transform.FoV, aspect, transform.NearPlane, transform.FarPlane);
			transform.WorldToClipUnjittered		= transform.WorldToView * transform.ViewToClip;
			transform.ViewToClipUnjittered		= transform.ViewToClip;

			if (jitter)
			{
				constexpr Math::HaltonSequence<16, 2> x;
				constexpr Math::HaltonSequence<16, 3> y;

				transform.Jitter.x = (x[transform.JitterIndex] * 2.0f - 1.0f) / transform.Viewport.GetWidth();
				transform.Jitter.y = (y[transform.JitterIndex] * 2.0f - 1.0f) / transform.Viewport.GetHeight();
				transform.ViewToClip.m[2][0] += transform.Jitter.x;
				transform.ViewToClip.m[2][1] += transform.Jitter.y;
				++transform.JitterIndex;
			}
			else
			{
				transform.Jitter = Vector2::Zero;
			}

			transform.ViewToClip.Invert(transform.ClipToView);
			transform.WorldToClip			= transform.WorldToView * transform.ViewToClip;
			transform.PerspectiveFrustum	= Math::CreateBoundingFrustum(transform.ViewToClip, transform.WorldToView);
			transform.Position				= cameraTransform.Position;
		}

		// Directional light is expected to be at index 0
		m_pWorld->Registry.sort<Light>([](const Light& a, const Light& b) {
			return (int)a.Type < (int)b.Type;
			});

		CreateShadowViews(m_MainView);
	}
	{
		const RenderView* pView = &m_MainView;

		{
			TaskContext taskContext;

			{
				PROFILE_CPU_SCOPE("Distance Sort");

				// Sort
				auto CompareSort = [this](const Batch& a, const Batch& b)
					{
						float aDist = Vector3::DistanceSquared(a.Bounds.Center, m_MainView.Position);
						float bDist = Vector3::DistanceSquared(b.Bounds.Center, m_MainView.Position);
						if (a.BlendMode != b.BlendMode)
							return (int)a.BlendMode < (int)b.BlendMode;
						return EnumHasAnyFlags(a.BlendMode, Batch::Blending::AlphaBlend) ? bDist < aDist : aDist < bDist;
					};
				std::sort(m_Batches.begin(), m_Batches.end(), CompareSort);
			}

			// In Visibility Buffer mode, culling is done on the GPU.
			if (m_RenderPath != RenderPath::Visibility && m_RenderPath != RenderPath::VisibilityDeferred)
			{
				TaskQueue::Execute([&](int)
					{
						PROFILE_CPU_SCOPE("Frustum Cull Main");
						m_MainView.VisibilityMask.SetAll();
						BoundingFrustum frustum = pView->PerspectiveFrustum;
						for (const Batch& b : m_Batches)
						{
							m_MainView.VisibilityMask.AssignBit(b.InstanceID, frustum.Contains(b.Bounds));
						}
					}, taskContext);
			}
			if (!Tweakables::gShadowsGPUCull)
			{
				TaskQueue::ExecuteMany([&](TaskDistributeArgs args)
					{
						PROFILE_CPU_SCOPE("Frustum Cull Shadows");
						RenderView& shadowView = m_ShadowViews[args.JobIndex];
						shadowView.VisibilityMask.SetAll();
						for (const Batch& b : m_Batches)
						{
							shadowView.VisibilityMask.AssignBit(b.InstanceID, shadowView.IsInFrustum(b.Bounds));
						}
					}, taskContext, (uint32)m_ShadowViews.size(), 1);
			}

			TaskQueue::Join(taskContext);
		}

		{
			PROFILE_CPU_SCOPE("Flush GPU uploads");
			m_pDevice->GetRingBuffer()->Sync();
		}

		{
			CommandContext* pContext = m_pDevice->AllocateCommandContext();

			// Upload GPU scene data
			UploadSceneData(*pContext);

			// Build RTAS
			m_AccelerationStructure.Build(*pContext, m_InstanceBuffer.pBuffer, m_Batches);

			// Upload PerView uniforms
			Renderer::UploadViewUniforms(*pContext, m_MainView);

			pContext->Execute();
		}

		RGGraph graph;

		{
			RG_GRAPH_SCOPE("GPU Frame", graph);
			PROFILE_CPU_SCOPE("Record RenderGraph");

			{
				RG_GRAPH_SCOPE("Skinning", graph);
				PROFILE_CPU_SCOPE("Skinning");

				struct SkinningUpdateInfo
				{
					uint32 SkinMatrixOffset;
					uint32 PositionsOffset;
					uint32 NormalsOffset;
					uint32 JointsOffset;
					uint32 WeightsOffset;
					uint32 SkinnedPositionsOffset;
					uint32 SkinnedNormalsOffset;
					uint32 NumVertices;
				};
				Array<SkinningUpdateInfo> skinDatas;
				Array<Matrix> skinningTransforms;
				Array<Mesh*> meshes;

				auto view = m_pWorld->Registry.view<const Model>();
				view.each([&](const Model& model)
					{
						if (model.SkeletonIndex != -1)
						{
							SkinningUpdateInfo& skinData = skinDatas.emplace_back();

							Mesh& mesh = m_pWorld->Meshes[model.MeshIndex];
							meshes.push_back(&mesh);
							skinData.SkinMatrixOffset		= (uint32)skinningTransforms.size();
							skinData.SkinnedPositionsOffset	= mesh.SkinnedPositionStreamLocation.OffsetFromStart;
							skinData.SkinnedNormalsOffset	= mesh.SkinnedNormalStreamLocation.OffsetFromStart;
							skinData.PositionsOffset		= mesh.PositionStreamLocation.OffsetFromStart;
							skinData.NormalsOffset			= mesh.NormalStreamLocation.OffsetFromStart;
							skinData.JointsOffset			= mesh.JointsStreamLocation.OffsetFromStart;
							skinData.WeightsOffset			= mesh.WeightsStreamLocation.OffsetFromStart;
							skinData.NumVertices			= mesh.PositionStreamLocation.Elements;

							const Animation& anim = m_pWorld->Animations[model.AnimationIndex];
							const Skeleton& skeleton = m_pWorld->Skeletons[model.SkeletonIndex];

							float t = fmod(Time::TotalTime(), anim.TimeEnd - anim.TimeStart);
							float time = t + anim.TimeStart;

							Array<JointTransform> jointTransforms(skeleton.NumJoints());
							for (const AnimationChannel& channel : anim.Channels)
							{
								JointTransform& jointTransform = jointTransforms[skeleton.GetJoint(channel.Target)];
								if (channel.Path == AnimationChannel::PathType::Translation)
									jointTransform.Translation = Vector3(channel.Evaluate(time));
								else if (channel.Path == AnimationChannel::PathType::Rotation)
									jointTransform.Rotation = channel.Evaluate(time);
								else if (channel.Path == AnimationChannel::PathType::Scale)
									jointTransform.Scale = Vector3(channel.Evaluate(time));
							}

							for (int i = 0; i < (int)skeleton.NumJoints(); ++i)
							{
								// Update joints in an order so that parent joints are always computed before any children
								Skeleton::JointIndex jointIndex = skeleton.JointUpdateOrder[i];
								Skeleton::JointIndex parentJointIndex = skeleton.ParentIndices[jointIndex];

								if (parentJointIndex != Skeleton::InvalidJoint)
								{
									JointTransform& transform = jointTransforms[jointIndex];
									const JointTransform& parentTransform = jointTransforms[parentJointIndex];

									JointTransform newTransform;
									newTransform.Translation	= parentTransform.Translation + Vector3::Transform(parentTransform.Scale * transform.Translation, parentTransform.Rotation);
									newTransform.Rotation		= transform.Rotation * parentTransform.Rotation;
									newTransform.Scale			= transform.Scale * parentTransform.Scale;

									transform = newTransform;
								}
							}

							// Compute final skin transforms
							skinningTransforms.resize(skinningTransforms.size() + skeleton.NumJoints());
							Matrix* pSkinMatrices = &skinningTransforms[skinData.SkinMatrixOffset];
							for (int i = 0; i < (int)skeleton.NumJoints(); ++i)
							{
								const JointTransform& transform = jointTransforms[i];
								Matrix jointMatrix = Matrix::CreateScale(transform.Scale) *
														Matrix::CreateFromQuaternion(transform.Rotation) *
														Matrix::CreateTranslation(transform.Translation);
								pSkinMatrices[i] = skeleton.InverseBindMatrices[i] * jointMatrix;
							}
						}
					});

				if (!skinningTransforms.empty())
				{
					RGBuffer* pSkinningMatrices = graph.Create("Skinning Matrices", BufferDesc::CreateStructured((uint32)skinningTransforms.size(), sizeof(Matrix)));
					RGUtils::DoUpload(graph, pSkinningMatrices, skinningTransforms.data(), (uint32)skinningTransforms.size() * sizeof(Matrix));

					graph.AddPass("GPU Skinning", RGPassFlag::Compute | RGPassFlag::NeverCull)
						.Read(pSkinningMatrices)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pSkinPSO);

								context.BindResources(BindingSlot::SRV, resources.GetSRV(pSkinningMatrices));

								for (int i = 0; i < (int)skinDatas.size(); ++i)
								{
									context.InsertResourceBarrier(meshes[i]->pBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

									context.BindRootCBV(BindingSlot::PerInstance, skinDatas[i]);
									context.BindResources(BindingSlot::UAV, { meshes[i]->pBuffer->GetUAV() });
									context.Dispatch(ComputeUtils::GetNumThreadGroups(meshes[i]->PositionStreamLocation.Elements, 64));

									context.InsertResourceBarrier(meshes[i]->pBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
								}
							});
				}
			}

			const Vector2u viewDimensions = pView->GetDimensions();

			SceneTextures sceneTextures;
			sceneTextures.pDepth = graph.Create("Depth Stencil", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::DepthStencilFormat, 1, TextureFlag::None, ClearBinding(0.0f, 0)));
			sceneTextures.pColorTarget = graph.Create("Color Target", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::GBufferFormat[0]));
			sceneTextures.pNormals = graph.Create("Normals", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::GBufferFormat[1]));
			sceneTextures.pRoughness = graph.Create("Roughness", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::GBufferFormat[2]));
			sceneTextures.pVelocity = graph.Create("Velocity", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RG16_FLOAT));
			sceneTextures.pPreviousColor = graph.TryImport(m_pColorHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));

			sceneTextures.pGBuffer0 = graph.Create("GBuffer 0", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::DeferredGBufferFormat[0]));
			sceneTextures.pGBuffer1 = graph.Create("GBuffer 1", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::DeferredGBufferFormat[1]));
			sceneTextures.pGBuffer2 = graph.Create("GBuffer 2", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, GraphicsCommon::DeferredGBufferFormat[2]));

			LightCull2DData lightCull2DData;
			LightCull3DData lightCull3DData;

			RGTexture* pSky = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::BlackCube));
			if (Tweakables::gSky)
			{
				pSky = graph.Create("Sky", TextureDesc::CreateCube(64, 64, ResourceFormat::RGBA16_FLOAT));
				graph.AddPass("Compute Sky", RGPassFlag::Compute)
					.Write(pSky)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							Texture* pSkyTexture = resources.Get(pSky);
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pRenderSkyPSO);

							struct
							{
								Vector2 DimensionsInv;
							} params;
							params.DimensionsInv = Vector2(1.0f / pSkyTexture->GetWidth(), 1.0f / pSkyTexture->GetHeight());

							Renderer::BindViewUniforms(context, *pView);
							context.BindRootCBV(BindingSlot::PerInstance, params);
							context.BindResources(BindingSlot::UAV, pSkyTexture->GetUAV());

							context.Dispatch(ComputeUtils::GetNumThreadGroups(pSkyTexture->GetWidth(), 16, pSkyTexture->GetHeight(), 16, 6));
						});

				graph.AddPass("Transition Sky", RGPassFlag::Raster | RGPassFlag::NeverCull)
					.Read(pSky);
			}

			// Export makes sure the target texture is filled in during pass execution.
			graph.Export(pSky, &m_pSky, TextureFlag::ShaderResource);

			RasterResult rasterResult;
			if (m_RenderPath != RenderPath::PathTracing)
			{
				{
					RG_GRAPH_SCOPE("Shadow Depths", graph);
					for (uint32 i = 0; i < (uint32)m_ShadowViews.size(); ++i)
					{
						const ShadowView& shadowView = m_ShadowViews[i];
						RG_GRAPH_SCOPE(Sprintf("View %d (%s - Cascade %d)", i, gLightTypeStr[(int)shadowView.pLight->Type], shadowView.ViewIndex).c_str(), graph);

						RGTexture* pShadowmap = graph.Import(m_ShadowViews[i].pDepthTexture);
						if (Tweakables::gShadowsGPUCull)
						{
							RasterContext context(graph, pShadowmap, RasterMode::Shadows, &m_ShadowHZBs[i]);
							context.EnableOcclusionCulling = Tweakables::gShadowsOcclusionCulling;
							RasterResult result;
							m_pMeshletRasterizer->Render(graph, &shadowView, context, result);
							if (Tweakables::gCullShadowsDebugStats == (int)i)
								m_pMeshletRasterizer->PrintStats(graph, Vector2(400, 20), pView, context);
						}
						else
						{
							graph.AddPass("Raster", RGPassFlag::Raster)
								.DepthStencil(pShadowmap, RenderPassDepthFlags::Clear)
								.Bind([=](CommandContext& context, const RGResources& resources)
									{
										context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
										context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

										const ShadowView& view = m_ShadowViews[i];
										Renderer::BindViewUniforms(context, view);

										{
											PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque");
											context.SetPipelineState(m_pShadowsOpaquePSO);
											DrawScene(context, m_Batches, view.VisibilityMask, Batch::Blending::Opaque);
										}
										{
											PROFILE_GPU_SCOPE(context.GetCommandList(), "Masked");
											context.SetPipelineState(m_pShadowsAlphaMaskPSO);
											DrawScene(context, m_Batches, view.VisibilityMask, Batch::Blending::AlphaMask | Batch::Blending::AlphaBlend);
										}
									});
						}
					}
				}

				const bool doPrepass = true;
				const bool needVisibilityBuffer = m_RenderPath == RenderPath::Visibility || m_RenderPath == RenderPath::VisibilityDeferred;

				if (doPrepass)
				{
					if (needVisibilityBuffer)
					{
						RasterContext rasterContext(graph, sceneTextures.pDepth, RasterMode::VisibilityBuffer, &m_pHZB);
						rasterContext.EnableDebug = Tweakables::gVisibilityDebugMode > 0;
						rasterContext.EnableOcclusionCulling = Tweakables::gOcclusionCulling;
						rasterContext.WorkGraph = Tweakables::gWorkGraph;
						m_pMeshletRasterizer->Render(graph, pView, rasterContext, rasterResult);
						if (Tweakables::gCullDebugStats)
							m_pMeshletRasterizer->PrintStats(graph, Vector2(20, 20), pView, rasterContext);
					}
					else
					{
						graph.AddPass("Depth Prepass", RGPassFlag::Raster)
							.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::Clear)
							.Bind([=](CommandContext& context, const RGResources& resources)
								{
									context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
									context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);

									Renderer::BindViewUniforms(context, *pView);
									{
										PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque");
										context.SetPipelineState(m_pDepthPrepassOpaquePSO);
										DrawScene(context, *pView, Batch::Blending::Opaque);
									}
									{
										PROFILE_GPU_SCOPE(context.GetCommandList(), "Masked");
										context.SetPipelineState(m_pDepthPrepassAlphaMaskPSO);
										DrawScene(context, *pView, Batch::Blending::AlphaMask);
									}
								});
					}

					if (Tweakables::gRenderTerrain.GetBool())
						m_pCBTTessellation->RasterMain(graph, pView, sceneTextures);
				}

				if (Tweakables::gSDSM)
				{
					RG_GRAPH_SCOPE("Depth Reduce", graph);

					Vector2u depthTarget = sceneTextures.pDepth->GetDesc().Size2D();
					depthTarget.x = Math::Max(depthTarget.x / 16u, 1u);
					depthTarget.y = Math::Max(depthTarget.y / 16u, 1u);
					RGTexture* pReductionTarget = graph.Create("Depth Reduction Target", TextureDesc::Create2D(depthTarget.x, depthTarget.y, ResourceFormat::RG32_FLOAT));

					graph.AddPass("Depth Reduce - Setup", RGPassFlag::Compute)
						.Read(sceneTextures.pDepth)
						.Write(pReductionTarget)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								Texture* pSource = resources.Get(sceneTextures.pDepth);
								Texture* pTarget = resources.Get(pReductionTarget);

								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pPrepareReduceDepthPSO);

								Renderer::BindViewUniforms(context, *pView);
								context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
								context.BindResources(BindingSlot::SRV, pSource->GetSRV());

								context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());
							});

					for (;;)
					{
						RGTexture* pReductionSource = pReductionTarget;
						pReductionTarget = graph.Create("Depth Reduction Target", TextureDesc::Create2D(depthTarget.x, depthTarget.y, ResourceFormat::RG32_FLOAT));

						graph.AddPass("Depth Reduce - Subpass", RGPassFlag::Compute)
							.Read(pReductionSource)
							.Write(pReductionTarget)
							.Bind([=](CommandContext& context, const RGResources& resources)
								{
									Texture* pTarget = resources.Get(pReductionTarget);
									context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
									context.SetPipelineState(m_pReduceDepthPSO);
									context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
									context.BindResources(BindingSlot::SRV, resources.GetSRV(pReductionSource));
									context.Dispatch(pTarget->GetWidth(), pTarget->GetHeight());
								});

						if (depthTarget.x == 1 && depthTarget.y == 1)
							break;

						depthTarget.x = Math::Max(1u, depthTarget.x / 16);
						depthTarget.y = Math::Max(1u, depthTarget.y / 16);
					}

					RGBuffer* pReadbackTarget = RGUtils::CreatePersistent(graph, "SDSM Readback", BufferDesc::CreateTyped(2, ResourceFormat::RG32_FLOAT, BufferFlag::Readback), &m_ReductionReadbackTargets[m_Frame % GraphicsDevice::NUM_BUFFERS], true);
					graph.AddPass("Readback Copy", RGPassFlag::Copy)
						.Read(pReductionTarget)
						.Write(pReadbackTarget)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.CopyTexture(resources.Get(pReductionTarget), resources.Get(pReadbackTarget), CD3DX12_BOX(0, 1));
							});
				}

				m_pParticles->Simulate(graph, pView, sceneTextures.pDepth);

				if (Tweakables::gEnableDDGI)
				{
					m_pDDGI->Execute(graph, pView);
				}

				graph.AddPass("Camera Motion", RGPassFlag::Compute)
					.Read(sceneTextures.pDepth)
					.Write(sceneTextures.pVelocity)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							Texture* pVelocity = resources.Get(sceneTextures.pVelocity);

							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pCameraMotionPSO);

							Renderer::BindViewUniforms(context, *pView);
							context.BindResources(BindingSlot::UAV, pVelocity->GetUAV());
							context.BindResources(BindingSlot::SRV, resources.GetSRV(sceneTextures.pDepth));

							context.Dispatch(ComputeUtils::GetNumThreadGroups(pVelocity->GetWidth(), 8, pVelocity->GetHeight(), 8));
						});

				RGTexture* pAO = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::White2D));
				if (Tweakables::gRaytracedAO)
					pAO = m_pRTAO->Execute(graph, pView, sceneTextures.pDepth, sceneTextures.pVelocity);
				else
					pAO = m_pSSAO->Execute(graph, pView, sceneTextures.pDepth);

				m_pLightCulling->ComputeTiledLightCulling(graph, pView, sceneTextures, lightCull2DData);
				m_pLightCulling->ComputeClusteredLightCulling(graph, pView, lightCull3DData);

				RGTexture* pFog = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D));
				if (Tweakables::gVolumetricFog)
					pFog = m_pVolumetricFog->RenderFog(graph, pView, lightCull3DData, m_FogData);

				if (m_RenderPath == RenderPath::Tiled)
				{
					m_pForwardRenderer->RenderForwardTiled(graph, pView, sceneTextures, lightCull2DData, pFog, pAO);
				}
				else if (m_RenderPath == RenderPath::Clustered)
				{
					m_pForwardRenderer->RenderForwardClustered(graph, pView, sceneTextures, lightCull3DData, pFog, pAO);
				}
				else if (m_RenderPath == RenderPath::Visibility)
				{
					graph.AddPass("Visibility Shading", RGPassFlag::Raster)
						.Read({ pFog, rasterResult.pVisibleMeshlets })
						.Read({ rasterResult.pVisibilityBuffer, sceneTextures.pDepth, pAO, sceneTextures.pPreviousColor })
						.Read({ lightCull2DData.pLightListOpaque })
						.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::ReadOnly)
						.RenderTarget(sceneTextures.pColorTarget)
						.RenderTarget(sceneTextures.pNormals)
						.RenderTarget(sceneTextures.pRoughness)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pVisibilityShadingGraphicsPSO);
								context.SetStencilRef((uint8)StencilBit::VisibilityBuffer);
								context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

								Renderer::BindViewUniforms(context, *pView);
								context.BindResources(BindingSlot::SRV, {
									resources.GetSRV(rasterResult.pVisibilityBuffer),
									resources.GetSRV(pAO),
									resources.GetSRV(sceneTextures.pDepth),
									resources.GetSRV(sceneTextures.pPreviousColor),
									resources.GetSRV(pFog),
									resources.GetSRV(rasterResult.pVisibleMeshlets),
									resources.GetSRV(lightCull2DData.pLightListOpaque),
									});
								context.Draw(0, 3);
							});
					m_pForwardRenderer->RenderForwardClustered(graph, pView, sceneTextures, lightCull3DData, pFog, pAO, true);
				}
				else if (m_RenderPath == RenderPath::VisibilityDeferred)
				{
					graph.AddPass("Build GBuffer", RGPassFlag::Raster)
						.Read({ rasterResult.pVisibilityBuffer, rasterResult.pVisibleMeshlets, })
						.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::ReadOnly)
						.RenderTarget(sceneTextures.pGBuffer0)
						.RenderTarget(sceneTextures.pGBuffer1)
						.RenderTarget(sceneTextures.pGBuffer2)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pVisibilityGBufferPSO);
								context.SetStencilRef((uint8)StencilBit::VisibilityBuffer);
								context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

								Renderer::BindViewUniforms(context, *pView);
								context.BindResources(BindingSlot::SRV, {
									resources.GetSRV(rasterResult.pVisibilityBuffer),
									resources.GetSRV(rasterResult.pVisibleMeshlets),
									});
								context.Draw(0, 3);
							});

					graph.AddPass("Deferred Shading", RGPassFlag::Compute)
						.Read({ pFog })
						.Read({ sceneTextures.pDepth, pAO, sceneTextures.pPreviousColor })
						.Read({ lightCull2DData.pLightListOpaque })
						.Read({ sceneTextures.pGBuffer0, sceneTextures.pGBuffer1, sceneTextures.pGBuffer2 })
						.Write(sceneTextures.pColorTarget)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								Texture* pTarget = resources.Get(sceneTextures.pColorTarget);

								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pDeferredShadePSO);

								Renderer::BindViewUniforms(context, *pView);
								context.BindResources(BindingSlot::UAV, resources.GetUAV(sceneTextures.pColorTarget));
								context.BindResources(BindingSlot::SRV, {
									resources.GetSRV(sceneTextures.pGBuffer0),
									resources.GetSRV(sceneTextures.pGBuffer1),
									resources.GetSRV(sceneTextures.pGBuffer2),
									resources.GetSRV(sceneTextures.pDepth),
									resources.GetSRV(sceneTextures.pPreviousColor),
									resources.GetSRV(pFog),
									resources.GetSRV(lightCull2DData.pLightListOpaque),
									resources.GetSRV(pAO),
									});

								context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
							});

					m_pForwardRenderer->RenderForwardClustered(graph, pView, sceneTextures, lightCull3DData, pFog, pAO, true);
				}

				if (Tweakables::gRenderTerrain.GetBool())
					m_pCBTTessellation->Shade(graph, pView, sceneTextures, pFog);

				m_pParticles->Render(graph, pView, sceneTextures);

				graph.AddPass("Render Sky", RGPassFlag::Raster)
					.Read(pSky)
					.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::ReadOnly)
					.RenderTarget(sceneTextures.pColorTarget)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
							context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pSkyboxPSO);

							Renderer::BindViewUniforms(context, *pView);
							context.Draw(0, 36);
						});

				if (Tweakables::gClouds)
				{
					sceneTextures.pColorTarget = m_pClouds->Render(graph, pView, sceneTextures.pColorTarget, sceneTextures.pDepth);
				}

				if (Tweakables::gRaytracedReflections)
				{
					m_pRTReflections->Execute(graph, pView, sceneTextures);
				}

				if (Tweakables::gTAA.Get())
				{
					RGTexture* pTaaTarget = graph.Create("TAA Target", sceneTextures.pColorTarget->GetDesc());

					graph.AddPass("Temporal Resolve", RGPassFlag::Compute)
						.Read({ sceneTextures.pVelocity, sceneTextures.pDepth, sceneTextures.pColorTarget, sceneTextures.pPreviousColor })
						.Write(pTaaTarget)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								Texture* pTarget = resources.Get(pTaaTarget);
								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pTemporalResolvePSO);

								struct
								{
									float MinBlendFactor;
								} params;
								params.MinBlendFactor = pView->CameraCut ? 1.0f : 0.0f;

								Renderer::BindViewUniforms(context, *pView);
								context.BindRootCBV(BindingSlot::PerInstance, params);
								context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
								context.BindResources(BindingSlot::SRV,
									{
										resources.GetSRV(sceneTextures.pVelocity),
										resources.GetSRV(sceneTextures.pPreviousColor),
										resources.GetSRV(sceneTextures.pColorTarget),
										resources.GetSRV(sceneTextures.pDepth),
									});

								context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
							});

					sceneTextures.pColorTarget = pTaaTarget;
				}
				graph.Export(sceneTextures.pColorTarget, &m_pColorHistory, TextureFlag::ShaderResource);

				// Probes contain irradiance data, and need to go through tonemapper.
				if (Tweakables::gVisualizeDDGI)
				{
					m_pDDGI->RenderVisualization(graph, pView, sceneTextures.pColorTarget, sceneTextures.pDepth);
				}
			}
			else
			{
				m_pPathTracing->Render(graph, pView, sceneTextures.pColorTarget);
			}

			/*
				Post Processing
			*/

			RGBuffer* pAverageLuminance = RGUtils::CreatePersistent(graph, "Average Luminance", BufferDesc::CreateStructured(3, sizeof(float)), &m_pAverageLuminance, true);
			{
				RG_GRAPH_SCOPE("Auto Exposure", graph);

				RGTexture* pColor = sceneTextures.pColorTarget;
				TextureDesc sourceDesc = pColor->GetDesc();
				sourceDesc.Width = Math::DivideAndRoundUp(sourceDesc.Width, 4);
				sourceDesc.Height = Math::DivideAndRoundUp(sourceDesc.Height, 4);
				RGTexture* pDownscaleTarget = graph.Create("Downscaled HDR Target", sourceDesc);

				graph.AddPass("Downsample Color", RGPassFlag::Compute)
					.Read(pColor)
					.Write(pDownscaleTarget)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							Texture* pTarget = resources.Get(pDownscaleTarget);

							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pDownsampleColorPSO);

							struct
							{
								Vector2i TargetDimensions;
								Vector2 TargetDimensionsInv;
							} parameters;
							parameters.TargetDimensions.x = pTarget->GetWidth();
							parameters.TargetDimensions.y = pTarget->GetHeight();
							parameters.TargetDimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
							context.BindResources(BindingSlot::SRV, resources.GetSRV(pColor));

							context.Dispatch(ComputeUtils::GetNumThreadGroups(parameters.TargetDimensions.x, 8, parameters.TargetDimensions.y, 8));
						});

				RGBuffer* pLuminanceHistogram = graph.Create("Luminance Histogram", BufferDesc::CreateByteAddress(sizeof(uint32) * 256));
				graph.AddPass("Luminance Histogram", RGPassFlag::Compute)
					.Read(pDownscaleTarget)
					.Write(pLuminanceHistogram)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							Texture* pColorSource = resources.Get(pDownscaleTarget);
							Buffer* pHistogram = resources.Get(pLuminanceHistogram);

							context.ClearUAVu(pHistogram->GetUAV());

							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pLuminanceHistogramPSO);

							struct
							{
								uint32 Width;
								uint32 Height;
								float MinLogLuminance;
								float OneOverLogLuminanceRange;
							} parameters;
							parameters.Width = pColorSource->GetWidth();
							parameters.Height = pColorSource->GetHeight();
							parameters.MinLogLuminance = Tweakables::gMinLogLuminance.Get();
							parameters.OneOverLogLuminanceRange = 1.0f / (Tweakables::gMaxLogLuminance.Get() - Tweakables::gMinLogLuminance.Get());

							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, pHistogram->GetUAV());
							context.BindResources(BindingSlot::SRV, pColorSource->GetSRV());

							context.Dispatch(ComputeUtils::GetNumThreadGroups(pColorSource->GetWidth(), 16, pColorSource->GetHeight(), 16));
						});

				uint32 numPixels = sourceDesc.Width * sourceDesc.Height;

				graph.AddPass("Average Luminance", RGPassFlag::Compute)
					.Read(pLuminanceHistogram)
					.Write(pAverageLuminance)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pAverageLuminancePSO);

							struct
							{
								int32 PixelCount;
								float MinLogLuminance;
								float LogLuminanceRange;
								float TimeDelta;
								float Tau;
							} parameters;

							parameters.PixelCount = numPixels;
							parameters.MinLogLuminance = Tweakables::gMinLogLuminance;
							parameters.LogLuminanceRange = Tweakables::gMaxLogLuminance - Tweakables::gMinLogLuminance;
							parameters.TimeDelta = Time::DeltaTime();
							parameters.Tau = Tweakables::gTau.Get();

							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, resources.GetUAV(pAverageLuminance));
							context.BindResources(BindingSlot::SRV, resources.GetSRV(pLuminanceHistogram));

							context.Dispatch(1);
						});

				if (Tweakables::gDrawHistogram.Get())
				{
					RGTexture* pHistogramDebugTexture = RGUtils::CreatePersistent(graph, "Debug Histogram", TextureDesc::Create2D(256 * 4, 256, ResourceFormat::RGBA8_UNORM, 1, TextureFlag::ShaderResource), &m_pDebugHistogramTexture, true);
					graph.AddPass("Draw Histogram", RGPassFlag::Compute)
						.Read({ pLuminanceHistogram, pAverageLuminance })
						.Write(pHistogramDebugTexture)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.ClearUAVf(resources.GetUAV(pHistogramDebugTexture));

								context.SetPipelineState(m_pDrawHistogramPSO);
								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

								struct
								{
									float MinLogLuminance;
									float InverseLogLuminanceRange;
									Vector2 InvTextureDimensions;
								} parameters;

								parameters.MinLogLuminance = Tweakables::gMinLogLuminance;
								parameters.InverseLogLuminanceRange = 1.0f / (Tweakables::gMaxLogLuminance - Tweakables::gMinLogLuminance);
								parameters.InvTextureDimensions.x = 1.0f / pHistogramDebugTexture->GetDesc().Width;
								parameters.InvTextureDimensions.y = 1.0f / pHistogramDebugTexture->GetDesc().Height;

								context.BindRootCBV(BindingSlot::PerInstance, parameters);
								context.BindResources(BindingSlot::UAV, resources.GetUAV(pHistogramDebugTexture));
								context.BindResources(BindingSlot::SRV, {
									resources.GetSRV(pLuminanceHistogram),
									resources.GetSRV(pAverageLuminance),
									});

								context.Dispatch(1, resources.Get(pLuminanceHistogram)->GetNumElements());
							});
				}
			}

			RGTexture* pBloomTexture = graph.Import(GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));
			if (Tweakables::gBloom.Get())
			{
				RG_GRAPH_SCOPE("Bloom", graph);

				RGTexture* pColor = sceneTextures.pColorTarget;
				auto ComputeNumMips = [](uint32 width, uint32 height) -> uint32
					{
						return (uint32)Math::Floor(log2f((float)Math::Max(width, height))) + 1u;
					};

				Vector2u bloomDimensions = Vector2u(pColor->GetDesc().Width >> 1, pColor->GetDesc().Height >> 1);
				const uint32 mipBias = 3;
				uint32 numMips = ComputeNumMips(bloomDimensions.x, bloomDimensions.y) - mipBias;
				RGTexture* pDownscaleTarget = graph.Create("Downscale Target", TextureDesc::Create2D(bloomDimensions.x, bloomDimensions.y, ResourceFormat::RGBA16_FLOAT, numMips));

				RGTexture* pSourceTexture = pColor;
				for (uint32 i = 0; i < numMips; ++i)
				{
					Vector2u targetDimensions(Math::Max(1u, bloomDimensions.x >> i), Math::Max(1u, bloomDimensions.y >> i));
					graph.AddPass(Sprintf("Downsample %d [%dx%d > %dx%d]", i, targetDimensions.x << 1, targetDimensions.y << 1, targetDimensions.x, targetDimensions.y).c_str(), RGPassFlag::Compute)
						.Read(i == 0 ? pSourceTexture : nullptr)
						.Write(pDownscaleTarget)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(i == 0 ? m_pBloomDownsampleKarisAveragePSO : m_pBloomDownsamplePSO);
								struct
								{
									Vector2 TargetDimensionsInv;
									uint32 SourceMip;
								} parameters;
								parameters.TargetDimensionsInv = Vector2(1.0f / targetDimensions.x, 1.0f / targetDimensions.y);
								parameters.SourceMip = i == 0 ? 0 : i - 1;

								context.BindRootCBV(BindingSlot::PerInstance, parameters);
								context.BindResources(BindingSlot::UAV, resources.GetUAV(pDownscaleTarget, i));
								context.BindResources(BindingSlot::SRV, static_cast<Texture*>(resources.GetResourceUnsafe(pSourceTexture))->GetSRV());
								context.Dispatch(ComputeUtils::GetNumThreadGroups(targetDimensions.x, 8, targetDimensions.y, 8));
								context.InsertUAVBarrier();
							});

					pSourceTexture = pDownscaleTarget;
				}

				numMips = Math::Max(2u, numMips);
				RGTexture* pUpscaleTarget = graph.Create("Upscale Target", TextureDesc::Create2D(bloomDimensions.x, bloomDimensions.y, ResourceFormat::RGBA16_FLOAT, numMips - 1));
				RGTexture* pPreviousSource = pDownscaleTarget;

				for (int32 i = numMips - 2; i >= 0; --i)
				{
					Vector2u targetDimensions(Math::Max(1u, bloomDimensions.x >> i), Math::Max(1u, bloomDimensions.y >> i));
					graph.AddPass(Sprintf("UpsampleCombine %d [%dx%d > %dx%d]", numMips - 2 - i, Math::Max(1u, targetDimensions.x >> 1), Math::Max(1u, targetDimensions.y >> 1), targetDimensions.x, targetDimensions.y).c_str(), RGPassFlag::Compute)
						.Read(pDownscaleTarget)
						.Write(pUpscaleTarget)
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pBloomUpsamplePSO);
								struct
								{
									Vector2 TargetDimensionsInv;
									uint32 SourceCurrentMip;
									uint32 SourcePreviousMip;
									float Radius;
								} parameters;
								parameters.TargetDimensionsInv = Vector2(1.0f / targetDimensions.x, 1.0f / targetDimensions.y);
								parameters.SourceCurrentMip = i;
								parameters.SourcePreviousMip = i + 1;
								parameters.Radius = Tweakables::gBloomInteralBlendFactor;

								context.BindRootCBV(BindingSlot::PerInstance, parameters);
								context.BindResources(BindingSlot::UAV, resources.Get(pUpscaleTarget)->GetUAV(i));
								context.BindResources(BindingSlot::SRV, {
									resources.GetSRV(pDownscaleTarget),
									resources.Get(pPreviousSource)->GetSRV(),
									});
								context.Dispatch(ComputeUtils::GetNumThreadGroups(targetDimensions.x, 8, targetDimensions.y, 8));
								context.InsertUAVBarrier();
							});

					pPreviousSource = pUpscaleTarget;
				}

				pBloomTexture = pUpscaleTarget;
			}

			RGTexture* pTonemapTarget = graph.Create("Tonemap Target", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RGBA8_UNORM));

			graph.AddPass("Tonemap", RGPassFlag::Compute)
				.Read({ sceneTextures.pColorTarget, pAverageLuminance, pBloomTexture })
				.Write(pTonemapTarget)
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						Texture* pTarget = resources.Get(pTonemapTarget);

						struct
						{
							float WhitePoint;
							uint32 Tonemapper;
							float BloomIntensity;
							float BloomBlendFactor;
							Vector3 LensDirtTint;
						} parameters;
						parameters.WhitePoint = Tweakables::gWhitePoint.Get();
						parameters.Tonemapper = Tweakables::gToneMapper.Get();
						parameters.BloomIntensity = Tweakables::gBloomIntensity.Get();
						parameters.BloomBlendFactor = Tweakables::gBloomBlendFactor.Get();
						parameters.LensDirtTint = m_LensDirtTint;

						context.SetPipelineState(m_pToneMapPSO);
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

						Renderer::BindViewUniforms(context, *pView);
						context.BindRootCBV(BindingSlot::PerInstance, parameters);
						context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
						context.BindResources(BindingSlot::SRV, {
							resources.GetSRV(sceneTextures.pColorTarget),
							resources.GetSRV(pAverageLuminance),
							resources.GetSRV(pBloomTexture),
							m_pLensDirtTexture->GetSRV(),
							});
						context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
					});

			sceneTextures.pColorTarget = pTonemapTarget;

			/*
				Debug Views
			*/

			if (m_RenderPath != RenderPath::PathTracing)
			{
				if (Tweakables::gVisualizeLightDensity)
				{
					if (m_RenderPath == RenderPath::Clustered)
						sceneTextures.pColorTarget = m_pLightCulling->VisualizeLightDensity(graph, pView, sceneTextures.pDepth, lightCull3DData);
					else if (m_RenderPath == RenderPath::Tiled || m_RenderPath == RenderPath::Visibility || m_RenderPath == RenderPath::VisibilityDeferred)
						sceneTextures.pColorTarget = m_pLightCulling->VisualizeLightDensity(graph, pView, sceneTextures.pDepth, lightCull2DData);
				}

				if ((m_RenderPath == RenderPath::Visibility || m_RenderPath == RenderPath::VisibilityDeferred) && Tweakables::gVisibilityDebugMode > 0)
				{
					graph.AddPass("Visibility Debug Render", RGPassFlag::Compute)
						.Read({ rasterResult.pVisibilityBuffer, rasterResult.pVisibleMeshlets, rasterResult.pDebugData })
						.Write({ sceneTextures.pColorTarget })
						.Bind([=](CommandContext& context, const RGResources& resources)
							{
								Texture* pColorTarget = resources.Get(sceneTextures.pColorTarget);

								context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
								context.SetPipelineState(m_pVisibilityDebugRenderPSO);

								Renderer::BindViewUniforms(context, *pView);

								uint32 mode = Tweakables::gVisibilityDebugMode;
								context.BindRootCBV(BindingSlot::PerInstance, mode);
								context.BindResources(BindingSlot::UAV, pColorTarget->GetUAV());
								context.BindResources(BindingSlot::SRV, {
									resources.GetSRV(rasterResult.pVisibilityBuffer),
									resources.GetSRV(rasterResult.pVisibleMeshlets),
									resources.GetSRV(rasterResult.pDebugData),
									});
								context.Dispatch(ComputeUtils::GetNumThreadGroups(pColorTarget->GetWidth(), 8, pColorTarget->GetHeight(), 8));
							});
				}
			}

			DebugRenderer::Get()->Render(graph, pView, sceneTextures.pColorTarget, sceneTextures.pDepth);

			m_pShaderDebugRenderer->Render(graph, pView, sceneTextures.pColorTarget, sceneTextures.pDepth);

			if (!Tweakables::VisualizeTextureName.empty())
			{
				RGTexture* pVisualizeTexture = graph.FindTexture(Tweakables::VisualizeTextureName.c_str());
				m_pCaptureTextureSystem->Capture(graph, m_CaptureTextureContext, pVisualizeTexture);
			}

			RGTexture* pOutput = graph.Import(pTarget);
			RGUtils::AddCopyPass(graph, sceneTextures.pColorTarget, pOutput);
		}

		RGGraphOptions graphOptions;
		graphOptions.Jobify = Tweakables::gRenderGraphJobify;
		graphOptions.PassCulling = Tweakables::gRenderGraphPassCulling;
		graphOptions.ResourceAliasing = Tweakables::gRenderGraphResourceAliasing;
		graphOptions.StateTracking = Tweakables::gRenderGraphStateTracking;
		graphOptions.CommandlistGroupSize = Tweakables::gRenderGraphPassGroupSize;

		// Compile graph
		graph.Compile(*m_RenderGraphPool, graphOptions);

		// Debug options
		graph.DrawResourceTracker(Tweakables::gRenderGraphResourceTracker.Get());
		graph.DrawPassView(Tweakables::gRenderGraphPassView.Get());

		if (Tweakables::gDumpRenderGraphNextFrame)
		{
			graph.DumpDebugGraph(Sprintf("%sRenderGraph_%s", Paths::SavedDir(), Utils::GetTimeString()).c_str());
			Tweakables::gDumpRenderGraphNextFrame = false;
		}

		// Execute
		graph.Execute(m_pDevice);

	}

	{
		++m_Frame;
		m_MainView.CameraCut = false;
	}
}

void Renderer::InitializePipelines()
{
	// Depth-only raster PSOs

	{
		ShaderDefineHelper defines;
		defines.Set("DEPTH_ONLY", true);

		{
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
			psoDesc.SetAmplificationShader("ForwardShading.hlsl", "ASMain", *defines);
			psoDesc.SetMeshShader("ForwardShading.hlsl", "MSMain", *defines);
			psoDesc.SetDepthOnlyTarget(GraphicsCommon::DepthStencilFormat, 1);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, 0x0, (uint8)StencilBit::SurfaceTypeMask);
			psoDesc.SetName("Depth Prepass Opaque");
			m_pDepthPrepassOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

			psoDesc.SetPixelShader("ForwardShading.hlsl", "DepthOnlyPS", *defines);
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			psoDesc.SetName("Depth Prepass Alpha Mask");
			m_pDepthPrepassAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
		}

		{
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
			psoDesc.SetAmplificationShader("ForwardShading.hlsl", "ASMain", *defines);
			psoDesc.SetMeshShader("ForwardShading.hlsl", "MSMain", *defines);
			psoDesc.SetDepthOnlyTarget(GraphicsCommon::ShadowFormat, 1);
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			psoDesc.SetDepthBias(-10, 0, -4.0f);
			psoDesc.SetName("Shadow Mapping Opaque");
			m_pShadowsOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

			psoDesc.SetPixelShader("ForwardShading.hlsl", "DepthOnlyPS", *defines);
			psoDesc.SetName("Shadow Mapping Alpha Mask");
			m_pShadowsAlphaMaskPSO = m_pDevice->CreatePipeline(psoDesc);
		}

	}

	ShaderDefineHelper tonemapperDefines;
	tonemapperDefines.Set("NUM_HISTOGRAM_BINS", 256);
	m_pLuminanceHistogramPSO	= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "LuminanceHistogram.hlsl", "CSMain", *tonemapperDefines);
	m_pDrawHistogramPSO			= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "DrawLuminanceHistogram.hlsl", "DrawLuminanceHistogram", *tonemapperDefines);
	m_pAverageLuminancePSO		= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "AverageLuminance.hlsl", "CSMain", *tonemapperDefines);
	m_pToneMapPSO				= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/Tonemapping.hlsl", "CSMain", *tonemapperDefines);
	m_pDownsampleColorPSO		= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/DownsampleColor.hlsl", "CSMain");

	m_pPrepareReduceDepthPSO	= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "ReduceDepth.hlsl", "PrepareReduceDepth");
	m_pReduceDepthPSO			= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "ReduceDepth.hlsl", "ReduceDepth");

	m_pCameraMotionPSO			= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "CameraMotionVectors.hlsl", "CSMain");
	m_pTemporalResolvePSO		= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/TemporalResolve.hlsl", "CSMain");


	//Sky
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetVertexShader("ProceduralSky.hlsl", "VSMain");
		psoDesc.SetPixelShader("ProceduralSky.hlsl", "PSMain");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA16_FLOAT, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Skybox");
		m_pSkyboxPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pRenderSkyPSO = m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "ProceduralSky.hlsl", "ComputeSkyCS");
	}

	//Bloom
	m_pBloomDownsamplePSO				= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/Bloom.hlsl", "DownsampleCS");
	m_pBloomDownsampleKarisAveragePSO	= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/Bloom.hlsl", "DownsampleCS", { "KARIS_AVERAGE=1" });
	m_pBloomUpsamplePSO					= m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/Bloom.hlsl", "UpsampleCS");

	//Visibility Shading
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetVertexShader("FullScreenTriangle.hlsl", "WithTexCoordVS");
		psoDesc.SetPixelShader("VisibilityShading.hlsl", "ShadePS");
		psoDesc.SetRenderTargetFormats(GraphicsCommon::GBufferFormat, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_EQUAL, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, (uint8)StencilBit::VisibilityBuffer, 0x0);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("Visibility Shading");
		m_pVisibilityShadingGraphicsPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	m_pVisibilityDebugRenderPSO = m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "VisibilityDebugView.hlsl", "DebugRenderCS");

	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetVertexShader("FullScreenTriangle.hlsl", "WithTexCoordVS");
		psoDesc.SetPixelShader("VisibilityGBuffer.hlsl", "ShadePS");
		psoDesc.SetRenderTargetFormats(GraphicsCommon::DeferredGBufferFormat, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_ALWAYS);
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_EQUAL, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, (uint8)StencilBit::VisibilityBuffer, 0x0);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetDepthEnabled(false);
		psoDesc.SetName("Visibility Shading");
		m_pVisibilityGBufferPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pDeferredShadePSO = m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "DeferredShading.hlsl", "ShadeCS");
	}

	{
		m_pSkinPSO = m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "Skinning.hlsl", "CSMain");
	}
}


void Renderer::GetViewUniforms(const RenderView& view, ShaderInterop::ViewUniforms& outUniforms)
{
	outUniforms.WorldToView				= view.WorldToView;
	outUniforms.ViewToWorld				= view.ViewToWorld;
	outUniforms.ViewToClip				= view.ViewToClip;
	outUniforms.ClipToView				= view.ClipToView;
	outUniforms.WorldToClip				= view.WorldToClip;
	outUniforms.WorldToClipPrev			= view.WorldToClipPrev;
	outUniforms.ClipToWorld				= view.ClipToView * view.ViewToWorld;
	outUniforms.WorldToClipUnjittered	= view.WorldToClipUnjittered;

	Matrix reprojectionMatrix = outUniforms.ClipToWorld * outUniforms.WorldToClipPrev;
	// Transform from uv to clip space: texcoord * 2 - 1
	Matrix premult = {
		2.0f, 0, 0, 0,
		0, -2.0f, 0, 0,
		0, 0, 1, 0,
		-1, 1, 0, 1
	};
	// Transform from clip to uv space: texcoord * 0.5 + 0.5
	Matrix postmult = {
		0.5f, 0, 0, 0,
		0, -0.5f, 0, 0,
		0, 0, 1, 0,
		0.5f, 0.5f, 0, 1
	};
	outUniforms.UVToPrevUV				= premult * reprojectionMatrix * postmult;
	outUniforms.ViewLocation				= view.Position;
	outUniforms.ViewLocationPrev			= view.PositionPrev;

	outUniforms.ViewportDimensions		= Vector2(view.Viewport.GetWidth(), view.Viewport.GetHeight());
	outUniforms.ViewportDimensionsInv	= Vector2(1.0f / view.Viewport.GetWidth(), 1.0f / view.Viewport.GetHeight());
	outUniforms.ViewJitter				= view.Jitter;
	outUniforms.ViewJitterPrev			= view.JitterPrev;
	outUniforms.NearZ					= view.NearPlane;
	outUniforms.FarZ					= view.FarPlane;
	outUniforms.FoV						= view.FoV;

	outUniforms.FrameIndex				= m_Frame;
	outUniforms.DeltaTime				= Time::DeltaTime();

	outUniforms.NumInstances			= (uint32)m_Batches.size();
	outUniforms.SsrSamples				= Tweakables::gSSRSamples.Get();
	outUniforms.LightCount				= m_LightBuffer.Count;
	outUniforms.CascadeDepths			= m_ShadowCascadeDepths;
	outUniforms.NumCascades				= m_NumShadowCascades;

	outUniforms.TLASIndex				= m_AccelerationStructure.GetSRV() ? m_AccelerationStructure.GetSRV()->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
	outUniforms.MeshesIndex				= m_MeshBuffer.pBuffer->GetSRVIndex();
	outUniforms.MaterialsIndex			= m_MaterialBuffer.pBuffer->GetSRVIndex();
	outUniforms.InstancesIndex			= m_InstanceBuffer.pBuffer->GetSRVIndex();
	outUniforms.LightsIndex				= m_LightBuffer.pBuffer->GetSRVIndex();
	outUniforms.LightMatricesIndex		= m_LightMatricesBuffer.pBuffer->GetSRVIndex();
	outUniforms.SkyIndex				= m_pSky ? m_pSky->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
	outUniforms.DDGIVolumesIndex		= m_DDGIVolumesBuffer.pBuffer->GetSRVIndex();
	outUniforms.NumDDGIVolumes			= m_DDGIVolumesBuffer.Count;

	outUniforms.FontDataIndex			= m_DebugRenderData.FontDataSRV;
	outUniforms.DebugRenderDataIndex	= m_DebugRenderData.RenderDataUAV;
	outUniforms.FontSize				= m_DebugRenderData.FontSize;
}


void Renderer::UploadViewUniforms(CommandContext& context, RenderView& view)
{
	PROFILE_CPU_SCOPE();

	ScratchAllocation alloc = context.AllocateScratch(sizeof(ShaderInterop::ViewUniforms));
	ShaderInterop::ViewUniforms& parameters = alloc.As<ShaderInterop::ViewUniforms>();
	GetViewUniforms(view, parameters);

	if (!view.ViewCB)
		view.ViewCB = context.GetParent()->CreateBuffer(BufferDesc{ .Size = sizeof(ShaderInterop::ViewUniforms), .ElementSize = sizeof(ShaderInterop::ViewUniforms) }, "ViewUniforms");
	context.CopyBuffer(alloc.pBackingResource, view.ViewCB, alloc.Size, alloc.Offset, 0);

	if (view.RequestFreezeCull && !view.FreezeCull)
	{
		view.CullViewCB = context.GetParent()->CreateBuffer(view.ViewCB->GetDesc(), "CullViewUniforms");
		context.InsertResourceBarrier(view.ViewCB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.CopyResource(view.ViewCB, view.CullViewCB);
		context.InsertResourceBarrier(view.ViewCB, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
	}

	view.FreezeCull = view.RequestFreezeCull;
	if (!view.FreezeCull)
	{
		view.CullViewCB = view.ViewCB;
	}
}


void Renderer::UploadSceneData(CommandContext& context)
{
	PROFILE_CPU_SCOPE();
	PROFILE_GPU_SCOPE(context.GetCommandList());

	const World* pWorld = m_pWorld;

	GraphicsDevice* pDevice = context.GetParent();
	auto CopyBufferData = [&](uint32 numElements, uint32 stride, const char* pName, const void* pSource, SceneBuffer& target)
		{
			uint32 desiredElements = Math::AlignUp(Math::Max(1u, numElements), 8u);
			if (!target.pBuffer || desiredElements > target.pBuffer->GetNumElements())
				target.pBuffer = pDevice->CreateBuffer(BufferDesc::CreateStructured(desiredElements, stride, BufferFlag::ShaderResource), pName);
			ScratchAllocation alloc = context.AllocateScratch(numElements * stride);
			memcpy(alloc.pMappedMemory, pSource, numElements * stride);
			context.CopyBuffer(alloc.pBackingResource, target.pBuffer, alloc.Size, alloc.Offset, 0);
			target.Count = numElements;
		};

	Array<Batch> sceneBatches;
	uint32 instanceID = 0;

	// Instances
	{
		Array<ShaderInterop::InstanceData> meshInstances;

		auto view = pWorld->Registry.view<Transform, Model>();
		view.each([&](const Transform& transform, const Model& model)
			{
				const Mesh& mesh = pWorld->Meshes[model.MeshIndex];
				const Material& material = pWorld->Materials[model.MaterialId];

				auto GetBlendMode = [](MaterialAlphaMode mode) {
					switch (mode)
					{
					case MaterialAlphaMode::Blend: return Batch::Blending::AlphaBlend;
					case MaterialAlphaMode::Opaque: return Batch::Blending::Opaque;
					case MaterialAlphaMode::Masked: return Batch::Blending::AlphaMask;
					}
					return Batch::Blending::Opaque;
					};

				Batch& batch = sceneBatches.emplace_back();
				batch.InstanceID = instanceID;
				batch.pMesh = &mesh;
				batch.pMaterial = &material;
				batch.BlendMode = GetBlendMode(material.AlphaMode);
				batch.WorldMatrix = transform.World;
				batch.Radius = Vector3(batch.Bounds.Extents).Length();
				mesh.Bounds.Transform(batch.Bounds, batch.WorldMatrix);

				ShaderInterop::InstanceData& meshInstance = meshInstances.emplace_back();
				meshInstance.ID = instanceID;
				meshInstance.MeshIndex = model.MeshIndex;
				meshInstance.MaterialIndex = model.MaterialId;
				meshInstance.LocalToWorld = transform.World;
				meshInstance.LocalToWorldPrev = transform.WorldPrev;
				meshInstance.LocalBoundsOrigin = mesh.Bounds.Center;
				meshInstance.LocalBoundsExtents = mesh.Bounds.Extents;

				++instanceID;
			});
		CopyBufferData((uint32)meshInstances.size(), sizeof(ShaderInterop::InstanceData), "Instances", meshInstances.data(), m_InstanceBuffer);
	}

	// Meshes
	{
		Array<ShaderInterop::MeshData> meshes;
		meshes.reserve(pWorld->Meshes.size());
		for (const Mesh& mesh : pWorld->Meshes)
		{
			ShaderInterop::MeshData& meshData = meshes.emplace_back();
			meshData.BufferIndex = mesh.pBuffer->GetSRVIndex();
			meshData.IndexByteSize = mesh.IndicesLocation.Stride();
			meshData.IndicesOffset = (uint32)mesh.IndicesLocation.OffsetFromStart;
			meshData.PositionsOffset = mesh.SkinnedPositionStreamLocation.IsValid() ? (uint32)mesh.SkinnedPositionStreamLocation.OffsetFromStart : (uint32)mesh.PositionStreamLocation.OffsetFromStart;
			meshData.NormalsOffset = mesh.SkinnedNormalStreamLocation.IsValid() ? (uint32)mesh.SkinnedNormalStreamLocation.OffsetFromStart : (uint32)mesh.NormalStreamLocation.OffsetFromStart;
			meshData.ColorsOffset = (uint32)mesh.ColorsStreamLocation.OffsetFromStart;
			meshData.UVsOffset = (uint32)mesh.UVStreamLocation.OffsetFromStart;

			meshData.MeshletOffset = mesh.MeshletsLocation;
			meshData.MeshletVertexOffset = mesh.MeshletVerticesLocation;
			meshData.MeshletTriangleOffset = mesh.MeshletTrianglesLocation;
			meshData.MeshletBoundsOffset = mesh.MeshletBoundsLocation;
			meshData.MeshletCount = mesh.NumMeshlets;
		}
		CopyBufferData((uint32)meshes.size(), sizeof(ShaderInterop::MeshData), "Meshes", meshes.data(), m_MeshBuffer);
	}

	// Materials
	{
		Array<ShaderInterop::MaterialData> materials;
		materials.reserve(pWorld->Materials.size());
		for (const Material& material : pWorld->Materials)
		{
			ShaderInterop::MaterialData& materialData = materials.emplace_back();
			materialData.Diffuse = material.pDiffuseTexture ? material.pDiffuseTexture->GetSRVIndex() : -1;
			materialData.Normal = material.pNormalTexture ? material.pNormalTexture->GetSRVIndex() : -1;
			materialData.RoughnessMetalness = material.pRoughnessMetalnessTexture ? material.pRoughnessMetalnessTexture->GetSRVIndex() : -1;
			materialData.Emissive = material.pEmissiveTexture ? material.pEmissiveTexture->GetSRVIndex() : -1;
			materialData.BaseColorFactor = material.BaseColorFactor;
			materialData.MetalnessFactor = material.MetalnessFactor;
			materialData.RoughnessFactor = material.RoughnessFactor;
			materialData.EmissiveFactor = material.EmissiveFactor;
			materialData.AlphaCutoff = material.AlphaCutoff;
			switch (material.AlphaMode)
			{
			case MaterialAlphaMode::Blend:	materialData.RasterBin = 0xFFFFFFFF;	break;
			case MaterialAlphaMode::Opaque: materialData.RasterBin = 0;				break;
			case MaterialAlphaMode::Masked: materialData.RasterBin = 1;				break;
			}
		}
		CopyBufferData((uint32)materials.size(), sizeof(ShaderInterop::MaterialData), "Materials", materials.data(), m_MaterialBuffer);
	}

	// DDGI
	{
		Array<ShaderInterop::DDGIVolume> ddgiVolumes;
		if (Tweakables::gEnableDDGI)
		{
			auto ddgi_view = pWorld->Registry.view<Transform, DDGIVolume>();
			ddgi_view.each([&](const Transform& transform, const DDGIVolume& volume)
				{
					ShaderInterop::DDGIVolume& ddgi = ddgiVolumes.emplace_back();
					ddgi.BoundsMin = transform.Position - volume.Extents;
					ddgi.ProbeSize = 2 * volume.Extents / (Vector3((float)volume.NumProbes.x, (float)volume.NumProbes.y, (float)volume.NumProbes.z) - Vector3::One);
					ddgi.ProbeVolumeDimensions = Vector3u(volume.NumProbes.x, volume.NumProbes.y, volume.NumProbes.z);
					ddgi.IrradianceIndex = volume.pIrradianceHistory ? volume.pIrradianceHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.DepthIndex = volume.pDepthHistory ? volume.pDepthHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.ProbeOffsetIndex = volume.pProbeOffset ? volume.pProbeOffset->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.ProbeStatesIndex = volume.pProbeStates ? volume.pProbeStates->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.NumRaysPerProbe = volume.NumRays;
					ddgi.MaxRaysPerProbe = volume.MaxNumRays;
				});
		}
		CopyBufferData((uint32)ddgiVolumes.size(), sizeof(ShaderInterop::DDGIVolume), "DDGI Volumes", ddgiVolumes.data(), m_DDGIVolumesBuffer);
	}
	// Lights
	{
		Array<ShaderInterop::Light> lightData;
		auto light_view = pWorld->Registry.view<const Transform, const Light>();
		light_view.each([&](const Transform& transform, const Light& light)
			{
				ShaderInterop::Light& data = lightData.emplace_back();
				data.Position = transform.Position;
				data.Direction = Vector3::Transform(Vector3::Forward, transform.Rotation);
				data.SpotlightAngles.x = cos(light.InnerConeAngle / 2.0f);
				data.SpotlightAngles.y = cos(light.OuterConeAngle / 2.0f);
				data.Color = Math::Pack_RGBA8_UNORM(light.Colour);
				data.Intensity = light.Intensity;
				data.Range = light.Range;
				data.ShadowMapIndex = light.CastShadows && light.ShadowMaps.size() ? light.ShadowMaps[0]->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				data.MaskTexture = light.pLightTexture ? light.pLightTexture->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				data.MatrixIndex = light.MatrixIndex;
				data.InvShadowSize = 1.0f / light.ShadowMapSize;
				data.IsEnabled = light.Intensity > 0 ? 1 : 0;
				data.IsVolumetric = light.VolumetricLighting;
				data.CastShadows = light.ShadowMaps.size() && light.CastShadows;
				data.IsPoint = light.Type == LightType::Point;
				data.IsSpot = light.Type == LightType::Spot;
				data.IsDirectional = light.Type == LightType::Directional;
			});
		CopyBufferData((uint32)lightData.size(), sizeof(ShaderInterop::Light), "Lights", lightData.data(), m_LightBuffer);
	}

	// Shadow Matrices
	{
		Array<Matrix> lightMatrices(m_ShadowViews.size());
		for (uint32 i = 0; i < m_ShadowViews.size(); ++i)
			lightMatrices[i] = m_ShadowViews[i].WorldToClip;
		CopyBufferData((uint32)lightMatrices.size(), sizeof(Matrix), "Light Matrices", lightMatrices.data(), m_LightMatricesBuffer);
	}

	sceneBatches.swap(m_Batches);

	// View Uniform Buffers
	{
		Renderer::UploadViewUniforms(context, m_MainView);
	}
}


void Renderer::DrawScene(CommandContext& context, const RenderView& view, Batch::Blending blendModes)
{
	DrawScene(context, view.pRenderer->GetBatches(), view.VisibilityMask, blendModes);
}


void Renderer::DrawScene(CommandContext& context, Span<const Batch> batches, const VisibilityMask& visibility, Batch::Blending blendModes)
{
	PROFILE_CPU_SCOPE();
	PROFILE_GPU_SCOPE(context.GetCommandList());
	gAssert(batches.GetSize() <= visibility.Size());
	for (const Batch& b : batches)
	{
		if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.InstanceID))
		{
			PROFILE_CPU_SCOPE("Draw Primitive");
			PROFILE_GPU_SCOPE(context.GetCommandList(), "Draw Pritimive");
			context.BindRootCBV(BindingSlot::PerInstance, b.InstanceID);
			context.DispatchMesh(Math::DivideAndRoundUp(b.pMesh->NumMeshlets, 32));
		}
	}
}

void Renderer::BindViewUniforms(CommandContext& context, const RenderView& view, RenderView::Type type)
{
	// Binding the cull view only works for RenderViews that have a VRAM Buffer
	const Buffer* pViewBuffer = type == RenderView::Type::Default ? view.ViewCB : view.CullViewCB;
	if (pViewBuffer)
	{
		context.BindRootCBV(BindingSlot::PerView, pViewBuffer);
	}
	else
	{
		ShaderInterop::ViewUniforms viewUniforms;
		view.pRenderer->GetViewUniforms(view, viewUniforms);
		context.BindRootCBV(BindingSlot::PerView, viewUniforms);
	}
}

void Renderer::MakeScreenshot(Texture* pSource)
{
	TaskContext taskContext;
	TaskQueue::Execute([this, pSource](uint32)
		{
			CommandContext* pScreenshotContext = m_pDevice->AllocateCommandContext();
			uint32 width = pSource->GetWidth();
			uint32 height = pSource->GetHeight();

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
			D3D12_RESOURCE_DESC resourceDesc = pSource->GetResource()->GetDesc();
			m_pDevice->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);
			Ref<Buffer> pScreenshotBuffer = m_pDevice->CreateBuffer(BufferDesc::CreateReadback(textureFootprint.Footprint.RowPitch * textureFootprint.Footprint.Height), "Screenshot Texture");
			pScreenshotContext->InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_UNKNOWN, D3D12_RESOURCE_STATE_COPY_SOURCE);
			pScreenshotContext->CopyTexture(pSource, pScreenshotBuffer, CD3DX12_BOX(0, 0, width, height));

			SyncPoint fence = pScreenshotContext->Execute();
			fence.Wait();

			char* pData = (char*)pScreenshotBuffer->GetMappedData();
			Image img(width, height, 1, ResourceFormat::RGBA8_UNORM, 1);
			uint32 imageRowPitch = width * 4;
			uint32 targetOffset = 0;
			for (uint32 i = 0; i < height; ++i)
			{
				img.SetData((uint32*)pData, targetOffset, imageRowPitch);
				pData += textureFootprint.Footprint.RowPitch;
				targetOffset += imageRowPitch;
			}

			Paths::CreateDirectoryTree(Paths::ScreenshotDir());
			img.Save(Sprintf("%sScreenshot_%s.png", Paths::ScreenshotDir().c_str(), Utils::GetTimeString().c_str()).c_str());
		}, taskContext);
}


void Renderer::CreateShadowViews(const RenderView& mainView)
{
	PROFILE_CPU_SCOPE("Shadow Setup");

	float minPoint = 0;
	float maxPoint = 1;

	const uint32 numCascades = Tweakables::gShadowCascades;
	const float pssmLambda = Tweakables::gPSSMFactor;
	m_NumShadowCascades = numCascades;

	if (Tweakables::gSDSM)
	{
		Buffer* pSourceBuffer = m_ReductionReadbackTargets[(m_Frame + 1) % GraphicsDevice::NUM_BUFFERS];
		if (pSourceBuffer)
		{
			Vector2* pData = (Vector2*)pSourceBuffer->GetMappedData();
			minPoint = pData->x;
			maxPoint = pData->y;
		}
	}

	const ViewTransform& viewTransform = mainView;
	float n = viewTransform.NearPlane;
	float f = viewTransform.FarPlane;
	float nearPlane = Math::Min(n, f);
	float farPlane = Math::Max(n, f);
	float clipPlaneRange = farPlane - nearPlane;

	float minZ = nearPlane + minPoint * clipPlaneRange;
	float maxZ = nearPlane + maxPoint * clipPlaneRange;

	constexpr uint32 MAX_CASCADES = 4;
	StaticArray<float, MAX_CASCADES> cascadeSplits{};

	for (uint32 i = 0; i < numCascades; ++i)
	{
		float p = (i + 1) / (float)numCascades;
		float log = minZ * std::pow(maxZ / minZ, p);
		float uniform = minZ + (maxZ - minZ) * p;
		float d = pssmLambda * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearPlane) / clipPlaneRange;
	}

	int32 shadowIndex = 0;
	m_ShadowViews.clear();
	auto AddShadowView = [&](Light& light, ShadowView shadowView, uint32 resolution, uint32 shadowMapLightIndex)
		{
			if (shadowMapLightIndex == 0)
				light.MatrixIndex = shadowIndex;
			if (shadowIndex >= (int32)m_ShadowMaps.size())
				m_ShadowMaps.push_back(m_pDevice->CreateTexture(TextureDesc::Create2D(resolution, resolution, GraphicsCommon::ShadowFormat, 1, TextureFlag::DepthStencil | TextureFlag::ShaderResource, ClearBinding(0.0f, 0)), Sprintf("Shadow Map %d", (uint32)m_ShadowMaps.size()).c_str()));
			Ref<Texture> pTarget = m_ShadowMaps[shadowIndex];

			light.ShadowMaps.resize(Math::Max(shadowMapLightIndex + 1, (uint32)light.ShadowMaps.size()));
			light.ShadowMaps[shadowMapLightIndex] = pTarget;
			light.ShadowMapSize = resolution;
			shadowView.pDepthTexture = pTarget;
			shadowView.pLight = &light;
			shadowView.ViewIndex = shadowMapLightIndex;
			shadowView.Viewport = FloatRect(0, 0, (float)resolution, (float)resolution);
			shadowView.pWorld = m_pWorld;
			shadowView.pRenderer = this;
			m_ShadowViews.push_back(shadowView);
			shadowIndex++;
		};

	auto light_view = m_pWorld->Registry.view<const Transform, Light>();
	light_view.each([&](const Transform& transform, Light& light)
		{
			light.ShadowMaps.clear();

			if (!light.CastShadows)
				return;

			if (light.Type == LightType::Directional)
			{
				// Frustum corners in world space
				const Matrix vpInverse = viewTransform.WorldToClip.Invert();
				const Vector3 frustumCornersWS[] = {
					Vector3::Transform(Vector3(-1, -1, 1), vpInverse),
					Vector3::Transform(Vector3(-1, -1, 0), vpInverse),
					Vector3::Transform(Vector3(-1, 1, 1), vpInverse),
					Vector3::Transform(Vector3(-1, 1, 0), vpInverse),
					Vector3::Transform(Vector3(1, 1, 1), vpInverse),
					Vector3::Transform(Vector3(1, 1, 0), vpInverse),
					Vector3::Transform(Vector3(1, -1, 1), vpInverse),
					Vector3::Transform(Vector3(1, -1, 0), vpInverse),
				};

				const Matrix lightView = transform.World.Invert();
				for (int i = 0; i < Tweakables::gShadowCascades; ++i)
				{
					float previousCascadeSplit = i == 0 ? minPoint : cascadeSplits[i - 1];
					float currentCascadeSplit = cascadeSplits[i];

					// Compute the frustum corners for the cascade in view space
					const Vector3 cornersVS[] = {
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[0], frustumCornersWS[1], previousCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[0], frustumCornersWS[1], currentCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[2], frustumCornersWS[3], previousCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[2], frustumCornersWS[3], currentCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[4], frustumCornersWS[5], previousCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[4], frustumCornersWS[5], currentCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[6], frustumCornersWS[7], previousCascadeSplit), lightView),
						Vector3::Transform(Vector3::Lerp(frustumCornersWS[6], frustumCornersWS[7], currentCascadeSplit), lightView),
					};

					Vector3 center = Vector3::Zero;
					for (const Vector3& corner : cornersVS)
						center += corner;
					center /= ARRAYSIZE(cornersVS);

					//Create a bounding sphere to maintain aspect in projection to avoid flickering when rotating
					float radius = 0;
					for (const Vector3& corner : cornersVS)
					{
						float dist = Vector3::Distance(center, corner);
						radius = Math::Max(dist, radius);
					}
					Vector3 minExtents = center - Vector3(radius);
					Vector3 maxExtents = center + Vector3(radius);

					// Snap the cascade to the resolution of the shadowmap
					Vector3 extents = maxExtents - minExtents;
					Vector3 texelSize = extents / 2048;
					minExtents = Math::Floor(minExtents / texelSize) * texelSize;
					maxExtents = Math::Floor(maxExtents / texelSize) * texelSize;
					center = (minExtents + maxExtents) * 0.5f;

					// Extent the Z bounds
					float extentsZ = fabs(center.z - minExtents.z);
					extentsZ = Math::Max(extentsZ, Math::Min(1500.0f, farPlane) * 0.5f);
					minExtents.z = center.z - extentsZ;
					maxExtents.z = center.z + extentsZ;

					Matrix projectionMatrix = Math::CreateOrthographicOffCenterMatrix(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z);

					ShadowView shadowView;
					shadowView.IsPerspective = false;
					shadowView.WorldToClip = lightView * projectionMatrix;
					shadowView.WorldToClipPrev = shadowView.WorldToClip;
					shadowView.OrthographicFrustum.Center = center;
					shadowView.OrthographicFrustum.Extents = maxExtents - minExtents;
					shadowView.OrthographicFrustum.Extents.z *= 10;
					shadowView.OrthographicFrustum.Orientation = Quaternion::CreateFromRotationMatrix(lightView.Invert());
					(&m_ShadowCascadeDepths.x)[i] = nearPlane + currentCascadeSplit * (farPlane - nearPlane);
					AddShadowView(light, shadowView, 2048, i);
				}
			}
			else if (light.Type == LightType::Spot)
			{
				BoundingBox box(transform.Position, Vector3(light.Range));
				if (!viewTransform.PerspectiveFrustum.Contains(box))
					return;

				const Matrix projection = Math::CreatePerspectiveMatrix(light.OuterConeAngle, 1.0f, light.Range, 0.01f);
				const Matrix lightView = transform.World.Invert();

				ShadowView shadowView;
				shadowView.IsPerspective = true;
				shadowView.WorldToClip = lightView * projection;
				shadowView.WorldToClipPrev = shadowView.WorldToClip;
				shadowView.PerspectiveFrustum = Math::CreateBoundingFrustum(projection, lightView);
				AddShadowView(light, shadowView, 512, 0);
			}
			else if (light.Type == LightType::Point)
			{
				BoundingSphere sphere(transform.Position, light.Range);
				if (!viewTransform.PerspectiveFrustum.Contains(sphere))
					return;

				Matrix viewMatrices[] = {
					Math::CreateLookToMatrix(transform.Position, Vector3::Right,	Vector3::Up),
					Math::CreateLookToMatrix(transform.Position, Vector3::Left,		Vector3::Up),
					Math::CreateLookToMatrix(transform.Position, Vector3::Up,		Vector3::Forward),
					Math::CreateLookToMatrix(transform.Position, Vector3::Down,		Vector3::Backward),
					Math::CreateLookToMatrix(transform.Position, Vector3::Backward, Vector3::Up),
					Math::CreateLookToMatrix(transform.Position, Vector3::Forward,	Vector3::Up),
				};
				Matrix projection = Math::CreatePerspectiveMatrix(Math::PI_DIV_2, 1, light.Range, 0.01f);

				for (int i = 0; i < ARRAYSIZE(viewMatrices); ++i)
				{
					ShadowView shadowView;
					shadowView.IsPerspective = true;
					shadowView.WorldToClip = viewMatrices[i] * projection;
					shadowView.WorldToClipPrev = shadowView.WorldToClip;
					shadowView.PerspectiveFrustum = Math::CreateBoundingFrustum(projection, viewMatrices[i]);
					AddShadowView(light, shadowView, 512, i);
				}
			}
		});

	m_ShadowHZBs.resize(shadowIndex);
}


void Renderer::DrawImGui(FloatRect viewport)
{
	ImVec2 viewportOrigin(viewport.Left, viewport.Top);
	ImVec2 viewportExtents(viewport.GetWidth(), viewport.GetHeight());

	if (m_pCaptureTextureSystem)
		m_pCaptureTextureSystem->RenderUI(m_CaptureTextureContext, viewportOrigin, viewportExtents);

	if (Tweakables::gDrawHistogram && m_pDebugHistogramTexture)
	{
		ImGui::Begin("Luminance Histogram");
		ImVec2 cursor = ImGui::GetCursorPos();
		ImVec2 size = ImGui::GetAutoSize(ImVec2((float)m_pDebugHistogramTexture->GetWidth(), (float)m_pDebugHistogramTexture->GetHeight()));
		ImGui::Image(m_pDebugHistogramTexture, size);
		ImGui::GetWindowDrawList()->AddText(cursor, IM_COL32(255, 255, 255, 255), Sprintf("%.2f", Tweakables::gMinLogLuminance.Get()).c_str());
		ImGui::End();
	}

	if (Tweakables::gVisualizeShadowCascades)
	{
		ImDrawList* pDraw = ImGui::GetWindowDrawList();
		float cascadeImageSize = 256.0f;
		ImVec2 cursor = viewportOrigin + ImVec2(5, viewportExtents.y - cascadeImageSize - 5);

		const Light& sunLight = m_pWorld->Registry.get<Light>(m_pWorld->Sunlight);
		for (int i = 0; i < Tweakables::gShadowCascades; ++i)
		{
			if (i < sunLight.ShadowMaps.size())
			{
				const RenderView& shadowView = m_ShadowViews[sunLight.MatrixIndex + i];
				const Matrix& lightViewProj = shadowView.WorldToClip;

				const ViewTransform& viewTransform = m_MainView;
				BoundingFrustum frustum = Math::CreateBoundingFrustum(Math::CreatePerspectiveMatrix(viewTransform.FoV, viewTransform.Viewport.GetAspect(), viewTransform.FarPlane, (&m_ShadowCascadeDepths.x)[i]), viewTransform.WorldToView);
				DirectX::XMFLOAT3 frustumCorners[8];
				frustum.GetCorners(frustumCorners);

				ImVec2 corners[8];
				for (int c = 0; c < 8; ++c)
				{
					Vector4 corner;
					corner = Vector4::Transform(Vector4(frustumCorners[c].x, frustumCorners[c].y, frustumCorners[c].z, 1), lightViewProj);
					corner.x /= corner.w;
					corner.y /= corner.w;
					corner.x = corner.x * 0.5f + 0.5f;
					corner.y = -corner.y * 0.5f + 0.5f;
					corners[c] = ImVec2(corner.x, corner.y) * cascadeImageSize;
				}

				pDraw->AddImage(sunLight.ShadowMaps[i], cursor, cursor + ImVec2(cascadeImageSize, cascadeImageSize));

				ImColor clr(0.7f, 1.0f, 1.0f, 0.5f);
				pDraw->AddLine(cursor + corners[0], cursor + corners[4], clr);
				pDraw->AddLine(cursor + corners[1], cursor + corners[5], clr);
				pDraw->AddLine(cursor + corners[2], cursor + corners[6], clr);
				pDraw->AddLine(cursor + corners[3], cursor + corners[7], clr);

				pDraw->AddLine(cursor + corners[0], cursor + corners[1], clr);
				pDraw->AddLine(cursor + corners[1], cursor + corners[2], clr);
				pDraw->AddLine(cursor + corners[2], cursor + corners[3], clr);
				pDraw->AddLine(cursor + corners[3], cursor + corners[0], clr);

				pDraw->AddLine(cursor + corners[4], cursor + corners[5], clr);
				pDraw->AddLine(cursor + corners[5], cursor + corners[6], clr);
				pDraw->AddLine(cursor + corners[6], cursor + corners[7], clr);
				pDraw->AddLine(cursor + corners[7], cursor + corners[4], clr);
				cursor.x += cascadeImageSize + 5;
			}
		}
	}


	if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_R))
		Tweakables::gRenderGraphResourceTracker = !Tweakables::gRenderGraphResourceTracker;
	if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_T))
		Tweakables::gRenderGraphPassView = !Tweakables::gRenderGraphPassView;


	if (ImGui::Begin("Settings"))
	{
		if (ImGui::CollapsingHeader("General"))
		{
			static constexpr const char* pPathNames[] =
			{
				"Tiled",
				"Clustered",
				"Path Tracing",
				"Visibility",
				"Visibility Deferred",
			};
			ImGui::Combo("Render Path", (int*)&m_RenderPath, pPathNames, ARRAYSIZE(pPathNames));

			if (m_RenderPath == RenderPath::Visibility || m_RenderPath == RenderPath::VisibilityDeferred)
			{
				ImGui::Checkbox("Freeze Culling", &m_MainView.RequestFreezeCull);
				ImGui::Checkbox("Occlusion Culling", &Tweakables::gOcclusionCulling.Get());
				static constexpr const char* pDebugViewNames[] =
				{
					"Off",
					"InstanceID",
					"MeshletID",
					"PrimitiveID",
					"Overdraw",
				};
				ImGui::Combo("VisBuffer Debug View", &Tweakables::gVisibilityDebugMode.Get(), pDebugViewNames, ARRAYSIZE(pDebugViewNames));

				ImGui::Checkbox("Cull statistics", &Tweakables::gCullDebugStats.Get());
				ImGui::Checkbox("Work Graph", &Tweakables::gWorkGraph.Get());
			}

			{
				ViewTransform& view = m_MainView;
				ImGui::Text("Camera");
				ImGui::Text("Location: [%.2f, %.2f, %.2f]", view.Position.x, view.Position.y, view.Position.z);
				float fov = view.FoV;
				if (ImGui::SliderAngle("Field of View", &fov, 10, 120))
					view.FoV = fov;
				Vector2 farNear(view.FarPlane, view.NearPlane);
				if (ImGui::DragFloatRange2("Near/Far", &farNear.x, &farNear.y, 1, 0.1f, 100))
				{
					view.FarPlane = farNear.x;
					view.NearPlane = farNear.y;
				}
			}
		}

		if (ImGui::CollapsingHeader("Render Graph"))
		{
			ImGui::Checkbox("RenderGraph Jobify", &Tweakables::gRenderGraphJobify.Get());
			ImGui::Checkbox("RenderGraph Aliasing", &Tweakables::gRenderGraphResourceAliasing.Get());
			ImGui::Checkbox("RenderGraph Pass Culling", &Tweakables::gRenderGraphPassCulling.Get());
			ImGui::Checkbox("RenderGraph State Tracking", &Tweakables::gRenderGraphStateTracking.Get());
			ImGui::SliderInt("RenderGraph Pass Group Size", &Tweakables::gRenderGraphPassGroupSize.Get(), 5, 50);
		}

		if (ImGui::CollapsingHeader("Atmosphere"))
		{
			if (m_pWorld->Registry.valid(m_pWorld->Sunlight))
			{
				Light& sunLight = m_pWorld->Registry.get<Light>(m_pWorld->Sunlight);
				Transform& sunTransform = m_pWorld->Registry.get<Transform>(m_pWorld->Sunlight);
				Vector3 euler = sunTransform.Rotation.ToEuler();

				if (ImGui::SliderFloat("Sun Orientation", &euler.y, -Math::PI, Math::PI))
					sunTransform.Rotation = Quaternion::CreateFromYawPitchRoll(euler);
				if (ImGui::SliderFloat("Sun Inclination", &euler.x, 0, Math::PI / 2))
					sunTransform.Rotation = Quaternion::CreateFromYawPitchRoll(euler);
				ImGui::SliderFloat("Sun Intensity", &sunLight.Intensity, 0, 30);
			}

			ImGui::Checkbox("Sky", &Tweakables::gSky.Get());
			ImGui::Checkbox("Volumetric Fog", &Tweakables::gVolumetricFog.Get());
			ImGui::Checkbox("Clouds", &Tweakables::gClouds.Get());
		}

		if (ImGui::CollapsingHeader("Shadows"))
		{
			ImGui::SliderInt("Shadow Cascades", &Tweakables::gShadowCascades.Get(), 1, 4);
			ImGui::Checkbox("SDSM", &Tweakables::gSDSM.Get());
			ImGui::SliderFloat("PSSM Factor", &Tweakables::gPSSMFactor.Get(), 0, 1);
			ImGui::Checkbox("Visualize Cascades", &Tweakables::gVisualizeShadowCascades.Get());
			ImGui::Checkbox("GPU Cull", &Tweakables::gShadowsGPUCull.Get());
			if (Tweakables::gShadowsGPUCull)
			{
				ImGui::Checkbox("GPU Occlusion Cull", &Tweakables::gShadowsOcclusionCulling.Get());
				ImGui::SliderInt("GPU Cull Stats", &Tweakables::gCullShadowsDebugStats.Get(), -1, (int)m_ShadowViews.size() - 1);
			}
		}
		if (ImGui::CollapsingHeader("Bloom"))
		{
			ImGui::Checkbox("Enabled", &Tweakables::gBloom.Get());
			ImGui::SliderFloat("Intensity", &Tweakables::gBloomIntensity.Get(), 0.0f, 4.0f);
			ImGui::SliderFloat("Blend Factor", &Tweakables::gBloomBlendFactor.Get(), 0.0f, 1.0f);
			ImGui::SliderFloat("Internal Blend Factor", &Tweakables::gBloomInteralBlendFactor.Get(), 0.0f, 1.0f);
			ImGui::ColorEdit3("Lens Dirt Tint", &m_LensDirtTint.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		}
		if (ImGui::CollapsingHeader("Exposure/Tonemapping"))
		{
			ImGui::DragFloatRange2("Log Luminance", &Tweakables::gMinLogLuminance.Get(), &Tweakables::gMaxLogLuminance.Get(), 1.0f, -100, 50);
			ImGui::Checkbox("Draw Exposure Histogram", &Tweakables::gDrawHistogram.Get());
			ImGui::SliderFloat("White Point", &Tweakables::gWhitePoint.Get(), 0, 20);
			ImGui::SliderFloat("Tau", &Tweakables::gTau.Get(), 0, 5);

			static constexpr const char* pTonemapperNames[] = {
				"Reinhard",
				"Reinhard Extended",
				"ACES Fast",
				"Unreal 3",
				"Uncharted 2",
			};
			ImGui::Combo("Tonemapper", (int*)&Tweakables::gToneMapper.Get(), pTonemapperNames, ARRAYSIZE(pTonemapperNames));
		}

		if (ImGui::CollapsingHeader("Misc"))
		{
			ImGui::Checkbox("TAA", &Tweakables::gTAA.Get());
			ImGui::Checkbox("Visualize Light Density", &Tweakables::gVisualizeLightDensity.Get());
			ImGui::SliderInt("SSR Samples", &Tweakables::gSSRSamples.Get(), 0, 32);
			ImGui::Checkbox("Object Bounds", &Tweakables::gRenderObjectBounds.Get());
			ImGui::Checkbox("Render Terrain", &Tweakables::gRenderTerrain.Get());
		}

		if (ImGui::CollapsingHeader("Raytracing"))
		{
			if (m_pDevice->GetCapabilities().SupportsRaytracing())
			{
				ImGui::Checkbox("Raytraced AO", &Tweakables::gRaytracedAO.Get());
				ImGui::Checkbox("Raytraced Reflections", &Tweakables::gRaytracedReflections.Get());
				ImGui::Checkbox("DDGI", &Tweakables::gEnableDDGI.Get());
				ImGui::Checkbox("Visualize DDGI", &Tweakables::gVisualizeDDGI.Get());
			}
		}
	}
	ImGui::End();
}
