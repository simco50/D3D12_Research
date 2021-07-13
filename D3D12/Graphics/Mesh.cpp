#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Core/Paths.h"
#include "Content/Image.h"

#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "External/cgltf/cgltf.h"
#pragma warning(pop)

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

	struct Vertex
	{
		PackedVector3 Position = PackedVector3(0.0f, 0.0f, 0.0f, 0.0f);
		PackedVector2 TexCoord = PackedVector2(0.0f, 0.0f);
		Vector3 Normal = Vector3::Forward;
		Vector4 Tangent = Vector4(1, 0, 0, 1);
	};

	std::vector<uint32> indices;
	std::vector<Vertex> vertices;
	struct MeshData
	{
		BoundingBox Bounds;
		uint32 NumIndices = 0;
		uint32 IndexOffset = 0;
		uint32 NumVertices = 0;
		uint32 VertexOffset = 0;
		uint32 MaterialIndex = 0;
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
			uint32 vertexOffset = (uint32)vertices.size();
			meshData.VertexOffset = vertexOffset;

			size_t indexCount = primitive.indices->count;
			uint32 indexOffset = (uint32)indices.size();
			indices.resize(indices.size() + indexCount);

			meshData.IndexOffset = indexOffset;
			meshData.NumIndices = (uint32)indexCount;
			meshData.MaterialIndex = MaterialIndex(primitive.material);

			for (size_t i = 0; i < indexCount; ++i)
			{
				indices[indexOffset + i] = (int)cgltf_accessor_read_index(primitive.indices, i);
			}

			for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
			{
				const cgltf_attribute& attribute = primitive.attributes[attrIdx];
				const char* pName = attribute.name;

				if (meshData.NumVertices == 0)
				{
					vertices.resize(vertices.size() + attribute.data->count);
					meshData.NumVertices = (uint32)attribute.data->count;
				}

				if (strcmp(pName, "POSITION") == 0)
				{
					std::vector<Vector3> positions;
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						Vector3 position;
						check(cgltf_accessor_read_float(attribute.data, i, &position.x, 3));
						vertices[i + vertexOffset].Position = PackedVector3(position.x, position.y, position.z, 0);
						positions.push_back(position);
					}
					meshData.Bounds.CreateFromPoints(meshData.Bounds, positions.size(), (DirectX::XMFLOAT3*)positions.data(), sizeof(Vector3));
				}
				else if (strcmp(pName, "NORMAL") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &vertices[i + vertexOffset].Normal.x, 3));
					}
				}
				else if (strcmp(pName, "TANGENT") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						check(cgltf_accessor_read_float(attribute.data, i, &vertices[i + vertexOffset].Tangent.x, 4));
					}
				}
				else if (strcmp(pName, "TEXCOORD_0") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						Vector2 texCoord;
						check(cgltf_accessor_read_float(attribute.data, i, &texCoord.x, 2));
						vertices[i + vertexOffset].TexCoord = PackedVector2(texCoord.x, texCoord.y);
					}
				}
				else
				{
					E_LOG(Warning, "GLTF - Attribute '%s' is unsupported", pName);
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

	// Load in the data
	static constexpr uint64 sBufferAlignment = 16;
	uint64 bufferSize = vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32) + meshDatas.size() * sBufferAlignment;
	m_pGeometryData = pDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Geometry Buffer");
	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COPY_DEST);

	uint64 dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext](void* pSource, uint64 size)
	{
		m_pGeometryData->SetData(pContext, pSource, size, dataOffset);
		dataOffset += size;
		dataOffset = Math::AlignUp<uint64>(dataOffset, sBufferAlignment);
	};

	for (const MeshData& meshData : meshDatas)
	{
		SubMesh subMesh;
		subMesh.Bounds = meshData.Bounds;
		subMesh.MaterialId = meshData.MaterialIndex;

		VertexBufferView vbv(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumVertices, sizeof(Vertex));
		subMesh.PositionsFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		subMesh.VerticesLocation = vbv;
		subMesh.pVertexSRV = new ShaderResourceView();
		subMesh.pVertexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32)dataOffset, meshData.NumVertices * sizeof(Vertex)));
		CopyData(&vertices[meshData.VertexOffset], sizeof(Vertex) * meshData.NumVertices);

		IndexBufferView ibv(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumIndices, DXGI_FORMAT_R32_UINT);
		subMesh.IndicesLocation = ibv;
		subMesh.pIndexSRV = new ShaderResourceView();
		subMesh.pIndexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32)dataOffset, meshData.NumIndices * sizeof(uint32)));
		CopyData(&indices[meshData.IndexOffset], sizeof(uint32) * meshData.NumIndices);

		subMesh.Stride = sizeof(Vertex);
		subMesh.pParent = this;
		m_Meshes.push_back(subMesh);
	}

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COMMON);
	pContext->FlushResourceBarriers();

	return true;
}

void SubMesh::Destroy()
{
	delete pIndexSRV;
	delete pVertexSRV;

	delete pBLAS;
	delete pBLASScratch;
}
