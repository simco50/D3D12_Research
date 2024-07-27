#include "stdafx.h"
#include "SceneLoader.h"
#include "RHI/CommandContext.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/Buffer.h"
#include "RHI/RingBufferAllocator.h"
#include "Core/Paths.h"
#include "Core/Image.h"
#include "Core/Utils.h"
#include "ShaderInterop.h"
#include "Renderer/Renderer.h"
#include "Renderer/Mesh.h"
#include "Renderer/Light.h"
#include "Core/Stream.h"
#include "Scene/World.h"

#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#pragma warning(pop)

#include <meshoptimizer.h>
#include <LDraw.h>



struct MeshData
{
	Array<Vector3> PositionsStream;
	Array<Vector3> NormalsStream;
	Array<Vector4> TangentsStream;
	Array<Vector2> UVsStream;
	Array<Vector4> ColorsStream;
	Array<Vector4i> JointsStream;
	Array<Vector4> WeightsStream;
	Array<uint32> Indices;

	Array<ShaderInterop::Meshlet> Meshlets;
	Array<uint32> MeshletVertices;
	Array<ShaderInterop::Meshlet::Triangle> MeshletTriangles;
	Array<ShaderInterop::Meshlet::Bounds> MeshletBounds;
};


static void BuildMeshData(MeshData& meshData)
{
	meshopt_optimizeVertexCache(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());
	meshopt_optimizeOverdraw(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), 1.05f);

	Array<uint32> remap(meshData.PositionsStream.size());
	meshopt_optimizeVertexFetchRemap(&remap[0], meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());
	meshopt_remapIndexBuffer(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &remap[0]);
	meshopt_remapVertexBuffer(meshData.PositionsStream.data(), meshData.PositionsStream.data(), meshData.PositionsStream.size(), sizeof(Vector3), &remap[0]);
	meshopt_remapVertexBuffer(meshData.NormalsStream.data(), meshData.NormalsStream.data(), meshData.NormalsStream.size(), sizeof(Vector3), &remap[0]);
	meshopt_remapVertexBuffer(meshData.TangentsStream.data(), meshData.TangentsStream.data(), meshData.TangentsStream.size(), sizeof(Vector4), &remap[0]);
	meshopt_remapVertexBuffer(meshData.UVsStream.data(), meshData.UVsStream.data(), meshData.UVsStream.size(), sizeof(Vector2), &remap[0]);
	meshopt_remapVertexBuffer(meshData.JointsStream.data(), meshData.JointsStream.data(), meshData.JointsStream.size(), sizeof(Vector4i), &remap[0]);
	meshopt_remapVertexBuffer(meshData.WeightsStream.data(), meshData.WeightsStream.data(), meshData.WeightsStream.size(), sizeof(Vector4), &remap[0]);
	meshopt_remapVertexBuffer(meshData.ColorsStream.data(), meshData.ColorsStream.data(), meshData.ColorsStream.size(), sizeof(Vector4), &remap[0]);

	// Meshlet generation
	const size_t maxVertices = ShaderInterop::MESHLET_MAX_VERTICES;
	const size_t maxTriangles = ShaderInterop::MESHLET_MAX_TRIANGLES;
	const size_t maxMeshlets = meshopt_buildMeshletsBound(meshData.Indices.size(), maxVertices, maxTriangles);

	meshData.Meshlets.resize(maxMeshlets);
	meshData.MeshletVertices.resize(maxMeshlets * maxVertices);

	Array<unsigned char> meshletTriangles(maxMeshlets * maxTriangles * 3);
	Array<meshopt_Meshlet> meshlets(maxMeshlets);

	size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshData.MeshletVertices.data(), meshletTriangles.data(),
		meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), maxVertices, maxTriangles, 0);

	// Trimming
	const meshopt_Meshlet& last = meshlets[meshlet_count - 1];
	meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
	meshlets.resize(meshlet_count);

	meshData.MeshletVertices.resize(last.vertex_offset + last.vertex_count);
	meshData.Meshlets.resize(meshlet_count);
	meshData.MeshletBounds.resize(meshlet_count);
	meshData.MeshletTriangles.resize(meshletTriangles.size() / 3);

	uint32 triangleOffset = 0;
	for (size_t i = 0; i < meshlet_count; ++i)
	{
		const meshopt_Meshlet& meshlet = meshlets[i];

		Vector3 min = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
		Vector3 max = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (uint32 k = 0; k < meshlet.triangle_count * 3; ++k)
		{
			uint32 idx = meshData.MeshletVertices[meshlet.vertex_offset + meshletTriangles[meshlet.triangle_offset + k]];
			const Vector3& p = meshData.PositionsStream[idx];
			max = Vector3::Max(max, p);
			min = Vector3::Min(min, p);
		}
		ShaderInterop::Meshlet::Bounds& outBounds = meshData.MeshletBounds[i];
		outBounds.LocalCenter = (max + min) / 2;
		outBounds.LocalExtents = (max - min) / 2;

		// Encode triangles and get rid of 4 byte padding
		unsigned char* pSourceTriangles = meshletTriangles.data() + meshlet.triangle_offset;
		for (uint32 triIdx = 0; triIdx < meshlet.triangle_count; ++triIdx)
		{
			ShaderInterop::Meshlet::Triangle& tri = meshData.MeshletTriangles[triIdx + triangleOffset];
			tri.V0 = *pSourceTriangles++;
			tri.V1 = *pSourceTriangles++;
			tri.V2 = *pSourceTriangles++;
		}

		ShaderInterop::Meshlet& outMeshlet = meshData.Meshlets[i];
		outMeshlet.TriangleCount = meshlet.triangle_count;
		outMeshlet.TriangleOffset = triangleOffset;
		outMeshlet.VertexCount = meshlet.vertex_count;
		outMeshlet.VertexOffset = meshlet.vertex_offset;
		triangleOffset += meshlet.triangle_count;
	}
	meshData.MeshletTriangles.resize(triangleOffset);
}


static Mesh CreateMesh(GraphicsDevice* pDevice, MeshData& meshData)
{
	BuildMeshData(meshData);

	bool hasAnim = !meshData.WeightsStream.empty();

	constexpr uint64 bufferAlignment = 16;
	uint64 bufferSize = 0;
	using TVertexPositionStream = Vector3;
	using TVertexNormalStream = Vector2u;
	using TVertexColorStream = uint32;
	using TVertexUVStream = uint32;
	using TWeightsStream = Vector2u;
	struct TJointsStream { uint16 Joints[4]; };

	bufferSize += Math::AlignUp<uint64>(meshData.Indices.size()				* sizeof(uint32),										bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.PositionsStream.size()		* sizeof(TVertexPositionStream),						bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.UVsStream.size()			* sizeof(TVertexUVStream),								bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.NormalsStream.size()		* sizeof(TVertexNormalStream),							bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.ColorsStream.size()		* sizeof(TVertexColorStream),							bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.JointsStream.size()		* sizeof(TJointsStream),								bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.WeightsStream.size()		* sizeof(TWeightsStream),								bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.Meshlets.size()			* sizeof(ShaderInterop::Meshlet),						bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.MeshletVertices.size()		* sizeof(uint32),										bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.MeshletTriangles.size()	* sizeof(ShaderInterop::Meshlet::Triangle),				bufferAlignment);
	bufferSize += Math::AlignUp<uint64>(meshData.MeshletBounds.size()		* sizeof(ShaderInterop::Meshlet::Bounds),				bufferAlignment);

	if (hasAnim)
	{
		bufferSize += Math::AlignUp<uint64>(meshData.PositionsStream.size() * sizeof(TVertexPositionStream), bufferAlignment);
		bufferSize += Math::AlignUp<uint64>(meshData.NormalsStream.size()	* sizeof(TVertexNormalStream), bufferAlignment);
	}

	gAssert(bufferSize < std::numeric_limits<uint32>::max(), "Offset stored in 32-bit int");
	Ref<Buffer> pGeometryData = pDevice->CreateBuffer(BufferDesc{ .Size = bufferSize, .ElementSize = (uint32)bufferSize, .Flags = BufferFlag::ShaderResource | BufferFlag::ByteAddress | BufferFlag::UnorderedAccess }, "Geometry Buffer");

	RingBufferAllocation allocation;
	pDevice->GetRingBuffer()->Allocate((uint32)bufferSize, allocation);

	char* pMappedMemory = (char*)allocation.pMappedMemory;

	uint64 dataOffset = 0;
	auto CopyData = [&dataOffset, &allocation, bufferAlignment](const void* pSource, uint64 size)
		{
			memcpy(static_cast<char*>(allocation.pMappedMemory) + dataOffset, pSource, size);
			dataOffset = Math::AlignUp(dataOffset + size, bufferAlignment);
		};

	BoundingBox bounds;
	bounds.CreateFromPoints(bounds, meshData.PositionsStream.size(), (DirectX::XMFLOAT3*)meshData.PositionsStream.data(), sizeof(Vector3));

	Mesh subMesh;
	subMesh.Bounds = bounds;
	subMesh.PositionsFormat = ResourceFormat::RGB32_FLOAT;

	{
		subMesh.PositionStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.PositionsStream.size(), sizeof(TVertexPositionStream), dataOffset);
		TVertexPositionStream* pTarget = (TVertexPositionStream*)(pMappedMemory + dataOffset);
		for (const Vector3& position : meshData.PositionsStream)
		{
			*pTarget++ = { position };
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.PositionsStream.size() * sizeof(TVertexPositionStream), bufferAlignment);

		if (hasAnim)
		{
			subMesh.SkinnedPositionStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.PositionsStream.size(), sizeof(TVertexPositionStream), dataOffset);
			dataOffset = Math::AlignUp(dataOffset + meshData.PositionsStream.size() * sizeof(TVertexPositionStream), bufferAlignment);
		}
	}

	{
		subMesh.NormalStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.NormalsStream.size(), sizeof(TVertexNormalStream), dataOffset);
		TVertexNormalStream* pTarget = (TVertexNormalStream*)(pMappedMemory + dataOffset);
		for (size_t i = 0; i < meshData.NormalsStream.size(); ++i)
		{
			*pTarget++ = {
					Math::Pack_RGB10A2_SNORM(Vector4(meshData.NormalsStream[i])),
					Math::Pack_RGB10A2_SNORM(meshData.TangentsStream.empty() ? Vector4(1, 0, 0, 1) : meshData.TangentsStream[i])
			};
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.NormalsStream.size() * sizeof(TVertexNormalStream), bufferAlignment);

		if (hasAnim)
		{
			subMesh.SkinnedNormalStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.NormalsStream.size(), sizeof(TVertexNormalStream), dataOffset);
			dataOffset = Math::AlignUp(dataOffset + meshData.NormalsStream.size() * sizeof(TVertexNormalStream), bufferAlignment);
		}
	}

	if (!meshData.ColorsStream.empty())
	{
		subMesh.ColorsStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.ColorsStream.size(), sizeof(TVertexColorStream), dataOffset);
		TVertexColorStream* pTarget = (TVertexColorStream*)(pMappedMemory + dataOffset);
		for (const Vector4& color : meshData.ColorsStream)
		{
			*pTarget++ = { Math::Pack_RGBA8_UNORM(color) };
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.ColorsStream.size() * sizeof(TVertexColorStream), bufferAlignment);
	}

	if (!meshData.UVsStream.empty())
	{
		subMesh.UVStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.UVsStream.size(), sizeof(TVertexUVStream), dataOffset);
		TVertexUVStream* pTarget = (TVertexUVStream*)(pMappedMemory + dataOffset);
		for (const Vector2& uv : meshData.UVsStream)
		{
			*pTarget++ = { Math::Pack_RG16_FLOAT(uv) };
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.UVsStream.size() * sizeof(TVertexUVStream), bufferAlignment);
	}

	if (!meshData.JointsStream.empty())
	{
		subMesh.JointsStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.JointsStream.size(), sizeof(TJointsStream), dataOffset);
		TJointsStream* pTarget = (TJointsStream*)(pMappedMemory + dataOffset);
		for (const Vector4i& joint : meshData.JointsStream)
		{
			*pTarget++ = { (uint16)joint.x, (uint16)joint.y, (uint16)joint.z, (uint16)joint.w };
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.JointsStream.size() * sizeof(TJointsStream), bufferAlignment);
	}

	if (!meshData.WeightsStream.empty())
	{
		subMesh.WeightsStreamLocation = VertexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.WeightsStream.size(), sizeof(TWeightsStream), dataOffset);
		TWeightsStream* pTarget = (TWeightsStream*)(pMappedMemory + dataOffset);
		for (const Vector4& weight: meshData.WeightsStream)
		{
			*pTarget++ = { Math::Pack_RGBA16_FLOAT(weight) };
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.WeightsStream.size() * sizeof(TWeightsStream), bufferAlignment);
	}

	{
		bool smallIndices = meshData.PositionsStream.size() < std::numeric_limits<uint16>::max();
		uint32 indexSize = smallIndices ? sizeof(uint16) : sizeof(uint32);
		subMesh.IndicesLocation = IndexBufferView(pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.Indices.size(), smallIndices ? ResourceFormat::R16_UINT : ResourceFormat::R32_UINT, dataOffset);
		char* pTarget = (char*)(pMappedMemory + dataOffset);
		for (uint32 index : meshData.Indices)
		{
			memcpy(pTarget, &index, indexSize);
			pTarget += indexSize;
		}
		dataOffset = Math::AlignUp(dataOffset + meshData.Indices.size() * indexSize, bufferAlignment);
	}

	subMesh.MeshletsLocation = (uint32)dataOffset;
	CopyData(meshData.Meshlets.data(), sizeof(ShaderInterop::Meshlet) * meshData.Meshlets.size());

	subMesh.MeshletVerticesLocation = (uint32)dataOffset;
	CopyData(meshData.MeshletVertices.data(), sizeof(uint32) * meshData.MeshletVertices.size());

	subMesh.MeshletTrianglesLocation = (uint32)dataOffset;
	CopyData(meshData.MeshletTriangles.data(), sizeof(ShaderInterop::Meshlet::Triangle) * meshData.MeshletTriangles.size());

	subMesh.MeshletBoundsLocation = (uint32)dataOffset;
	CopyData(meshData.MeshletBounds.data(), sizeof(ShaderInterop::Meshlet::Bounds) * meshData.MeshletBounds.size());

	subMesh.NumMeshlets = (uint32)meshData.Meshlets.size();

	subMesh.pBuffer = pGeometryData;

	allocation.pContext->CopyBuffer(allocation.pBackingResource, pGeometryData, bufferSize, allocation.Offset, 0);
	pDevice->GetRingBuffer()->Free(allocation);

	return subMesh;
}


static bool LoadLdr(const char* pFilePath, GraphicsDevice* pDevice, World& world)
{
	LdrConfig config;
	config.pDatabasePath = "D:/References/ldraw/ldraw/";
	//config.Quality = LdrQuality::High;

	// Logo studs
	config.ReplacementMap.push_back({ "stud.dat", "stud-logo4.dat" });

	// No studs
	//config.ReplacementMap.push_back({ "stud.dat", nullptr });

	LdrState context;
	if (LdrInit(&config, &context) != LdrResult::Success)
		return false;

	LdrModel mdl;
	if (LdrLoadModel(pFilePath, &context, mdl) != LdrResult::Success)
		return false;

	auto FixBaseColor = [](Color& c)
		{
			c.x = powf(c.x, 2.2f);
			c.y = powf(c.y, 2.2f);
			c.z = powf(c.z, 2.2f);
		};

	auto CreateMaterialFromLDraw = [&](int color) {
		Material mat;
		const LdrMaterial& lmat = LdrGetMaterial(color, &context);
		mat.Name = lmat.Name.c_str();
		LdrDecodeARGB(lmat.Color, &mat.BaseColorFactor.x);
		FixBaseColor(mat.BaseColorFactor);
		mat.RoughnessFactor = 0.1f;
		mat.MetalnessFactor = 0.0f;
		mat.AlphaMode = mat.BaseColorFactor.w >= 1 ? MaterialAlphaMode::Opaque : MaterialAlphaMode::Blend;

		if (lmat.Type == LdrMaterialFinish::Metallic)
		{
			mat.MetalnessFactor = 1.0f;
			mat.RoughnessFactor = 0.1f;
		}
		else if (lmat.Type == LdrMaterialFinish::MatteMetallic)
		{
			mat.MetalnessFactor = 1.0f;
			mat.RoughnessFactor = 0.5f;
		}
		else if (lmat.Type == LdrMaterialFinish::Chrome)
		{
			mat.MetalnessFactor = 1.0f;
			mat.RoughnessFactor = 0.0f;
		}
		return mat;
		};

	struct MaterialPartCombination
	{
		LdrPart* pPart;
		uint32 Color;
		uint32 Index;
	};

	// Materials are part of the mesh so instances of the same mesh but different material have to be duplicated :(
	Array<MaterialPartCombination> map;

	for (int i = 0; i < (int)mdl.Instances.size(); ++i)
	{
		const LdrModel::Instance& instance = mdl.Instances[i];
		LdrPart* pPart = mdl.Parts[instance.Index];

		auto it = std::find_if(map.begin(), map.end(), [&](const MaterialPartCombination& cm)
			{
				return cm.Color == instance.Color && cm.pPart == pPart;
			});

		uint32 materialIndex = 0;
		uint32 meshIndex = 0;
		if (it != map.end())
		{
			meshIndex = it->Index;
			materialIndex = it->Index;
		}
		else
		{
			Material material = CreateMaterialFromLDraw(instance.Color);
			if (pPart->IsMultiMaterial)
				material.BaseColorFactor = Color(1, 1, 1, 1);

			MeshData mesh;
			mesh.Indices.resize(pPart->Indices.size());
			for (int j = 0; j < (int)pPart->Indices.size(); ++j)
			{
				mesh.Indices[j] = pPart->Indices[j];
			}
			mesh.PositionsStream.resize(pPart->Vertices.size());
			mesh.NormalsStream.resize(pPart->Vertices.size());
			mesh.TangentsStream.resize(pPart->Vertices.size());
			if (pPart->IsMultiMaterial)
				mesh.ColorsStream.resize(pPart->Colors.size());
			for (int j = 0; j < (int)pPart->Vertices.size(); ++j)
			{
				mesh.PositionsStream[j] = Vector3(pPart->Vertices[j].x, pPart->Vertices[j].y, pPart->Vertices[j].z);
				mesh.NormalsStream[j] = Vector3(pPart->Normals[j].x, pPart->Normals[j].y, pPart->Normals[j].z);
				mesh.TangentsStream[j] = Vector4(1, 0, 0, 1);

				if (pPart->IsMultiMaterial)
				{
					uint32 vertexColor = LdrResolveVertexColor(instance.Color, pPart->Colors[j], &context);
					Color verColor;
					LdrDecodeARGB(vertexColor, &verColor.x);
					FixBaseColor(verColor);
					mesh.ColorsStream[j] = verColor;
				}
			}

			MaterialPartCombination combination;
			combination.pPart = pPart;
			combination.Color = instance.Color;
			combination.Index = (int)world.Meshes.size();
			map.push_back(combination);
			meshIndex = combination.Index;

			world.Meshes.push_back(CreateMesh(pDevice, mesh));
			world.Materials.push_back(material);
		}

		entt::entity entity = world.Registry.create();
		Model& model = world.Registry.emplace<Model>(entity);
		model.MeshIndex = meshIndex;
		model.MaterialId = materialIndex;
		Transform& transform = world.Registry.emplace<Transform>(entity);

		Matrix t = Matrix(&instance.Transform.m[0][0]);
		t.Decompose(transform.Scale, transform.Rotation, transform.Position);
	}
	return true;
}


static bool LoadGltf(const char* pFilePath, GraphicsDevice* pDevice, World& world)
{
	cgltf_options options{};
	cgltf_data* pGltfData = nullptr;
	cgltf_result result = cgltf_parse_file(&options, pFilePath, &pGltfData);
	if (result != cgltf_result_success)
	{
		E_LOG(Warning, "GLTF - Failed to load '%s'", pFilePath);
		return false;
	}
	result = cgltf_load_buffers(&options, pGltfData, pFilePath);
	if (result != cgltf_result_success)
	{
		E_LOG(Warning, "GLTF - Failed to load buffers '%s'", pFilePath);
		return false;
	}

	// Load unique textures;
	HashMap<const cgltf_texture*, Texture*> imageToTexture;
	HashMap<const cgltf_material*, uint32> materialToIndex;
	materialToIndex[nullptr] = 0;
	HashMap<const cgltf_primitive*, uint32> meshToIndex;

	// Load Animations
	for (const cgltf_animation& gltfAnimation : Span(pGltfData->animations, (uint32)pGltfData->animations_count))
	{
		Animation& animation = world.Animations.emplace_back();

		animation.Name = gltfAnimation.name ? gltfAnimation.name : "Unnamed";
		for (const cgltf_animation_channel& gltfChannel : Span(gltfAnimation.channels, (uint32)gltfAnimation.channels_count))
		{
			AnimationChannel& channel = animation.Channels.emplace_back();

			channel.Target = gltfChannel.target_node->name;

			// Animation type
			switch (gltfChannel.target_path)
			{
			case cgltf_animation_path_type_translation:		channel.Path = AnimationChannel::PathType::Translation; break;
			case cgltf_animation_path_type_rotation:		channel.Path = AnimationChannel::PathType::Rotation; break;
			case cgltf_animation_path_type_scale:			channel.Path = AnimationChannel::PathType::Scale; break;
			default: gUnreachable();
			}

			// Sampler
			const cgltf_animation_sampler& gltfSampler = *gltfChannel.sampler;
			switch (gltfSampler.interpolation)
			{
			case cgltf_interpolation_type_step:				channel.Interpolation = AnimationChannel::Interpolation::Step; break;
			case cgltf_interpolation_type_linear:			channel.Interpolation = AnimationChannel::Interpolation::Linear; break;
			case cgltf_interpolation_type_cubic_spline:		channel.Interpolation = AnimationChannel::Interpolation::Cubic; break;
			default: gUnreachable();
			}

			// Read time keys
			channel.KeyFrames.resize(gltfSampler.input->count);
			gAssert(cgltf_num_components(gltfSampler.input->type) == 1);
			gVerify(cgltf_accessor_unpack_floats(gltfSampler.input, &channel.KeyFrames[0], gltfSampler.input->count), > 0);

			// Read key data
			channel.Data.resize(gltfSampler.output->count);
			int num_components = (int)cgltf_num_components(gltfSampler.output->type);
			gAssert(num_components <= 4);
			for (int i = 0; i < gltfSampler.output->count; ++i)
			{
				gVerify(cgltf_accessor_read_float(gltfSampler.output, i, &channel.Data[i].x, num_components), == 1);
			}

			// Track min/max time of animation
			animation.TimeStart = Math::Min(animation.TimeStart, channel.KeyFrames.front());
			animation.TimeEnd = Math::Max(animation.TimeEnd, channel.KeyFrames.back());
		}
	}

	// Load Skeletons
	for (const cgltf_skin& gltfSkin : Span(pGltfData->skins, (uint32)pGltfData->skins_count))
	{
		Skeleton& skeleton = world.Skeletons.emplace_back();

		// Load inverse bind matrices
		skeleton.InverseBindMatrices.resize(gltfSkin.joints_count);
		gAssert(cgltf_num_components(gltfSkin.inverse_bind_matrices->type) == 16);
		gVerify(cgltf_accessor_unpack_floats(gltfSkin.inverse_bind_matrices, &skeleton.InverseBindMatrices[0].m[0][0], gltfSkin.joints_count * 16), > 0);

		// Joint name to index mapping
		for (int i = 0; i < gltfSkin.joints_count; ++i)
		{
			Skeleton::JointIndex joint = (Skeleton::JointIndex)i;
			skeleton.JointsMap[gltfSkin.joints[i]->name] = joint;
		}

		// Compute parent index of each joint
		Skeleton::JointIndex rootJoint = Skeleton::InvalidJoint;
		Array<Array<Skeleton::JointIndex>> parentToChildMap(gltfSkin.joints_count);
		skeleton.ParentIndices.resize(gltfSkin.joints_count);
		for (Skeleton::JointIndex i = 0; i < (Skeleton::JointIndex)gltfSkin.joints_count; ++i)
		{
			if (gltfSkin.joints[i] != gltfSkin.skeleton)
			{
				const cgltf_node* pParent = gltfSkin.joints[i]->parent;
				Skeleton::JointIndex parentJoint = skeleton.GetJoint(pParent->name);
				skeleton.ParentIndices[i] = parentJoint;

				parentToChildMap[parentJoint].push_back(i);
			}
			else
			{
				skeleton.ParentIndices[i] = Skeleton::InvalidJoint;
				rootJoint = i;
			}
		}

		// Compute joints order so that parent joints are always evaluates before any children
		skeleton.JointUpdateOrder.reserve(gltfSkin.joints_count);
		Array<Skeleton::JointIndex> stack;
		stack.reserve(gltfSkin.joints_count);
		stack.push_back(rootJoint);
		while (!stack.empty())
		{
			Skeleton::JointIndex joint = stack.back();
			stack.pop_back();
			skeleton.JointUpdateOrder.push_back(joint);
			for (Skeleton::JointIndex childJoint : parentToChildMap[joint])
				stack.push_back(childJoint);
		}
	}

	// Load Materials and Textures
	for (const cgltf_material& gltfMaterial : Span(pGltfData->materials, (uint32)pGltfData->materials_count))
	{
		materialToIndex[&gltfMaterial] = (uint32)world.Materials.size();
		Material& material = world.Materials.emplace_back();
		auto RetrieveTexture = [&imageToTexture, &world, pDevice, pFilePath](const cgltf_texture_view& texture, bool srgb) -> Texture*
			{
				if (texture.texture)
				{
					auto it = imageToTexture.find(texture.texture);
					const cgltf_image* pImage = texture.texture->image;
					const char* pName = pImage->uri ? pImage->uri : "Material Texture";
					Ref<Texture> pTex;
					if (it == imageToTexture.end())
					{
						Image image;
						bool validImage = false;
						if (pImage->buffer_view)
						{
							MemoryStream stream(false, (char*)pImage->buffer_view->buffer->data + pImage->buffer_view->offset, (uint32)pImage->buffer_view->size);
							validImage = image.Load(stream, pImage->mime_type);
						}
						else
						{
							FileStream stream;
							if (stream.Open(Paths::Combine(Paths::GetDirectoryPath(pFilePath), pImage->uri).c_str(), FileMode::Read))
							{
								validImage = image.Load(stream, Paths::GetFileExtenstion(pImage->uri).c_str());
							}
						}

						if (validImage)
							pTex = GraphicsCommon::CreateTextureFromImage(pDevice, image, srgb, pName);

						if (!pTex.Get())
						{
							E_LOG(Warning, "GLTF - Failed to load texture '%s' for '%s'", pImage->uri, pFilePath);
							return nullptr;
						}

						world.Textures.push_back(pTex);
						imageToTexture[texture.texture] = world.Textures.back();
						return world.Textures.back();

					}
					return it->second;
				}
				return nullptr;
			};

		auto GetAlphaMode = [](cgltf_alpha_mode mode) {
			switch (mode)
			{
			case cgltf_alpha_mode_blend: return MaterialAlphaMode::Blend;
			case cgltf_alpha_mode_opaque: return MaterialAlphaMode::Opaque;
			case cgltf_alpha_mode_mask: return MaterialAlphaMode::Masked;
			}
			return MaterialAlphaMode::Opaque;
			};

		if (gltfMaterial.has_pbr_metallic_roughness)
		{
			material.pDiffuseTexture = RetrieveTexture(gltfMaterial.pbr_metallic_roughness.base_color_texture, true);
			material.pRoughnessMetalnessTexture = RetrieveTexture(gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture, false);
			material.BaseColorFactor.x = gltfMaterial.pbr_metallic_roughness.base_color_factor[0];
			material.BaseColorFactor.y = gltfMaterial.pbr_metallic_roughness.base_color_factor[1];
			material.BaseColorFactor.z = gltfMaterial.pbr_metallic_roughness.base_color_factor[2];
			material.BaseColorFactor.w = gltfMaterial.pbr_metallic_roughness.base_color_factor[3];
			material.MetalnessFactor = gltfMaterial.pbr_metallic_roughness.metallic_factor;
			material.RoughnessFactor = gltfMaterial.pbr_metallic_roughness.roughness_factor;
		}
		else if (gltfMaterial.has_pbr_specular_glossiness)
		{
			material.pDiffuseTexture = RetrieveTexture(gltfMaterial.pbr_specular_glossiness.diffuse_texture, true);
			material.RoughnessFactor = 1.0f - gltfMaterial.pbr_specular_glossiness.glossiness_factor;
			material.BaseColorFactor.x = gltfMaterial.pbr_specular_glossiness.diffuse_factor[0];
			material.BaseColorFactor.y = gltfMaterial.pbr_specular_glossiness.diffuse_factor[1];
			material.BaseColorFactor.z = gltfMaterial.pbr_specular_glossiness.diffuse_factor[2];
			material.BaseColorFactor.w = gltfMaterial.pbr_specular_glossiness.diffuse_factor[3];
		}
		material.AlphaCutoff = gltfMaterial.alpha_mode == cgltf_alpha_mode_mask ? gltfMaterial.alpha_cutoff : 1.0f;
		material.AlphaMode = GetAlphaMode(gltfMaterial.alpha_mode);
		material.pEmissiveTexture = RetrieveTexture(gltfMaterial.emissive_texture, true);
		material.EmissiveFactor.x = gltfMaterial.emissive_factor[0];
		material.EmissiveFactor.y = gltfMaterial.emissive_factor[1];
		material.EmissiveFactor.z = gltfMaterial.emissive_factor[2];
		if (gltfMaterial.has_emissive_strength)
			material.EmissiveFactor *= gltfMaterial.emissive_strength.emissive_strength;
		material.pNormalTexture = RetrieveTexture(gltfMaterial.normal_texture, false);
		if (gltfMaterial.name)
			material.Name = gltfMaterial.name;
	}

	// Load Meshes
	for (const cgltf_mesh& mesh : Span(pGltfData->meshes, (uint32)pGltfData->meshes_count))
	{
		for (const cgltf_primitive& primitive : Span(mesh.primitives, (uint32)mesh.primitives_count))
		{
			MeshData meshData;
			meshData.Indices.resize(primitive.indices->count);

			constexpr int indexMap[] = { 0, 2, 1 };
			for (size_t i = 0; i < primitive.indices->count; i += 3)
			{
				meshData.Indices[i + 0] = (uint32)cgltf_accessor_read_index(primitive.indices, i + indexMap[0]);
				meshData.Indices[i + 1] = (uint32)cgltf_accessor_read_index(primitive.indices, i + indexMap[1]);
				meshData.Indices[i + 2] = (uint32)cgltf_accessor_read_index(primitive.indices, i + indexMap[2]);
			}

			for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
			{
				const cgltf_attribute& attribute = primitive.attributes[attrIdx];
				if (attribute.type == cgltf_attribute_type_position)
				{
					meshData.PositionsStream.resize(attribute.data->count);
					gVerify(cgltf_accessor_unpack_floats(attribute.data, &meshData.PositionsStream[0].x, attribute.data->count * 3), > 0);
				}
				else if (attribute.type == cgltf_attribute_type_normal)
				{
					meshData.NormalsStream.resize(attribute.data->count);
					gVerify(cgltf_accessor_unpack_floats(attribute.data, &meshData.NormalsStream[0].x, attribute.data->count * 3), > 0);
				}
				else if (attribute.type == cgltf_attribute_type_tangent)
				{
					meshData.TangentsStream.resize(attribute.data->count);
					gVerify(cgltf_accessor_unpack_floats(attribute.data, &meshData.TangentsStream[0].x, attribute.data->count * 4), > 0);
				}
				else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
				{
					meshData.UVsStream.resize(attribute.data->count);
					gVerify(cgltf_accessor_unpack_floats(attribute.data, &meshData.UVsStream[0].x, attribute.data->count * 2), > 0);
				}
				else if (attribute.type == cgltf_attribute_type_color && attribute.index == 0)
				{
					meshData.ColorsStream.resize(attribute.data->count);
					gVerify(cgltf_accessor_unpack_floats(attribute.data, &meshData.ColorsStream[0].x, attribute.data->count * 4), > 0);
				}
				else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0)
				{
					meshData.WeightsStream.resize(attribute.data->count);
					gVerify(cgltf_accessor_unpack_floats(attribute.data, &meshData.WeightsStream[0].x, attribute.data->count * 4), > 0);
				}
				else if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0)
				{
					Vector4 joints;
					meshData.JointsStream.resize(attribute.data->count);
					for (int i = 0; i < attribute.data->count; ++i)
					{
						gVerify(cgltf_accessor_read_float(attribute.data, i, &joints.x, 4), > 0);
						meshData.JointsStream[i] = Vector4i((int)joints.x, (int)joints.y, (int)joints.z, (int)joints.w);
					}
				}
			}
			meshToIndex[&primitive] = (uint32)world.Meshes.size();
			world.Meshes.push_back(CreateMesh(pDevice, meshData));
		}
	}


	// Load Scene Nodes
	for (const cgltf_node& node : Span(pGltfData->nodes, (uint32)pGltfData->nodes_count))
	{
		if (node.mesh)
		{
			Matrix localToWorld;
			cgltf_node_transform_world(&node, &localToWorld.m[0][0]);

			for (const cgltf_primitive& primitive : Span(node.mesh->primitives, (uint32)node.mesh->primitives_count))
			{
				entt::entity entity = world.CreateEntity(node.name ? node.name : "Primitive");
				Transform& transform = world.Registry.emplace<Transform>(entity);
				Model& model = world.Registry.emplace<Model>(entity);

				model.MeshIndex = meshToIndex.at(&primitive);
				model.MaterialId = materialToIndex.at(primitive.material);
				Matrix m = localToWorld * Matrix::CreateScale(1, 1, -1);
				m.Decompose(transform.Scale, transform.Rotation, transform.Position);

				if (node.skin)
				{
					model.SkeletonIndex = 0;
					model.AnimationIndex = 0;
				}
			}
		}

		if (node.light)
		{
			Matrix localToWorld;
			cgltf_node_transform_world(&node, &localToWorld.m[0][0]);
			entt::entity entity = world.CreateEntity(node.name ? node.name : "Light");
			Transform& transform = world.Registry.emplace<Transform>(entity);
			localToWorld.Decompose(transform.Scale, transform.Rotation, transform.Position);

			Light& light				= world.Registry.emplace<Light>(entity);
			light.Colour				= Color(node.light->color[0], node.light->color[1], node.light->color[2], 1.0f);
			light.Intensity				= node.light->intensity;
			light.Range					= node.light->range;
			light.InnerConeAngle		= node.light->spot_inner_cone_angle;
			light.OuterConeAngle		= node.light->spot_outer_cone_angle;

			switch (node.light->type)
			{
			case cgltf_light_type_directional:	light.Type = LightType::Directional; break;
			case cgltf_light_type_spot:			light.Type = LightType::Spot; break;
			case cgltf_light_type_point:		light.Type = LightType::Point; break;
			}
		}
	}

	cgltf_free(pGltfData);
	return true;
}


bool SceneLoader::Load(const char* pFilePath, GraphicsDevice* pDevice, World& world)
{
	String extension = Paths::GetFileExtenstion(pFilePath);
	if (extension == "dat" || extension == "ldr" || extension == "mpd")
	{
		LoadLdr(pFilePath, pDevice, world);
	}
	else
	{
		LoadGltf(pFilePath, pDevice, world);
	}
	return true;
}

