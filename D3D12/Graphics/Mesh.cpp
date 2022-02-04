#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/Buffer.h"
#include "Core/Paths.h"
#include "Content/Image.h"
#include "Core/Utils.h"
#include "Core/ShaderInterop.h"

#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#pragma warning(pop)

#include "meshoptimizer.h"

#include "LDraw.h"

Mesh::~Mesh()
{
	for (SubMesh& subMesh : m_Meshes)
	{
		subMesh.Destroy();
	}
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pDevice, CommandContext* pContext, float uniformScale /*= 1.0f*/)
{
	struct VS_Position
	{
		Vector3 Position = Vector3(0.0f, 0.0f, 0.0f);
	};

	struct VS_UV
	{
		PackedVector2 UV = PackedVector2(0.0f, 0.0f);
	};

	struct VS_Normal
	{
		Vector3 Normal = Vector3::Forward;
		Vector4 Tangent = Vector4(1, 0, 0, 1);
	};

	struct MeshData
	{
		uint32 MaterialIndex = 0;

		std::vector<Vector3> PositionsStream;
		std::vector<VS_Normal> NormalsStream;
		std::vector<Vector2> UVsStream;
		std::vector<uint32> Indices;

		std::vector<ShaderInterop::Meshlet> Meshlets;
		std::vector<uint32> MeshletVertices;
		std::vector<ShaderInterop::MeshletTriangle> MeshletTriangles;
		std::vector<ShaderInterop::MeshletBounds> MeshletBounds;
	};

	std::vector<MeshData> meshDatas;

	std::string extension = Paths::GetFileExtenstion(pFilePath);
	if (extension == "dat" || extension == "ldr" || extension == "mpd")
	{
		LDraw::Context context;
		context.Init();

		LDraw::Model mdl;
		context.LoadModel(pFilePath, mdl);

		Material defaultMaterial;
		m_Materials.push_back(defaultMaterial);

		for (LDraw::Part* pPart : mdl.Parts)
		{
			MeshData mesh;
			mesh.MaterialIndex = 0;

			for (int i = 0; i < (int)pPart->Vertices.size(); i += 3)
			{
				mesh.PositionsStream.push_back(pPart->Vertices[i + 0]);
				mesh.PositionsStream.push_back(pPart->Vertices[i + 1]);
				mesh.PositionsStream.push_back(pPart->Vertices[i + 2]);

				mesh.NormalsStream.push_back({ pPart->Normals[i + 0], Vector4(1, 0, 0, 1) });
				mesh.NormalsStream.push_back({ pPart->Normals[i + 1], Vector4(1, 0, 0, 1) });
				mesh.NormalsStream.push_back({ pPart->Normals[i + 2], Vector4(1, 0, 0, 1) });

				mesh.Indices.push_back((int)mesh.Indices.size());
				mesh.Indices.push_back((int)mesh.Indices.size());
				mesh.Indices.push_back((int)mesh.Indices.size());
			}

			meshDatas.push_back(mesh);
		}

		for (const LDraw::Model::Instance& partInstance : mdl.Instances)
		{
			SubMeshInstance instance;
			instance.MeshIndex = partInstance.Index;
			instance.Transform = partInstance.Transform * Matrix::CreateScale(0.01f);
			m_MeshInstances.push_back(instance);
		}
	}
	else
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
		std::map<const cgltf_image*, Texture*> textureMap;

		auto MaterialIndex = [&](const cgltf_material* pMat) -> int
		{
			if (!pMat)
			{
				return 0;
			}
			return (int)(pMat - pGltfData->materials) + 1;
		};

		Material defaultMaterial;
		m_Materials.push_back(defaultMaterial);

		m_Materials.reserve(pGltfData->materials_count + 1);
		for (size_t i = 0; i < pGltfData->materials_count; ++i)
		{
			const cgltf_material& gltfMaterial = pGltfData->materials[i];

			m_Materials.push_back(Material());
			Material& material = m_Materials.back();

			auto RetrieveTexture = [this, &textureMap, pDevice, pContext, pFilePath](const cgltf_texture_view texture, bool srgb) -> Texture*
			{
				if (texture.texture)
				{
					const cgltf_image* pImage = texture.texture->image;
					auto it = textureMap.find(pImage);
					std::unique_ptr<Texture> pTex = std::make_unique<Texture>(pDevice, pImage->uri ? pImage->uri : "Material Texture");
					if (it == textureMap.end())
					{
						bool success = false;
						if (pImage->buffer_view)
						{
							Image newImg;
							if (newImg.Load((char*)pImage->buffer_view->buffer->data + pImage->buffer_view->offset, pImage->buffer_view->size, pImage->mime_type))
							{
								success = pTex->Create(pContext, newImg, srgb);
							}
						}
						else
						{
							success = pTex->Create(pContext, Paths::Combine(Paths::GetDirectoryPath(pFilePath), pImage->uri).c_str(), srgb);
						}
						if (success)
						{
							m_Textures.push_back(std::move(pTex));
							textureMap[pImage] = m_Textures.back().get();
							return m_Textures.back().get();
						}
						else
						{
							E_LOG(Warning, "GLTF - Failed to load texture '%s' for '%s'", pImage->uri, pFilePath);
						}
					}
					else
					{
						return it->second;
					}
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
			material.AlphaCutoff = gltfMaterial.alpha_cutoff;
			material.AlphaMode = GetAlphaMode(gltfMaterial.alpha_mode);
			material.pEmissiveTexture = RetrieveTexture(gltfMaterial.emissive_texture, true);
			material.EmissiveFactor.x = gltfMaterial.emissive_factor[0];
			material.EmissiveFactor.y = gltfMaterial.emissive_factor[1];
			material.EmissiveFactor.z = gltfMaterial.emissive_factor[2];
			material.pNormalTexture = RetrieveTexture(gltfMaterial.normal_texture, false);
			if (gltfMaterial.name)
				material.Name = gltfMaterial.name;
		}

		std::map<const cgltf_mesh*, std::vector<int>> meshToPrimitives;
		int primitiveIndex = 0;

		for (size_t meshIdx = 0; meshIdx < pGltfData->meshes_count; ++meshIdx)
		{
			const cgltf_mesh& mesh = pGltfData->meshes[meshIdx];
			std::vector<int> primitives;
			for (size_t primIdx = 0; primIdx < mesh.primitives_count; ++primIdx)
			{
				const cgltf_primitive& primitive = mesh.primitives[primIdx];
				primitives.push_back(primitiveIndex++);
				MeshData meshData;

				meshData.MaterialIndex = MaterialIndex(primitive.material);
				meshData.Indices.resize(primitive.indices->count);

				constexpr int indexMap[] = { 0, 2, 1 };
				for (size_t i = 0; i < primitive.indices->count; i += 3)
				{
					meshData.Indices[i + 0] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[0]);
					meshData.Indices[i + 1] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[1]);
					meshData.Indices[i + 2] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[2]);
				}

				for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
				{
					const cgltf_attribute& attribute = primitive.attributes[attrIdx];
					const char* pName = attribute.name;

					if (strcmp(pName, "POSITION") == 0)
					{
						meshData.PositionsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.PositionsStream[i].x, 3));
						}
					}
					else if (strcmp(pName, "NORMAL") == 0)
					{
						meshData.NormalsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.NormalsStream[i].Normal.x, 3));
						}
					}
					else if (strcmp(pName, "TANGENT") == 0)
					{
						meshData.NormalsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.NormalsStream[i].Tangent.x, 4));
						}
					}
					else if (strcmp(pName, "TEXCOORD_0") == 0)
					{
						meshData.UVsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.UVsStream[i].x, 2));
						}
					}
					else
					{
						validateOncef(false, "GLTF - Attribute '%s' is unsupported", pName);
					}
				}
				meshDatas.push_back(meshData);
			}
			meshToPrimitives[&mesh] = primitives;
		}

		for (size_t i = 0; i < pGltfData->nodes_count; i++)
		{
			const cgltf_node& node = pGltfData->nodes[i];

			cgltf_float matrix[16];
			cgltf_node_transform_world(&node, matrix);

			if (node.mesh)
			{
				SubMeshInstance newNode;
				newNode.Transform = Matrix(matrix) * Matrix::CreateScale(uniformScale, uniformScale, -uniformScale);
				for (int primitive : meshToPrimitives[node.mesh])
				{
					newNode.MeshIndex = primitive;
					m_MeshInstances.push_back(newNode);
				}
			}
		}

		cgltf_free(pGltfData);

	}

	uint64 bufferSize = 0;

	for (MeshData& meshData : meshDatas)
	{
#if 0
		meshopt_optimizeVertexCache(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());

		meshopt_optimizeOverdraw(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), 1.05f);

		std::vector<uint32> remap(meshData.PositionsStream.size());
		meshopt_optimizeVertexFetchRemap(&remap[0], meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());
		meshopt_remapIndexBuffer(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &remap[0]);
		meshopt_remapVertexBuffer(meshData.PositionsStream.data(), meshData.PositionsStream.data(), meshData.PositionsStream.size(), sizeof(Vector3), &remap[0]);
		meshopt_remapVertexBuffer(meshData.NormalsStream.data(), meshData.NormalsStream.data(), meshData.NormalsStream.size(), sizeof(VS_Normal), &remap[0]);
		meshopt_remapVertexBuffer(meshData.UVsStream.data(), meshData.UVsStream.data(), meshData.UVsStream.size(), sizeof(Vector2), &remap[0]);

		// Meshlet generation
		const size_t max_vertices = ShaderInterop::MESHLET_MAX_VERTICES;
		const size_t max_triangles = ShaderInterop::MESHLET_MAX_TRIANGLES;
		const float cone_weight = 0.5f;

		size_t max_meshlets = meshopt_buildMeshletsBound(meshData.Indices.size(), max_vertices, max_triangles);

		meshData.Meshlets.resize(max_meshlets);
		meshData.MeshletVertices.resize(max_meshlets * max_vertices);

		std::vector<unsigned char> meshlet_triangles(max_meshlets* max_triangles * 3);
		std::vector<meshopt_Meshlet> meshlets(max_meshlets);

		size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshData.MeshletVertices.data(), meshlet_triangles.data(),
			meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), max_vertices, max_triangles, cone_weight);

		// Trimming
		const meshopt_Meshlet& last = meshlets[meshlet_count - 1];
		meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
		meshlets.resize(meshlet_count);

		meshData.MeshletVertices.resize(last.vertex_offset + last.vertex_count);
		meshData.Meshlets.resize(meshlet_count);
		meshData.MeshletBounds.resize(meshlet_count);
		meshData.MeshletTriangles.resize(meshlet_triangles.size());

		uint32 triangleOffset = 0;
		for (size_t i = 0; i < meshlet_count; ++i)
		{
			const meshopt_Meshlet& meshlet = meshlets[i];
			const meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshData.MeshletVertices[meshlet.vertex_offset], &meshlet_triangles[meshlet.triangle_offset],
				meshlet.triangle_count, &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3));

			ShaderInterop::MeshletBounds& outBounds = meshData.MeshletBounds[i];
			outBounds.Center = Vector3(bounds.center);
			outBounds.ConeApex = Vector3(bounds.cone_apex);
			outBounds.ConeAxis = Vector3(bounds.cone_axis);
			outBounds.Radius = bounds.radius;
			outBounds.ConeCutoff = bounds.cone_cutoff;
			memcpy(&outBounds.ConeS8, &bounds.cone_axis_s8, sizeof(uint32));

			// Encode triangles and get rid of 4 byte padding
			for (uint32 triIdx = 0; triIdx < meshlet.triangle_count; ++triIdx)
			{
				meshData.MeshletTriangles[triIdx + triangleOffset].V0 = meshlet_triangles[triIdx * 3 + 0 + meshlet.triangle_offset];
				meshData.MeshletTriangles[triIdx + triangleOffset].V1 = meshlet_triangles[triIdx * 3 + 1 + meshlet.triangle_offset];
				meshData.MeshletTriangles[triIdx + triangleOffset].V2 = meshlet_triangles[triIdx * 3 + 2 + meshlet.triangle_offset];
			}

			ShaderInterop::Meshlet& outMeshlet = meshData.Meshlets[i];
			outMeshlet.TriangleCount = meshlet.triangle_count;
			outMeshlet.TriangleOffset = triangleOffset;
			outMeshlet.VertexCount = meshlet.vertex_count;
			outMeshlet.VertexOffset = meshlet.vertex_offset;
			triangleOffset += meshlet.triangle_count * 3;
		}
		meshData.MeshletTriangles.resize(triangleOffset);
#endif

		bufferSize += Math::AlignUp<uint64>(meshData.Meshlets.size() * sizeof(ShaderInterop::Meshlet), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.MeshletVertices.size() * sizeof(uint32), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.MeshletTriangles.size() * sizeof(ShaderInterop::MeshletTriangle), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.MeshletBounds.size() * sizeof(ShaderInterop::MeshletBounds), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.Indices.size() * sizeof(uint32), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.PositionsStream.size() * sizeof(VS_Position), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.UVsStream.size() * sizeof(VS_UV), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.NormalsStream.size() * sizeof(VS_Normal), 16);
	}

	m_pGeometryData = pDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Geometry Buffer");
	DynamicAllocation allocation = pContext->AllocateTransientMemory(bufferSize);

	uint64 dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext, &allocation](const void* pSource, uint64 size)
	{
		checkf(dataOffset < std::numeric_limits<uint32>::max(), "Offset stored in 32-bit int");
		memcpy(static_cast<char*>(allocation.pMappedMemory) + dataOffset, pSource, size);
		dataOffset = Math::AlignUp(dataOffset + size, 16ull);
	};

	for (const MeshData& meshData : meshDatas)
	{
		BoundingBox bounds;
		if(meshData.PositionsStream.size() > 0)
			bounds.CreateFromPoints(bounds, meshData.PositionsStream.size(), (DirectX::XMFLOAT3*)meshData.PositionsStream.data(), sizeof(Vector3));

		SubMesh subMesh;
		subMesh.Bounds = bounds;
		subMesh.MaterialId = meshData.MaterialIndex;
		subMesh.PositionsFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		subMesh.PositionsStride = sizeof(VS_Position);

		subMesh.PositionStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.PositionsStream.size(), sizeof(VS_Position), dataOffset);
		std::vector<VS_Position> positionStream;
		positionStream.reserve(meshData.PositionsStream.size());
		Utils::Transform(meshData.PositionsStream, positionStream, [](const Vector3& value) -> VS_Position { return { Vector3(value.x, value.y, value.z) }; });
		CopyData(positionStream.data(), sizeof(VS_Position)* meshData.PositionsStream.size());

		subMesh.NormalStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.NormalsStream.size(), sizeof(VS_Normal), dataOffset);
		CopyData(meshData.NormalsStream.data(), sizeof(VS_Normal) * meshData.NormalsStream.size());

		subMesh.UVStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.UVsStream.size(), sizeof(VS_UV), dataOffset);
		std::vector<VS_UV> uvStream;
		uvStream.reserve(meshData.UVsStream.size());
		Utils::Transform(meshData.UVsStream, uvStream, [](const Vector2& value) -> VS_UV { return { PackedVector2(value.x, value.y) }; });
		CopyData(uvStream.data(), sizeof(VS_UV)* uvStream.size());

		if (meshData.PositionsStream.size() < std::numeric_limits<uint16>::max())
		{
			subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.Indices.size(), DXGI_FORMAT_R16_UINT, dataOffset);
			std::vector<uint16> indicesStream;
			indicesStream.reserve(meshData.Indices.size());
			Utils::Transform(meshData.Indices, indicesStream, [](const uint32 value) -> uint16 { assert(value < std::numeric_limits<uint16>::max());  return (uint16)value; });
			CopyData(indicesStream.data(), sizeof(uint16) * indicesStream.size());
		}
		else
		{
			subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.Indices.size(), DXGI_FORMAT_R32_UINT, dataOffset);
			CopyData(meshData.Indices.data(), sizeof(uint32) * meshData.Indices.size());
		}

		subMesh.MeshletsLocation = (uint32)dataOffset;
		CopyData(meshData.Meshlets.data(), sizeof(ShaderInterop::Meshlet) * meshData.Meshlets.size());

		subMesh.MeshletVerticesLocation = (uint32)dataOffset;
		CopyData(meshData.MeshletVertices.data(), sizeof(uint32) * meshData.MeshletVertices.size());

		subMesh.MeshletTrianglesLocation = (uint32)dataOffset;
		CopyData(meshData.MeshletTriangles.data(), sizeof(ShaderInterop::MeshletTriangle) * meshData.MeshletTriangles.size());

		subMesh.MeshletBoundsLocation = (uint32)dataOffset;
		CopyData(meshData.MeshletBounds.data(), sizeof(ShaderInterop::MeshletBounds) * meshData.MeshletBounds.size());

		subMesh.NumMeshlets = (uint32)meshData.Meshlets.size();

		subMesh.pParent = this;
		m_Meshes.push_back(subMesh);
	}

	pContext->CopyBuffer(allocation.pBackingResource, m_pGeometryData.get(), bufferSize, allocation.Offset, 0);

	return true;
}

void SubMesh::Destroy()
{
	delete pBLAS;
	delete pBLASScratch;
}
