#include "stdafx.h"
#include "SceneView.h"
#include "Core/Utils.h"
#include "RHI/Buffer.h"
#include "RHI/PipelineState.h"
#include "RHI/Texture.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "Mesh.h"
#include "Light.h"
#include "Core/ConsoleVariables.h"
#include "Content/Image.h"
#include "Core/Profiler.h"

namespace Tweakables
{
	extern ConsoleVariable<int> gSSRSamples;
	extern ConsoleVariable<bool> gEnableDDGI;
}

namespace Renderer
{
	ShaderInterop::ViewUniforms GetViewUniforms(const SceneView* pView, const ViewTransform* pViewTransform, Texture* pTarget)
	{
		ShaderInterop::ViewUniforms parameters;

		parameters.View = pViewTransform->View;
		parameters.ViewInverse = pViewTransform->ViewInverse;
		parameters.Projection = pViewTransform->Projection;
		parameters.ProjectionInverse = pViewTransform->ProjectionInverse;
		parameters.ViewProjection = pViewTransform->ViewProjection;
		parameters.ViewProjectionPrev = pViewTransform->ViewProjectionPrev;
		parameters.ViewProjectionInverse = pViewTransform->ProjectionInverse * pViewTransform->ViewInverse;

		Matrix reprojectionMatrix = parameters.ViewProjectionInverse * parameters.ViewProjectionPrev;
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

		parameters.ViewLocation = pViewTransform->Position;
		parameters.ViewLocationPrev = pViewTransform->PositionPrev;

		parameters.ViewportDimensions = Vector2(pViewTransform->Viewport.GetWidth(), pViewTransform->Viewport.GetHeight());
		parameters.ViewportDimensionsInv = Vector2(1.0f / pViewTransform->Viewport.GetWidth(), 1.0f / pViewTransform->Viewport.GetHeight());
		parameters.ViewJitter = pViewTransform->Jitter;
		parameters.ViewJitterPrev = pViewTransform->JitterPrev;
		parameters.NearZ = pViewTransform->NearPlane;
		parameters.FarZ = pViewTransform->FarPlane;
		parameters.FoV = pViewTransform->FoV;

		if (pTarget)
		{
			parameters.TargetDimensions = Vector2((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
			parameters.TargetDimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
		}
		parameters.FrameIndex = pView->FrameIndex;
		parameters.NumInstances = (uint32)pView->Batches.size();
		parameters.SsrSamples = Tweakables::gSSRSamples.Get();
		parameters.LightCount = pView->LightBuffer.Count;
		parameters.CascadeDepths = pView->ShadowCascadeDepths;
		parameters.NumCascades = pView->NumShadowCascades;

		parameters.TLASIndex = pView->AccelerationStructure.GetSRV() ? pView->AccelerationStructure.GetSRV()->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
		parameters.MeshesIndex = pView->MeshBuffer.pBuffer->GetSRVIndex();
		parameters.MaterialsIndex = pView->MaterialBuffer.pBuffer->GetSRVIndex();
		parameters.InstancesIndex = pView->InstanceBuffer.pBuffer->GetSRVIndex();
		parameters.LightsIndex = pView->LightBuffer.pBuffer->GetSRVIndex();
		parameters.LightMatricesIndex = pView->LightMatricesBuffer.pBuffer->GetSRVIndex();
		parameters.SkyIndex = pView->pSky ? pView->pSky->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
		parameters.DDGIVolumesIndex = pView->DDGIVolumesBuffer.pBuffer->GetSRVIndex();
		parameters.NumDDGIVolumes = pView->DDGIVolumesBuffer.Count;

		parameters.FontDataIndex = pView->DebugRenderData.FontDataSRV;
		parameters.DebugRenderDataIndex = pView->DebugRenderData.RenderDataUAV;
		parameters.FontSize = pView->DebugRenderData.FontSize;

		return parameters;
	}

	ShaderInterop::ViewUniforms GetViewUniforms(const SceneView* pView, Texture* pTarget)
	{
		return GetViewUniforms(pView, &pView->MainView, pTarget);
	}

	void UploadSceneData(CommandContext& context, SceneView* pView, const World* pWorld)
	{
		PROFILE_CPU_SCOPE();
		PROFILE_GPU_SCOPE(context.GetCommandList());

		GraphicsDevice* pDevice = context.GetParent();
		auto CopyBufferData = [&](uint32 numElements, uint32 stride, const char* pName, const void* pSource, SceneView::SceneBuffer& target)
			{
				uint32 desiredElements = Math::AlignUp(Math::Max(1u, numElements), 8u);
				if (!target.pBuffer || desiredElements > target.pBuffer->GetNumElements())
					target.pBuffer = pDevice->CreateBuffer(BufferDesc::CreateStructured(desiredElements, stride, BufferFlag::ShaderResource), pName);
				ScratchAllocation alloc = context.AllocateScratch(numElements * stride);
				memcpy(alloc.pMappedMemory, pSource, numElements * stride);
				context.CopyBuffer(alloc.pBackingResource, target.pBuffer, alloc.Size, alloc.Offset, 0);
				target.Count = numElements;
			};


		std::vector<Batch> sceneBatches;
		uint32 instanceID = 0;

		// Instances
		{
			std::vector<ShaderInterop::InstanceData> meshInstances;

			auto view = pWorld->Registry.view<Transform, Model>();
			view.each([&](const Transform& transform, const Model& model)
				{
					const Mesh& mesh = pWorld->Meshes[model.MeshIndex];
					const Material& meshMaterial = pWorld->Materials[mesh.MaterialId];

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
					batch.InstanceID		= instanceID;
					batch.pMesh				= &mesh;
					batch.BlendMode			= GetBlendMode(meshMaterial.AlphaMode);
					batch.WorldMatrix		= transform.World;
					batch.Radius			= Vector3(batch.Bounds.Extents).Length();
					mesh.Bounds.Transform(batch.Bounds, batch.WorldMatrix);

					ShaderInterop::InstanceData& meshInstance = meshInstances.emplace_back();
					meshInstance.ID						= instanceID;
					meshInstance.MeshIndex				= model.MeshIndex;
					meshInstance.MaterialIndex			= mesh.MaterialId;
					meshInstance.LocalToWorld			= transform.World;
					meshInstance.LocalToWorldPrev		= transform.World; //#todo
					meshInstance.LocalBoundsOrigin		= mesh.Bounds.Center;
					meshInstance.LocalBoundsExtents		= mesh.Bounds.Extents;

					++instanceID;
				});
			CopyBufferData((uint32)meshInstances.size(), sizeof(ShaderInterop::InstanceData), "Instances", meshInstances.data(), pView->InstanceBuffer);
		}

		// Meshes
		{
			std::vector<ShaderInterop::MeshData> meshes;
			meshes.reserve(pWorld->Meshes.size());
			for(const Mesh& mesh : pWorld->Meshes)
			{
				ShaderInterop::MeshData& meshData = meshes.emplace_back();
				meshData.BufferIndex			= mesh.pBuffer->GetSRVIndex();
				meshData.IndexByteSize			= mesh.IndicesLocation.Stride();
				meshData.IndicesOffset			= (uint32)mesh.IndicesLocation.OffsetFromStart;
				meshData.PositionsOffset		= (uint32)mesh.PositionStreamLocation.OffsetFromStart;
				meshData.NormalsOffset			= (uint32)mesh.NormalStreamLocation.OffsetFromStart;
				meshData.ColorsOffset			= (uint32)mesh.ColorsStreamLocation.OffsetFromStart;
				meshData.UVsOffset				= (uint32)mesh.UVStreamLocation.OffsetFromStart;

				meshData.MeshletOffset			= mesh.MeshletsLocation;
				meshData.MeshletVertexOffset	= mesh.MeshletVerticesLocation;
				meshData.MeshletTriangleOffset	= mesh.MeshletTrianglesLocation;
				meshData.MeshletBoundsOffset	= mesh.MeshletBoundsLocation;
				meshData.MeshletCount			= mesh.NumMeshlets;
			}
			CopyBufferData((uint32)meshes.size(), sizeof(ShaderInterop::MeshData), "Meshes", meshes.data(), pView->MeshBuffer);
		}

		// Materials
		{
			std::vector<ShaderInterop::MaterialData> materials;
			materials.reserve(pWorld->Materials.size());
			for (const Material& material : pWorld->Materials)
			{
				ShaderInterop::MaterialData& materialData = materials.emplace_back();
				materialData.Diffuse			= material.pDiffuseTexture ? material.pDiffuseTexture->GetSRVIndex() : -1;
				materialData.Normal				= material.pNormalTexture ? material.pNormalTexture->GetSRVIndex() : -1;
				materialData.RoughnessMetalness = material.pRoughnessMetalnessTexture ? material.pRoughnessMetalnessTexture->GetSRVIndex() : -1;
				materialData.Emissive			= material.pEmissiveTexture ? material.pEmissiveTexture->GetSRVIndex() : -1;
				materialData.BaseColorFactor	= material.BaseColorFactor;
				materialData.MetalnessFactor	= material.MetalnessFactor;
				materialData.RoughnessFactor	= material.RoughnessFactor;
				materialData.EmissiveFactor		= material.EmissiveFactor;
				materialData.AlphaCutoff		= material.AlphaCutoff;
				switch (material.AlphaMode)
				{
				case MaterialAlphaMode::Blend:	materialData.RasterBin = 0xFFFFFFFF;	break;
				case MaterialAlphaMode::Opaque: materialData.RasterBin = 0;				break;
				case MaterialAlphaMode::Masked: materialData.RasterBin = 1;				break;
				}
			}
			CopyBufferData((uint32)materials.size(), sizeof(ShaderInterop::MaterialData), "Materials", materials.data(), pView->MaterialBuffer);
		}

		// DDGI
		if (Tweakables::gEnableDDGI)
		{
			std::vector<ShaderInterop::DDGIVolume> ddgiVolumes;
			auto ddgi_view = pWorld->Registry.view<Transform, DDGIVolume>();
			ddgi_view.each([&](const Transform& transform, const DDGIVolume& volume)
				{
					ShaderInterop::DDGIVolume& ddgi = ddgiVolumes.emplace_back();
					ddgi.BoundsMin				= transform.Position - volume.Extents;
					ddgi.ProbeSize				= 2 * volume.Extents / (Vector3((float)volume.NumProbes.x, (float)volume.NumProbes.y, (float)volume.NumProbes.z) - Vector3::One);
					ddgi.ProbeVolumeDimensions	= Vector3u(volume.NumProbes.x, volume.NumProbes.y, volume.NumProbes.z);
					ddgi.IrradianceIndex		= volume.pIrradianceHistory ? volume.pIrradianceHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.DepthIndex				= volume.pDepthHistory ? volume.pDepthHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.ProbeOffsetIndex		= volume.pProbeOffset ? volume.pProbeOffset->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.ProbeStatesIndex		= volume.pProbeStates ? volume.pProbeStates->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					ddgi.NumRaysPerProbe		= volume.NumRays;
					ddgi.MaxRaysPerProbe		= volume.MaxNumRays;
				});
			CopyBufferData((uint32)ddgiVolumes.size(), sizeof(ShaderInterop::DDGIVolume), "DDGI Volumes", ddgiVolumes.data(), pView->DDGIVolumesBuffer);
		}

		// Lights
		{
			std::vector<ShaderInterop::Light> lightData;
			auto light_view = pWorld->Registry.view<const Transform, const Light>();
			light_view.each([&](const Transform& transform, const Light& light)
				{
					ShaderInterop::Light& data = lightData.emplace_back();
					data.Position			= transform.Position;
					data.Direction			= Vector3::Transform(Vector3::Forward, transform.Rotation);
					data.SpotlightAngles.x	= cos(light.PenumbraAngleDegrees * Math::DegreesToRadians / 2.0f);
					data.SpotlightAngles.y	= cos(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
					data.Color				= Math::Pack_RGBA8_UNORM(light.Colour);
					data.Intensity			= light.Intensity;
					data.Range				= light.Range;
					data.ShadowMapIndex		= light.CastShadows && light.ShadowMaps.size() ? light.ShadowMaps[0]->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					data.MaskTexture		= light.pLightTexture ? light.pLightTexture->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
					data.MatrixIndex		= light.MatrixIndex;
					data.InvShadowSize		= 1.0f / light.ShadowMapSize;
					data.IsEnabled			= light.Intensity > 0 ? 1 : 0;
					data.IsVolumetric		= light.VolumetricLighting;
					data.CastShadows		= light.ShadowMaps.size() && light.CastShadows;
					data.IsPoint			= light.Type == LightType::Point;
					data.IsSpot				= light.Type == LightType::Spot;
					data.IsDirectional		= light.Type == LightType::Directional;
				});
			CopyBufferData((uint32)lightData.size(), sizeof(ShaderInterop::Light), "Lights", lightData.data(), pView->LightBuffer);
		}
		{
			std::vector<Matrix> lightMatrices(pView->ShadowViews.size());
			for (uint32 i = 0; i < pView->ShadowViews.size(); ++i)
				lightMatrices[i] = pView->ShadowViews[i].View.ViewProjection;
			CopyBufferData((uint32)lightMatrices.size(), sizeof(Matrix), "Light Matrices", lightMatrices.data(), pView->LightMatricesBuffer);
		}

		sceneBatches.swap(pView->Batches);
	}

	void DrawScene(CommandContext& context, const SceneView* pView, Batch::Blending blendModes)
	{
		DrawScene(context, pView->Batches, pView->VisibilityMask, blendModes);
	}

	void DrawScene(CommandContext& context, Span<Batch> batches, const VisibilityMask& visibility, Batch::Blending blendModes)
	{
		PROFILE_CPU_SCOPE();
		PROFILE_GPU_SCOPE(context.GetCommandList());
		check(batches.GetSize() <= visibility.Size());
		for (const Batch& b : batches)
		{
			if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.InstanceID))
			{
				PROFILE_CPU_SCOPE("Draw Primitive");
				PROFILE_GPU_SCOPE(context.GetCommandList(), "Draw Pritimive");
				context.BindRootCBV(0, b.InstanceID);
				context.DispatchMesh(Math::DivideAndRoundUp(b.pMesh->NumMeshlets, 32));
			}
		}
	}
}

namespace GraphicsCommon
{
	static std::array<Ref<Texture>, (uint32)DefaultTexture::MAX> DefaultTextures;

	Ref<CommandSignature> pIndirectDrawSignature;
	Ref<CommandSignature> pIndirectDispatchSignature;
	Ref<CommandSignature> pIndirectDispatchMeshSignature;
	Ref<RootSignature> pCommonRS;

	void Create(GraphicsDevice* pDevice)
	{
		auto RegisterDefaultTexture = [pDevice](DefaultTexture type, const char* pName, const TextureDesc& desc, const uint32* pData)
		{
			D3D12_SUBRESOURCE_DATA data;
			data.pData = pData;
			data.RowPitch = RHI::GetRowPitch(desc.Format, desc.Width);
			data.SlicePitch = RHI::GetSlicePitch(desc.Format, desc.Width, desc.Height);
			Ref<Texture> pTexture = pDevice->CreateTexture(desc, pName, data);
			DefaultTextures[(int)type] = pTexture;
		};

		uint32 BLACK =							Math::Pack_RGBA8_UNORM(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		uint32 WHITE =							Math::Pack_RGBA8_UNORM(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		uint32 MAGENTA =						Math::Pack_RGBA8_UNORM(Vector4(1.0f, 0.0f, 1.0f, 1.0f));
		uint32 GRAY =							Math::Pack_RGBA8_UNORM(Vector4(0.5f, 0.5f, 0.5f, 1.0f));
		uint32 DEFAULT_NORMAL =					Math::Pack_RGBA8_UNORM(Vector4(0.5f, 0.5f, 1.0f, 1.0f));
		uint32 DEFAULT_ROUGHNESS_METALNESS =	Math::Pack_RGBA8_UNORM(Vector4(0.5f, 0.0f, 1.0f, 1.0f));

		const TextureFlag textureFlags = TextureFlag::ShaderResource;
		RegisterDefaultTexture(DefaultTexture::Black2D,				"Default Black",				TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &BLACK);
		RegisterDefaultTexture(DefaultTexture::White2D,				"Default White",				TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &WHITE);
		RegisterDefaultTexture(DefaultTexture::Magenta2D,			"Default Magenta",				TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &MAGENTA);
		RegisterDefaultTexture(DefaultTexture::Gray2D,				"Default Gray",					TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &GRAY);
		RegisterDefaultTexture(DefaultTexture::Normal2D,			"Default Normal",				TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &DEFAULT_NORMAL);
		RegisterDefaultTexture(DefaultTexture::RoughnessMetalness,	"Default Roughness/Metalness",	TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &DEFAULT_ROUGHNESS_METALNESS);

		uint32 BLACK_CUBE[6] = {};
		RegisterDefaultTexture(DefaultTexture::BlackCube,			"Default Black Cube",			TextureDesc::CreateCube(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), BLACK_CUBE);

		RegisterDefaultTexture(DefaultTexture::Black3D,				"Default Black 3D",				TextureDesc::Create3D(1, 1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &BLACK);

		constexpr uint32 checkerPixels[] =
		{
			0xFFFFFFFF, 0xFF000000,
			0xFF000000, 0xFFFFFFFF
		};
		RegisterDefaultTexture(DefaultTexture::CheckerPattern, "Checker Pattern", TextureDesc::Create2D(2, 2, ResourceFormat::RGBA8_UNORM, 1, textureFlags), checkerPixels);

		DefaultTextures[(int)DefaultTexture::ColorNoise256] = CreateTextureFromFile(pDevice, "Resources/Textures/Noise.png", false, "Noise");
		DefaultTextures[(int)DefaultTexture::BlueNoise512] = CreateTextureFromFile(pDevice, "Resources/Textures/BlueNoise.dds", false, "Blue Noise");

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

		// Common Root Signature - Make it 12 DWORDs as is often recommended by IHVs
		pCommonRS = new RootSignature(pDevice);
		pCommonRS->AddRootConstants(0, 8);
		pCommonRS->AddRootCBV(100);
		pCommonRS->AddDescriptorTable(0, 16, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
		pCommonRS->AddDescriptorTable(0, 64, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
		pCommonRS->Finalize("Common");
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
		pCommonRS.Reset();
	}

	Texture* GetDefaultTexture(DefaultTexture type)
	{
		return DefaultTextures[(int)type];
	}

	Ref<Texture> CreateTextureFromImage(GraphicsDevice* pDevice, const Image& image, bool sRGB, const char* pName)
	{
		TextureDesc desc;
		desc.Width = image.GetWidth();
		desc.Height = image.GetHeight();
		desc.Format = image.GetFormat();
		desc.Mips = image.GetMipLevels();
		desc.Flags = TextureFlag::ShaderResource;
		if (sRGB)
		{
			desc.Flags |= TextureFlag::sRGB;
		}
		desc.Type = image.IsCubemap() ? TextureType::TextureCube : TextureType::Texture2D;
		if (RHI::GetFormatInfo(desc.Format).IsBC)
		{
			desc.Width = Math::Max(desc.Width, 4u);
			desc.Height = Math::Max(desc.Height, 4u);
		}

		std::vector<D3D12_SUBRESOURCE_DATA> subResourceData;
		const Image* pImg = &image;
		while (pImg)
		{
			for (uint32 i = 0; i < desc.Mips; ++i)
			{
				D3D12_SUBRESOURCE_DATA& data = subResourceData.emplace_back();
				data.pData = pImg->GetData(i);
				data.RowPitch = RHI::GetRowPitch(image.GetFormat(), desc.Width, i);
				data.SlicePitch = RHI::GetSlicePitch(image.GetFormat(), desc.Width, desc.Height, i);
			}
			pImg = pImg->GetNextImage();
		}
		Ref<Texture> pTexture = pDevice->CreateTexture(desc, pName ? pName : "", subResourceData);
		return pTexture;
	}

	Ref<Texture> CreateTextureFromFile(GraphicsDevice* pDevice, const char* pFilePath, bool sRGB, const char* pName)
	{
		Image image;
		if (image.Load(pFilePath))
		{
			return CreateTextureFromImage(pDevice, image, sRGB, pName);
		}
		return nullptr;
	}
}
