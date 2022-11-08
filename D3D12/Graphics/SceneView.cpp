#include "stdafx.h"
#include "SceneView.h"
#include "Core/Utils.h"
#include "RHI/Buffer.h"
#include "RHI/PipelineState.h"
#include "RHI/Texture.h"
#include "RHI/Graphics.h"
#include "RHI/CommandContext.h"
#include "RHI/DynamicResourceAllocator.h"
#include "Mesh.h"
#include "Light.h"
#include "Core/ConsoleVariables.h"
#include "Content/Image.h"
#include "Profiler.h"

namespace Tweakables
{
	extern ConsoleVariable<int> g_SsrSamples;
	extern ConsoleVariable<bool> g_EnableDDGI;
}

namespace Renderer
{
	void DrawScene(CommandContext& context, const SceneView* pView, Batch::Blending blendModes)
	{
		DrawScene(context, pView, pView->VisibilityMask, blendModes);
	}

	ShaderInterop::ViewUniforms GetViewUniforms(const SceneView* pView, Texture* pTarget)
	{
		ShaderInterop::ViewUniforms parameters;
		const ViewTransform& view = pView->View;

		parameters.View = view.View;
		parameters.ViewInverse = view.ViewInverse;
		parameters.Projection = view.Projection;
		parameters.ProjectionInverse = view.ProjectionInverse;
		parameters.ViewProjection = view.ViewProjection;
		parameters.ViewProjectionPrev = view.ViewProjectionPrev;
		parameters.ViewProjectionFrozen = view.ViewProjectionFrozen;
		parameters.ViewProjectionInverse = view.ProjectionInverse * view.ViewInverse;

		Matrix reprojectionMatrix = parameters.ViewProjectionInverse * view.ViewProjectionPrev;
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

		parameters.ReprojectionMatrix = premult * reprojectionMatrix * postmult;
		parameters.ViewLocation = view.Position;
		parameters.ViewLocationPrev = view.PositionPrev;

		DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
		view.Frustum.GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
		parameters.FrustumPlanes[0] = Vector4(nearPlane);
		parameters.FrustumPlanes[1] = Vector4(farPlane);
		parameters.FrustumPlanes[2] = Vector4(left);
		parameters.FrustumPlanes[3] = Vector4(right);
		parameters.FrustumPlanes[4] = Vector4(top);
		parameters.FrustumPlanes[5] = Vector4(bottom);

		if (pTarget)
		{
			parameters.TargetDimensions = Vector2((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
			parameters.TargetDimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
		}
		parameters.ViewportDimensions = Vector2(view.Viewport.GetWidth(), view.Viewport.GetHeight());
		parameters.ViewportDimensionsInv = Vector2(1.0f / view.Viewport.GetWidth(), 1.0f / view.Viewport.GetHeight());
		parameters.HZBDimensions = pView->HZBDimensions;
		parameters.ViewJitter.x = view.PreviousJitter.x - view.Jitter.x;
		parameters.ViewJitter.y = -(view.PreviousJitter.y - view.Jitter.y);
		parameters.NearZ = view.NearPlane;
		parameters.FarZ = view.FarPlane;
		parameters.FoV = view.FoV;

		parameters.FrameIndex = pView->FrameIndex;
		parameters.NumInstances = (uint32)pView->Batches.size();
		parameters.SsrSamples = Tweakables::g_SsrSamples.Get();
		parameters.LightCount = pView->NumLights;

		check(pView->ShadowViews.size() <= MAX_SHADOW_CASTERS);
		for (uint32 i = 0; i < pView->ShadowViews.size(); ++i)
		{
			parameters.LightMatrices[i] = pView->ShadowViews[i].ViewProjection;
		}
		parameters.CascadeDepths = pView->ShadowCascadeDepths;
		parameters.NumCascades = pView->NumShadowCascades;

		parameters.TLASIndex = pView->AccelerationStructure.GetSRV() ? pView->AccelerationStructure.GetSRV()->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
		parameters.MeshesIndex = pView->pMeshBuffer->GetSRVIndex();
		parameters.MaterialsIndex = pView->pMaterialBuffer->GetSRVIndex();
		parameters.InstancesIndex = pView->pInstanceBuffer->GetSRVIndex();
		parameters.LightsIndex = pView->pLightBuffer->GetSRVIndex();
		parameters.SkyIndex = pView->pSky ? pView->pSky->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
		parameters.DDGIVolumesIndex = pView->pDDGIVolumesBuffer->GetSRVIndex();
		parameters.NumDDGIVolumes = pView->NumDDGIVolumes;

		parameters.LTCAmplitudeIndex = pView->pLTCAmplitudeTexture->GetSRVIndex();
		parameters.LTCMatrixIndex = pView->pLTCMatrixTexture->GetSRVIndex();
		parameters.FontDataIndex = pView->DebugRenderData.FontDataSRV;
		parameters.DebugRenderDataIndex = pView->DebugRenderData.RenderDataUAV;

		return parameters;
	}

	void UploadSceneData(CommandContext& context, SceneView* pView, World* pWorld)
	{
		GPU_PROFILE_SCOPE("Upload Scene Data", &context);

		std::vector<ShaderInterop::MaterialData> materials;
		materials.reserve(128);
		std::vector<ShaderInterop::MeshData> meshes;
		meshes.reserve(128);
		std::vector<ShaderInterop::InstanceData> meshInstances;
		meshInstances.reserve(pView->Batches.size());
		std::vector<Batch> sceneBatches;
		sceneBatches.reserve(pView->Batches.size());
		uint32 instanceID = 0;

		for (const auto& pMesh : pWorld->Meshes)
		{
			for (const SubMeshInstance& node : pMesh->GetMeshInstances())
			{
				SubMesh& parentMesh = pMesh->GetMesh(node.MeshIndex);
				const Material& meshMaterial = pMesh->GetMaterial(parentMesh.MaterialId);

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
				batch.pMesh = &parentMesh;
				batch.BlendMode = GetBlendMode(meshMaterial.AlphaMode);
				batch.WorldMatrix = node.Transform;
				parentMesh.Bounds.Transform(batch.Bounds, batch.WorldMatrix);
				batch.Radius = Vector3(batch.Bounds.Extents).Length();

				ShaderInterop::InstanceData& meshInstance = meshInstances.emplace_back();
				meshInstance.ID = instanceID;
				meshInstance.MeshIndex = (uint32)meshes.size() + node.MeshIndex;
				meshInstance.MaterialIndex = (uint32)materials.size() + parentMesh.MaterialId;
				meshInstance.LocalToWorld = node.Transform;
				meshInstance.LocalToWorldPrev = node.Transform; //#todo
				meshInstance.LocalBoundsOrigin = parentMesh.Bounds.Center;
				meshInstance.LocalBoundsExtents = parentMesh.Bounds.Extents;

				++instanceID;
			}

			for (const SubMesh& subMesh : pMesh->GetMeshes())
			{
				ShaderInterop::MeshData& mesh = meshes.emplace_back();
				mesh.BufferIndex = pMesh->GetData()->GetSRVIndex();
				mesh.IndexByteSize = subMesh.IndicesLocation.Stride();
				mesh.IndicesOffset = (uint32)subMesh.IndicesLocation.OffsetFromStart;
				mesh.PositionsOffset = (uint32)subMesh.PositionStreamLocation.OffsetFromStart;
				mesh.NormalsOffset = (uint32)subMesh.NormalStreamLocation.OffsetFromStart;
				mesh.ColorsOffset = (uint32)subMesh.ColorsStreamLocation.OffsetFromStart;
				mesh.UVsOffset = (uint32)subMesh.UVStreamLocation.OffsetFromStart;

				mesh.MeshletOffset = subMesh.MeshletsLocation;
				mesh.MeshletVertexOffset = subMesh.MeshletVerticesLocation;
				mesh.MeshletTriangleOffset = subMesh.MeshletTrianglesLocation;
				mesh.MeshletBoundsOffset = subMesh.MeshletBoundsLocation;
				mesh.MeshletCount = subMesh.NumMeshlets;
			}

			for (const Material& material : pMesh->GetMaterials())
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
			}
		}
		sceneBatches.swap(pView->Batches);

		std::vector<ShaderInterop::DDGIVolume> ddgiVolumes;
		if (Tweakables::g_EnableDDGI)
		{
			ddgiVolumes.reserve(pWorld->DDGIVolumes.size());
			for (DDGIVolume& ddgiVolume : pWorld->DDGIVolumes)
			{
				ShaderInterop::DDGIVolume& ddgi = ddgiVolumes.emplace_back();
				ddgi.BoundsMin = ddgiVolume.Origin - ddgiVolume.Extents;
				ddgi.ProbeSize = 2 * ddgiVolume.Extents / (Vector3((float)ddgiVolume.NumProbes.x, (float)ddgiVolume.NumProbes.y, (float)ddgiVolume.NumProbes.z) - Vector3::One);
				ddgi.ProbeVolumeDimensions = Vector3u(ddgiVolume.NumProbes.x, ddgiVolume.NumProbes.y, ddgiVolume.NumProbes.z);
				ddgi.IrradianceIndex = ddgiVolume.pIrradianceHistory ? ddgiVolume.pIrradianceHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.DepthIndex = ddgiVolume.pDepthHistory ? ddgiVolume.pDepthHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.ProbeOffsetIndex = ddgiVolume.pProbeOffset ? ddgiVolume.pProbeOffset->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.ProbeStatesIndex = ddgiVolume.pProbeStates ? ddgiVolume.pProbeStates->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.NumRaysPerProbe = ddgiVolume.NumRays;
				ddgi.MaxRaysPerProbe = ddgiVolume.MaxNumRays;
			}
		}
		pView->NumDDGIVolumes = (uint32)ddgiVolumes.size();

		std::vector<ShaderInterop::Light> lightData;
		lightData.reserve(pWorld->Lights.size());
		for (const Light& light : pWorld->Lights)
		{
			ShaderInterop::Light& data = lightData.emplace_back();
			data.Position = light.Position;
			data.Direction = Vector3::Transform(Vector3::Forward, light.Rotation);
			data.SpotlightAngles.x = cos(light.PenumbraAngleDegrees * Math::DegreesToRadians / 2.0f);
			data.SpotlightAngles.y = cos(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
			data.Color = Math::EncodeRGBA(light.Colour);
			data.Intensity = light.Intensity;
			data.Range = light.Range;
			data.ShadowMapIndex = light.CastShadows && light.ShadowMaps.size() ? light.ShadowMaps[0]->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
			data.MaskTexture = light.pLightTexture ? light.pLightTexture->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
			data.MatrixIndex = light.MatrixIndex;
			data.InvShadowSize = 1.0f / light.ShadowMapSize;
			data.IsEnabled = light.Intensity > 0 ? 1 : 0;
			data.IsVolumetric = light.VolumetricLighting;
			data.CastShadows = light.CastShadows;
			data.IsPoint = light.Type == LightType::Point;
			data.IsSpot = light.Type == LightType::Spot;
			data.IsDirectional = light.Type == LightType::Directional;
		}
		pView->NumLights = (uint32)pWorld->Lights.size();

		GraphicsDevice* pDevice = context.GetParent();
		auto CopyBufferData = [&](uint32 numElements, uint32 stride, const char* pName, const void* pSource, RefCountPtr<Buffer>& pTarget)
		{
			uint32 desiredElements = Math::AlignUp(Math::Max(1u, numElements), 8u);
			if (!pTarget || desiredElements > pTarget->GetNumElements())
			{
				pTarget = pDevice->CreateBuffer(BufferDesc::CreateStructured(desiredElements, stride, BufferFlag::ShaderResource), pName);
			}
			context.WriteBuffer(pTarget, pSource, numElements * stride);
		};

		CopyBufferData((uint32)ddgiVolumes.size(), sizeof(ShaderInterop::DDGIVolume), "DDGI Volumes", ddgiVolumes.data(), pView->pDDGIVolumesBuffer);
		CopyBufferData((uint32)meshes.size(), sizeof(ShaderInterop::MeshData), "Meshes", meshes.data(), pView->pMeshBuffer);
		CopyBufferData((uint32)meshInstances.size(), sizeof(ShaderInterop::InstanceData), "Instances", meshInstances.data(), pView->pInstanceBuffer);
		CopyBufferData((uint32)materials.size(), sizeof(ShaderInterop::MaterialData), "Materials", materials.data(), pView->pMaterialBuffer);
		CopyBufferData((uint32)lightData.size(), sizeof(ShaderInterop::Light), "Lights", lightData.data(), pView->pLightBuffer);

		if (!pView->pLTCAmplitudeTexture)
		{
			pView->pLTCAmplitudeTexture = GraphicsCommon::CreateTextureFromFile(context, "Resources/Textures/ltc_amp.dds", false, "LTC Amplitude");
			pView->pLTCMatrixTexture = GraphicsCommon::CreateTextureFromFile(context, "Resources/Textures/ltc_mat.dds", false, "LTC Matrix");
		}
	}

	void DrawScene(CommandContext& context, const SceneView* pView, const VisibilityMask& visibility, Batch::Blending blendModes)
	{
		std::vector<const Batch*> meshes;
		meshes.reserve(pView->Batches.size());
		for (const Batch& b : pView->Batches)
		{
			if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.InstanceID))
			{
				meshes.push_back(&b);
			}
		}

		auto CompareSort = [pView, blendModes](const Batch* a, const Batch* b)
		{
			float aDist = Vector3::DistanceSquared(a->Bounds.Center, pView->View.Position);
			float bDist = Vector3::DistanceSquared(b->Bounds.Center, pView->View.Position);
			return EnumHasAnyFlags(blendModes, Batch::Blending::AlphaBlend) ? bDist < aDist : aDist < bDist;
		};
		std::sort(meshes.begin(), meshes.end(), CompareSort);

		for (const Batch* b : meshes)
		{
			context.SetRootConstants(0, b->InstanceID);
			if (context.GetCurrentPSO()->GetType() == PipelineStateType::Mesh)
			{
				context.DispatchMesh(ComputeUtils::GetNumThreadGroups(b->pMesh->NumMeshlets, 32));
			}
			else
			{
				context.SetIndexBuffer(b->pMesh->IndicesLocation);
				context.DrawIndexedInstanced(b->pMesh->IndicesLocation.Elements, 0, 1, 0, 0);
			}
		}
	}
}

namespace GraphicsCommon
{
	static std::array<RefCountPtr<Texture>, (uint32)DefaultTexture::MAX> DefaultTextures;

	RefCountPtr<CommandSignature> pIndirectDrawSignature;
	RefCountPtr<CommandSignature> pIndirectDispatchSignature;
	RefCountPtr<CommandSignature> pIndirectDispatchMeshSignature;

	void Create(GraphicsDevice* pDevice)
	{
		CommandContext& context = *pDevice->AllocateCommandContext();

		auto RegisterDefaultTexture = [pDevice, &context](DefaultTexture type, const char* pName, const TextureDesc& desc, uint32* pData)
		{
			RefCountPtr<Texture> pTexture = pDevice->CreateTexture(desc, pName);
			D3D12_SUBRESOURCE_DATA data;
			data.pData = pData;
			data.RowPitch = GetFormatByteSize(desc.Format, desc.Width);
			data.SlicePitch = data.RowPitch * desc.Width;
			context.InsertResourceBarrier(pTexture, D3D12_RESOURCE_STATE_COPY_DEST);
			context.FlushResourceBarriers();
			context.WriteTexture(pTexture, data, 0);
			DefaultTextures[(int)type] = pTexture;
		};

		uint32 BLACK = 0xFF000000;
		RegisterDefaultTexture(DefaultTexture::Black2D, "Default Black", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM), &BLACK);
		uint32 WHITE = 0xFFFFFFFF;
		RegisterDefaultTexture(DefaultTexture::White2D, "Default White", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM), &WHITE);
		uint32 MAGENTA = 0xFFFF00FF;
		RegisterDefaultTexture(DefaultTexture::Magenta2D, "Default Magenta", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM), &MAGENTA);
		uint32 GRAY = 0xFF808080;
		RegisterDefaultTexture(DefaultTexture::Gray2D, "Default Gray", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM), &GRAY);
		uint32 DEFAULT_NORMAL = 0xFFFF8080;
		RegisterDefaultTexture(DefaultTexture::Normal2D, "Default Normal", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM), &DEFAULT_NORMAL);
		uint32 DEFAULT_ROUGHNESS_METALNESS = 0xFFFF80FF;
		RegisterDefaultTexture(DefaultTexture::RoughnessMetalness, "Default Roughness/Metalness", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM), &DEFAULT_ROUGHNESS_METALNESS);

		uint32 BLACK_CUBE[6] = {};
		RegisterDefaultTexture(DefaultTexture::BlackCube, "Default Black Cube", TextureDesc::CreateCube(1, 1, ResourceFormat::RGBA8_UNORM), BLACK_CUBE);

		RegisterDefaultTexture(DefaultTexture::Black3D, "Default Black 3D", TextureDesc::Create3D(1, 1, 1, ResourceFormat::RGBA8_UNORM), &BLACK);

		DefaultTextures[(int)DefaultTexture::ColorNoise256] = CreateTextureFromFile(context, "Resources/Textures/Noise.png", false, "Noise");
		DefaultTextures[(int)DefaultTexture::BlueNoise512] = CreateTextureFromFile(context, "Resources/Textures/BlueNoise.dds", false, "Blue Noise");

		context.Execute(true);

		{
			CommandSignatureInitializer sigDesc;
			sigDesc.AddDispatch();
			pIndirectDispatchSignature = pDevice->CreateCommandSignature(sigDesc, "Default Indirect Dispatch");
		}
		{
			CommandSignatureInitializer sigDesc;
			sigDesc.AddDraw();
			pIndirectDrawSignature = pDevice->CreateCommandSignature(sigDesc, "Default Indirect Draw");
		}
		{
			CommandSignatureInitializer sigDesc;
			sigDesc.AddDispatchMesh();
			pIndirectDispatchMeshSignature = pDevice->CreateCommandSignature(sigDesc, "Default Indirect Dispatch Mesh");
		}
	}

	void Destroy()
	{
		for (auto& pTexture : DefaultTextures)
		{
			pTexture.Reset();
		}

		pIndirectDispatchSignature.Reset();
		pIndirectDrawSignature.Reset();
		pIndirectDispatchMeshSignature.Reset();
	}

	Texture* GetDefaultTexture(DefaultTexture type)
	{
		return DefaultTextures[(int)type];
	}

	RefCountPtr<Texture> CreateTextureFromImage(CommandContext& context, Image& image, bool sRGB, const char* pName)
	{
		GraphicsDevice* pDevice = context.GetParent();
		TextureDesc desc;
		desc.Width = image.GetWidth();
		desc.Height = image.GetHeight();
		desc.Format = Image::TextureFormatFromCompressionFormat(image.GetFormat(), sRGB);
		desc.Mips = image.GetMipLevels();
		desc.Usage = TextureFlag::ShaderResource;
		desc.Dimensions = image.IsCubemap() ? TextureDimension::TextureCube : TextureDimension::Texture2D;
		if (GetFormatInfo(desc.Format).IsBC)
		{
			desc.Width = Math::Max(desc.Width, 4u);
			desc.Height = Math::Max(desc.Height, 4u);
		}

		const Image* pImg = &image;
		std::vector<D3D12_SUBRESOURCE_DATA> subResourceData;
		int resourceOffset = 0;
		while (pImg)
		{
			subResourceData.resize(subResourceData.size() + desc.Mips);
			for (uint32 i = 0; i < desc.Mips; ++i)
			{
				D3D12_SUBRESOURCE_DATA& data = subResourceData[resourceOffset++];
				MipLevelInfo info = pImg->GetMipInfo(i);
				data.pData = pImg->GetData(i);
				data.RowPitch = info.RowSize;
				data.SlicePitch = (uint64)info.RowSize * info.Width;
			}
			pImg = pImg->GetNextImage();
		}
		RefCountPtr<Texture> pTexture = pDevice->CreateTexture(desc, pName ? pName : "");
		context.WriteTexture(pTexture, subResourceData, 0);
		return pTexture;
	}

	RefCountPtr<Texture> CreateTextureFromFile(CommandContext& context, const char* pFilePath, bool sRGB, const char* pName)
	{
		Image image;
		if (image.Load(pFilePath))
		{
			return CreateTextureFromImage(context, image, sRGB, pName);
		}
		return nullptr;
	}
}

Vector2i SceneView::GetDimensions() const
{
	return Vector2i((uint32)View.Viewport.GetWidth(), (uint32)View.Viewport.GetHeight());
}
