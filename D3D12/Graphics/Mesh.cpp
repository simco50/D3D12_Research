#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/Buffer.h"
#include "Core/Paths.h"
#include "Content/Image.h"
#include "Core/ShaderInterop.h"

#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "External/cgltf/cgltf.h"
#pragma warning(pop)

#include "meshoptimizer.h"

Mesh::~Mesh()
{
	for (SubMesh& subMesh : m_Meshes)
	{
		subMesh.Destroy();
	}
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pDevice, CommandContext* pContext, float uniformScale /*= 1.0f*/)
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
		check(pMat);
		return (int)(pMat - pGltfData->materials);
	};

	m_Materials.reserve(pGltfData->materials_count);
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
		material.IsTransparent = gltfMaterial.alpha_mode != cgltf_alpha_mode_opaque;
		material.pEmissiveTexture = RetrieveTexture(gltfMaterial.emissive_texture, true);
		material.EmissiveFactor.x = gltfMaterial.emissive_factor[0];
		material.EmissiveFactor.y = gltfMaterial.emissive_factor[1];
		material.EmissiveFactor.z = gltfMaterial.emissive_factor[2];
		material.pNormalTexture = RetrieveTexture(gltfMaterial.normal_texture, false);
	}

	struct VS_Position
	{
		PackedVector3 Position = PackedVector3(0.0f, 0.0f, 0.0f, 0.0f);
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

	std::vector<uint32> indicesStream;
	std::vector<VS_Position> positionsStream;
	std::vector<Vector3> positionsStreamFull;
	std::vector<VS_UV> uvStream;
	std::vector<VS_Normal> normalStream;

	struct MeshData
	{
		BoundingBox Bounds;
		uint32 NumIndices = 0;
		uint32 IndexOffset = 0;
		uint32 NumVertices = 0;
		uint32 VertexOffset = 0;
		uint32 MaterialIndex = 0;

		std::vector<ShaderInterop::Meshlet> Meshlets;
		std::vector<uint32> MeshletVertices;
		std::vector<ShaderInterop::MeshletTriangle> MeshletTriangles;
		std::vector<ShaderInterop::MeshletBounds> MeshletBounds;
	};

	std::vector<MeshData> meshDatas;
	std::map<const cgltf_mesh*, std::vector<int>> meshToPrimitives;
	int primitiveIndex = 0;

	for (size_t meshIdx = 0; meshIdx < pGltfData->meshes_count; ++meshIdx)
	{
		const cgltf_mesh& mesh = pGltfData->meshes[meshIdx];
		std::vector<int> primtives;
		for (size_t primIdx = 0; primIdx < mesh.primitives_count; ++primIdx)
		{
			const cgltf_primitive& primitive = mesh.primitives[primIdx];
			primtives.push_back(primitiveIndex++);
			MeshData meshData;
			uint32 vertexOffset = (uint32)positionsStream.size();
			meshData.VertexOffset = vertexOffset;

			size_t indexCount = primitive.indices->count;
			uint32 indexOffset = (uint32)indicesStream.size();
			indicesStream.resize(indicesStream.size() + indexCount);

			meshData.IndexOffset = indexOffset;
			meshData.NumIndices = (uint32)indexCount;
			meshData.MaterialIndex = MaterialIndex(primitive.material);

			constexpr int indexMap[] = { 0, 2, 1 };
			for (size_t i = 0; i < indexCount; i += 3)
			{
				indicesStream[indexOffset + i + 0] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[0]);
				indicesStream[indexOffset + i + 1] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[1]);
				indicesStream[indexOffset + i + 2] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[2]);
			}

			for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
			{
				const cgltf_attribute& attribute = primitive.attributes[attrIdx];
				const char* pName = attribute.name;

				if (meshData.NumVertices == 0)
				{
					positionsStream.resize(positionsStream.size() + attribute.data->count);
					positionsStreamFull.resize(positionsStreamFull.size() + attribute.data->count);
					uvStream.resize(uvStream.size() + attribute.data->count);
					normalStream.resize(normalStream.size() + attribute.data->count);
					meshData.NumVertices = (uint32)attribute.data->count;
				}

				if (strcmp(pName, "POSITION") == 0)
				{
					std::vector<Vector3> positions;
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						Vector3 position;
						check(cgltf_accessor_read_float(attribute.data, i, &position.x, 3));
						positionsStream[i + vertexOffset].Position = PackedVector3(position.x, position.y, position.z, 0);
						positionsStreamFull[i + vertexOffset] = Vector3(position.x, position.y, position.z);
						positions.push_back(position);
					}
					meshData.Bounds.CreateFromPoints(meshData.Bounds, positions.size(), (DirectX::XMFLOAT3*)positions.data(), sizeof(Vector3));
				}
				else if (strcmp(pName, "NORMAL") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &normalStream[i + vertexOffset].Normal.x, 3));
					}
				}
				else if (strcmp(pName, "TANGENT") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &normalStream[i + vertexOffset].Tangent.x, 4));
					}
				}
				else if (strcmp(pName, "TEXCOORD_0") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						Vector2 texCoord;
						check(cgltf_accessor_read_float(attribute.data, i, &texCoord.x, 2));
						uvStream[i + vertexOffset].UV = PackedVector2(texCoord.x, texCoord.y);
					}
				}
				else
				{
					validateOncef(false, "GLTF - Attribute '%s' is unsupported", pName);
				}
			}
			meshDatas.push_back(meshData);
		}
		meshToPrimitives[&mesh] = primtives;
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

	uint64 bufferSize = 0;

	for (MeshData& meshData : meshDatas)
	{
		meshopt_optimizeVertexCache(&indicesStream[meshData.IndexOffset], &indicesStream[meshData.IndexOffset], meshData.NumIndices, meshData.NumVertices);

		meshopt_optimizeOverdraw(&indicesStream[meshData.IndexOffset], &indicesStream[meshData.IndexOffset], meshData.NumIndices, &positionsStreamFull[meshData.VertexOffset].x, meshData.NumVertices, sizeof(Vector3), 1.05f);

		std::vector<uint32> remap(meshData.NumVertices);
		meshopt_optimizeVertexFetchRemap(&remap[0], &indicesStream[meshData.IndexOffset], meshData.NumIndices, meshData.NumVertices);
		meshopt_remapIndexBuffer(&indicesStream[meshData.IndexOffset], &indicesStream[meshData.IndexOffset], meshData.NumIndices, &remap[0]);
		meshopt_remapVertexBuffer(&positionsStream[meshData.VertexOffset], &positionsStream[meshData.VertexOffset], meshData.NumVertices, sizeof(VS_Position), &remap[0]);
		meshopt_remapVertexBuffer(&normalStream[meshData.VertexOffset], &normalStream[meshData.VertexOffset], meshData.NumVertices, sizeof(VS_Normal), &remap[0]);
		meshopt_remapVertexBuffer(&uvStream[meshData.VertexOffset], &uvStream[meshData.VertexOffset], meshData.NumVertices, sizeof(VS_UV), &remap[0]);
		meshopt_remapVertexBuffer(&positionsStreamFull[meshData.VertexOffset], &positionsStreamFull[meshData.VertexOffset], meshData.NumVertices, sizeof(Vector3), &remap[0]);

		// Meshlet generation
		const size_t max_vertices = 64;
		const size_t max_triangles = 124;
		const float cone_weight = 0.5f;

		size_t max_meshlets = meshopt_buildMeshletsBound(meshData.NumIndices, max_vertices, max_triangles);

		meshData.Meshlets.resize(max_meshlets);
		meshData.MeshletVertices.resize(max_meshlets * max_vertices);

		std::vector<unsigned char> meshlet_triangles(max_meshlets* max_triangles * 3);
		std::vector<meshopt_Meshlet> meshlets(max_meshlets);

		size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshData.MeshletVertices.data(), meshlet_triangles.data(),
			&indicesStream[meshData.IndexOffset], meshData.NumIndices, &positionsStreamFull[meshData.VertexOffset].x, meshData.NumVertices, sizeof(Vector3), max_vertices, max_triangles, cone_weight);

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
				meshlet.triangle_count, &positionsStreamFull[meshData.VertexOffset].x, meshData.NumVertices, sizeof(Vector3));

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

		bufferSize += meshData.Meshlets.size() * sizeof(ShaderInterop::Meshlet);
		bufferSize += meshData.MeshletVertices.size() * sizeof(uint32);
		bufferSize += meshData.MeshletTriangles.size() * sizeof(ShaderInterop::MeshletTriangle);
		bufferSize += meshData.MeshletBounds.size() * sizeof(ShaderInterop::MeshletBounds);
	}

	// Load in the data
	bufferSize += indicesStream.size() * sizeof(uint32);
	bufferSize += positionsStream.size() * sizeof(VS_Position);
	bufferSize += uvStream.size() * sizeof(VS_UV);
	bufferSize += normalStream.size() * sizeof(VS_Normal);
	m_pGeometryData = pDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Geometry Buffer");

	uint64 dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext](const void* pSource, uint64 size)
	{
		checkf(dataOffset < std::numeric_limits<uint32>::max(), "Offset stored in 32-bit int");
		pContext->InitializeBuffer(m_pGeometryData.get(), pSource, size, dataOffset);
		dataOffset += size;
	};

	for (const MeshData& meshData : meshDatas)
	{
		SubMesh subMesh;
		subMesh.Bounds = meshData.Bounds;
		subMesh.MaterialId = meshData.MaterialIndex;
		subMesh.PositionsFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		subMesh.PositionsStride = sizeof(VS_Position);

		subMesh.PositionStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumVertices, sizeof(VS_Position), dataOffset);
		CopyData(&positionsStream[meshData.VertexOffset], sizeof(VS_Position) * meshData.NumVertices);

		subMesh.NormalStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumVertices, sizeof(VS_Normal), dataOffset);
		CopyData(&normalStream[meshData.VertexOffset], sizeof(VS_Normal)* meshData.NumVertices);

		subMesh.UVStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumVertices, sizeof(VS_UV), dataOffset);
		CopyData(&uvStream[meshData.VertexOffset], sizeof(VS_UV)* meshData.NumVertices);

		subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumIndices, DXGI_FORMAT_R32_UINT, dataOffset);
		CopyData(&indicesStream[meshData.IndexOffset], sizeof(uint32) * meshData.NumIndices);

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

	return true;
}

void SubMesh::Destroy()
{
	delete pBLAS;
	delete pBLASScratch;
}
