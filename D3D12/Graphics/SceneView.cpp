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
		parameters.ViewProjectionInverse = view.ProjectionInverse * view.ViewInverse;

		Matrix reprojectionMatrix = parameters.ViewProjectionInverse * view.PreviousViewProjection;
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

		parameters.PreviousViewProjection = view.PreviousViewProjection;
		parameters.ReprojectionMatrix = premult * reprojectionMatrix * postmult;
		parameters.ViewLocation = view.Position;

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

		parameters.ViewJitter.x = view.PreviousJitter.x - view.Jitter.x;
		parameters.ViewJitter.y = -(view.PreviousJitter.y - view.Jitter.y);
		parameters.NearZ = view.NearPlane;
		parameters.FarZ = view.FarPlane;
		parameters.FoV = view.FoV;


		parameters.FrameIndex = pView->FrameIndex;
		parameters.SsrSamples = Tweakables::g_SsrSamples.Get();
		parameters.LightCount = pView->pLightBuffer->GetNumElements();

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
		parameters.MeshInstancesIndex = pView->pMeshInstanceBuffer->GetSRVIndex();
		parameters.TransformsIndex = pView->pTransformsBuffer->GetSRVIndex();
		parameters.LightsIndex = pView->pLightBuffer->GetSRVIndex();
		parameters.SkyIndex = pView->pSky ? pView->pSky->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
		parameters.DDGIVolumesIndex = pView->pDDGIVolumesBuffer->GetSRVIndex();
		parameters.NumDDGIVolumes = pView->NumDDGIVolumes;
		return parameters;
	}

	void UploadSceneData(CommandContext& context, SceneView* pView, World* pWorld)
	{
		std::vector<ShaderInterop::MaterialData> materials;
		std::vector<ShaderInterop::MeshData> meshes;
		std::vector<ShaderInterop::MeshInstance> meshInstances;
		std::vector<Batch> sceneBatches;
		std::vector<Matrix> transforms;

		pView->AccelerationStructure.Reset();

		for (const auto& pMesh : pWorld->Meshes)
		{
			for (const SubMeshInstance& node : pMesh->GetMeshInstances())
			{
				SubMesh& parentMesh = pMesh->GetMesh(node.MeshIndex);
				const Material& meshMaterial = pMesh->GetMaterial(parentMesh.MaterialId);
				ShaderInterop::MeshInstance meshInstance;
				meshInstance.Mesh = (uint32)meshes.size() + node.MeshIndex;
				meshInstance.Material = (uint32)materials.size() + parentMesh.MaterialId;
				meshInstance.World = (uint32)transforms.size();
				meshInstances.push_back(meshInstance);

				transforms.push_back(node.Transform);

				auto GetBlendMode = [](MaterialAlphaMode mode) {
					switch (mode)
					{
					case MaterialAlphaMode::Blend: return Batch::Blending::AlphaBlend;
					case MaterialAlphaMode::Opaque: return Batch::Blending::Opaque;
					case MaterialAlphaMode::Masked: return Batch::Blending::AlphaMask;
					}
					return Batch::Blending::Opaque;
				};

				Batch batch;
				batch.InstanceData = meshInstance;
				batch.pMesh = &parentMesh;
				batch.BlendMode = GetBlendMode(meshMaterial.AlphaMode);
				batch.WorldMatrix = node.Transform;
				parentMesh.Bounds.Transform(batch.Bounds, batch.WorldMatrix);
				batch.Radius = Vector3(batch.Bounds.Extents).Length();
				sceneBatches.push_back(batch);

				pView->AccelerationStructure.AddInstance(meshInstance.World, &parentMesh, batch.WorldMatrix);
			}

			for (const SubMesh& subMesh : pMesh->GetMeshes())
			{
				ShaderInterop::MeshData mesh;
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
				meshes.push_back(mesh);
			}

			for (const Material& material : pMesh->GetMaterials())
			{
				ShaderInterop::MaterialData materialData;
				materialData.Diffuse = material.pDiffuseTexture ? material.pDiffuseTexture->GetSRVIndex() : -1;
				materialData.Normal = material.pNormalTexture ? material.pNormalTexture->GetSRVIndex() : -1;
				materialData.RoughnessMetalness = material.pRoughnessMetalnessTexture ? material.pRoughnessMetalnessTexture->GetSRVIndex() : -1;
				materialData.Emissive = material.pEmissiveTexture ? material.pEmissiveTexture->GetSRVIndex() : -1;
				materialData.BaseColorFactor = material.BaseColorFactor;
				materialData.MetalnessFactor = material.MetalnessFactor;
				materialData.RoughnessFactor = material.RoughnessFactor;
				materialData.EmissiveFactor = material.EmissiveFactor;
				materialData.AlphaCutoff = material.AlphaCutoff;
				materials.push_back(materialData);
			}
		}
		sceneBatches.swap(pView->Batches);

		std::vector<ShaderInterop::DDGIVolume> ddgiVolumes;
		if (Tweakables::g_EnableDDGI)
		{
			for (DDGIVolume& ddgiVolume : pWorld->DDGIVolumes)
			{
				ShaderInterop::DDGIVolume ddgi{};
				ddgi.BoundsMin = ddgiVolume.Origin - ddgiVolume.Extents;
				ddgi.ProbeSize = 2 * ddgiVolume.Extents / (Vector3((float)ddgiVolume.NumProbes.x, (float)ddgiVolume.NumProbes.y, (float)ddgiVolume.NumProbes.z) - Vector3::One);
				ddgi.ProbeVolumeDimensions = TIntVector3<uint32>(ddgiVolume.NumProbes.x, ddgiVolume.NumProbes.y, ddgiVolume.NumProbes.z);
				ddgi.IrradianceIndex = ddgiVolume.pIrradianceHistory ? ddgiVolume.pIrradianceHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.DepthIndex = ddgiVolume.pDepthHistory ? ddgiVolume.pDepthHistory->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.ProbeOffsetIndex = ddgiVolume.pProbeOffset ? ddgiVolume.pProbeOffset->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.ProbeStatesIndex = ddgiVolume.pProbeStates ? ddgiVolume.pProbeStates->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
				ddgi.NumRaysPerProbe = ddgiVolume.NumRays;
				ddgi.MaxRaysPerProbe = ddgiVolume.MaxNumRays;
				ddgiVolumes.push_back(ddgi);
			}
		}
		pView->NumDDGIVolumes = (uint32)ddgiVolumes.size();

		std::vector<ShaderInterop::Light> lightData;
		Utils::Transform(pWorld->Lights, lightData, [](const Light& light) { return light.GetData(); });

		GraphicsDevice* pDevice = context.GetParent();
		auto CopyBufferData = [&](size_t numElements, uint32 stride, const char* pName, const void* pSource, RefCountPtr<Buffer>& pTarget)
		{
			if (!pTarget || numElements > pTarget->GetNumElements())
			{
				pTarget = pDevice->CreateBuffer(BufferDesc::CreateStructured(Math::Max(1u, (uint32)numElements), stride, BufferFlag::ShaderResource), pName);
			}
			context.WriteBuffer(pTarget, pSource, numElements * stride);
		};

		CopyBufferData(ddgiVolumes.size(), sizeof(ShaderInterop::DDGIVolume), "DDGI Volumes", ddgiVolumes.data(), pView->pDDGIVolumesBuffer);
		CopyBufferData(meshes.size(), sizeof(ShaderInterop::MeshData), "Meshes", meshes.data(), pView->pMeshBuffer);
		CopyBufferData(meshInstances.size(), sizeof(ShaderInterop::MeshInstance), "Meshes Instances", meshInstances.data(), pView->pMeshInstanceBuffer);
		CopyBufferData(materials.size(), sizeof(ShaderInterop::MaterialData), "Materials", materials.data(), pView->pMaterialBuffer);
		CopyBufferData(transforms.size(), sizeof(Matrix), "Transforms", transforms.data(), pView->pTransformsBuffer);
		CopyBufferData(lightData.size(), sizeof(ShaderInterop::Light), "Lights", lightData.data(), pView->pLightBuffer);

		pView->AccelerationStructure.Build(context);
	}

	void DrawScene(CommandContext& context, const SceneView* pView, const VisibilityMask& visibility, Batch::Blending blendModes)
	{
		std::vector<const Batch*> meshes;
		for (const Batch& b : pView->Batches)
		{
			if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.InstanceData.World))
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
			context.SetRootConstants(0, b->InstanceData);
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
			data.RowPitch = D3D::GetFormatRowDataSize(desc.Format, desc.Width);
			data.SlicePitch = data.RowPitch * desc.Width;
			context.InsertResourceBarrier(pTexture, D3D12_RESOURCE_STATE_COPY_DEST);
			context.FlushResourceBarriers();
			context.WriteTexture(pTexture, data, 0);
			DefaultTextures[(int)type] = pTexture;
		};

		uint32 BLACK = 0xFF000000;
		RegisterDefaultTexture(DefaultTexture::Black2D, "Default Black", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &BLACK);
		uint32 WHITE = 0xFFFFFFFF;
		RegisterDefaultTexture(DefaultTexture::White2D, "Default White", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &WHITE);
		uint32 MAGENTA = 0xFFFF00FF;
		RegisterDefaultTexture(DefaultTexture::Magenta2D, "Default Magenta", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &MAGENTA);
		uint32 GRAY = 0xFF808080;
		RegisterDefaultTexture(DefaultTexture::Gray2D, "Default Gray", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &GRAY);
		uint32 DEFAULT_NORMAL = 0xFFFF8080;
		RegisterDefaultTexture(DefaultTexture::Normal2D, "Default Normal", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_NORMAL);
		uint32 DEFAULT_ROUGHNESS_METALNESS = 0xFFFF80FF;
		RegisterDefaultTexture(DefaultTexture::RoughnessMetalness, "Default Roughness/Metalness", TextureDesc::Create2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &DEFAULT_ROUGHNESS_METALNESS);

		uint32 BLACK_CUBE[6] = {};
		RegisterDefaultTexture(DefaultTexture::BlackCube, "Default Black Cube", TextureDesc::CreateCube(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), BLACK_CUBE);

		RegisterDefaultTexture(DefaultTexture::Black3D, "Default Black 3D", TextureDesc::Create3D(1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM), &BLACK);

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
		desc.Format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(image.GetFormat(), sRGB);
		desc.Mips = image.GetMipLevels();
		desc.Usage = TextureFlag::ShaderResource;
		desc.Dimensions = image.IsCubemap() ? TextureDimension::TextureCube : TextureDimension::Texture2D;
		if (D3D::IsBlockCompressFormat(desc.Format))
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

IntVector2 SceneView::GetDimensions() const
{
	return IntVector2((uint32)View.Viewport.GetWidth(), (uint32)View.Viewport.GetHeight());
}
