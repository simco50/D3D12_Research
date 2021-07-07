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

struct Vertex
{
	Vector3 Position;
	Vector2 TexCoord;
	Vector3 Normal;
	Vector3 Tangent;
	Vector3 Bitangent;
};


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
	std::map<StringHash, Texture*> textureMap;
	std::map<const cgltf_material*, int> materialMap;

	m_Materials.reserve(pGltfData->materials_count);
	for (size_t i = 0; i < pGltfData->materials_count; ++i)
	{
		const cgltf_material& gltfMaterial = pGltfData->materials[i];
		materialMap[&gltfMaterial] = (int)m_Materials.size();

		m_Materials.push_back(Material());
		Material& material = m_Materials.back();

		auto RetrieveTexture = [this, &textureMap, pDevice, pContext, pFilePath](const cgltf_texture_view texture, bool srgb) -> Texture*
		{
			if (texture.texture)
			{
				const cgltf_image* pImage = texture.texture->image;
				StringHash pathHash = StringHash(pImage->uri);
				pathHash.Combine((int)srgb);
				auto it = textureMap.find(pathHash);
				std::unique_ptr<Texture> pTex = std::make_unique<Texture>(pDevice, pImage->uri);
				if (it == textureMap.end())
				{
					bool success = pTex->Create(pContext, Paths::Combine(Paths::GetDirectoryPath(pFilePath), pImage->uri).c_str(), srgb);
					if (success)
					{
						m_Textures.push_back(std::move(pTex));
						textureMap[pathHash] = m_Textures.back().get();
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

	std::vector<uint32> indices;
	std::vector<Vertex> vertices;
	struct MeshData
	{
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

			size_t indexStride = primitive.indices->stride;
			size_t indexCount = primitive.indices->count;
			uint32 indexOffset = (uint32)indices.size();
			indices.resize(indices.size() + indexCount);

			meshData.IndexOffset = indexOffset;
			meshData.NumIndices = (uint32)indexCount;
			meshData.MaterialIndex = materialMap[primitive.material];

			const cgltf_buffer_view* pIndexView = primitive.indices->buffer_view;
			const char* indexData = (char*)pIndexView->buffer->data + pIndexView->offset + primitive.indices->offset;

			if (indexStride == 4)
			{
				for (size_t i = 0; i < indexCount; ++i)
				{
					indices[indexOffset + i] = ((uint32*)indexData)[i];
				}
			}
			else if (indexStride == 2)
			{
				for (size_t i = 0; i < indexCount; ++i)
				{
					indices[indexOffset + i] = ((uint16*)indexData)[i];
				}
			}
			else
			{
				noEntry();
			}

			for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
			{
				const cgltf_attribute& attribute = primitive.attributes[attrIdx];
				const char* pName = attribute.name;

				const cgltf_buffer_view* pAttrView = attribute.data->buffer_view;
				size_t stride = attribute.data->stride;
				const char* pData = (char*)pAttrView->buffer->data + pAttrView->offset + attribute.data->offset;

				if (meshData.NumVertices == 0)
				{
					vertices.resize(vertices.size() + attribute.data->count);
					meshData.NumVertices = (uint32)attribute.data->count;
				}

				if (strcmp(pName, "POSITION") == 0)
				{
					check(stride == sizeof(Vector3));
					const Vector3* pPositions = (Vector3*)pData;
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						vertices[i + vertexOffset].Position = pPositions[i];
					}
				}
				else if (strcmp(pName, "NORMAL") == 0)
				{
					check(stride == sizeof(Vector3));
					const Vector3* pNormals = (Vector3*)pData;
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						vertices[i + vertexOffset].Normal = pNormals[i];
					}
				}
				else if (strcmp(pName, "TANGENT") == 0)
				{
					check(stride == sizeof(Vector4));
					const Vector4* pTangents = (Vector4*)pData;
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						vertices[i + vertexOffset].Tangent = Vector3(pTangents[i]);
						float sign = pTangents[i].w;
						vertices[i + vertexOffset].Bitangent = Vector3(sign, sign, sign);
					}
				}
				else if (strcmp(pName, "TEXCOORD_0") == 0)
				{
					check(stride == sizeof(Vector2));
					Vector2* pTexCoords = (Vector2*)pData;
					for (size_t i = 0; i < attribute.data->count; ++i)
					{
						vertices[i + vertexOffset].TexCoord = pTexCoords[i];
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

	// Generate bitangents.
	// B = N x T * T.w
	for (Vertex& v : vertices)
	{
		if (v.Normal == Vector3::Zero)
			v.Normal = Vector3::Forward;
		if (v.Tangent == Vector3::Zero)
			v.Tangent = Vector3::Right;
		if (v.Bitangent == Vector3::Zero)
			v.Bitangent = Vector3::Up;

		v.Bitangent *= v.Normal.Cross(v.Tangent);
		v.Bitangent.Normalize();
	}

	// Load in the data
	static constexpr uint64 sBufferAlignment = 16;
	uint64 bufferSize = vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32) + meshDatas.size() * sBufferAlignment;
	m_pGeometryData = pDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Geometry Buffer");
	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COPY_DEST);

		
	const cgltf_scene* pGltfScene = pGltfData->scene;
	for (size_t i = 0; i < pGltfScene->nodes_count; i++)
	{
		const cgltf_node* pParent = pGltfScene->nodes[i];
		struct QueuedNode
		{
			const cgltf_node* pNode;
			Matrix Transform;
		};
		std::queue<QueuedNode> toProcess;
		QueuedNode rootNode;
		rootNode.pNode = pParent;
		rootNode.Transform = Matrix::CreateScale(uniformScale);
		toProcess.push(rootNode);
		while (!toProcess.empty())
		{
			QueuedNode currentNode = toProcess.front();
			toProcess.pop();
			const cgltf_node* pNode = currentNode.pNode;
			Matrix transform = currentNode.Transform;
			if (pNode->has_matrix)
			{
				transform *= Matrix(pNode->matrix);
			}
			if (pNode->has_scale)
			{
				transform *= Matrix::CreateScale(Vector3(pNode->scale));
			}
			if (pNode->has_rotation)
			{
				transform *= Matrix::CreateFromQuaternion(Quaternion(pNode->rotation));
			}
			if (pNode->has_translation)
			{
				transform *= Matrix::CreateTranslation(Vector3(pNode->translation));
			}

			SubMeshInstance newNode;
			newNode.Transform = transform;
			if (pNode->mesh)
			{
				for (int primitive : meshToPrimitives[pNode->mesh])
				{
					newNode.MeshIndex = primitive;
					m_MeshInstances.push_back(newNode);
				}
			}

			for (size_t childIdx = 0; childIdx < pNode->children_count; ++childIdx)
			{
				QueuedNode q;
				q.pNode = pNode->children[childIdx];
				q.Transform = newNode.Transform;
				toProcess.push(q);
			}
		}
	}

	cgltf_free(pGltfData);

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
		BoundingBox::CreateFromPoints(subMesh.Bounds, meshData.NumVertices, (Vector3*)&vertices[meshData.VertexOffset], sizeof(Vertex));
		subMesh.MaterialId = meshData.MaterialIndex;

		VertexBufferView vbv(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumVertices, sizeof(Vertex));
		subMesh.VerticesLocation = vbv;
		subMesh.pVertexSRV = new ShaderResourceView();
		subMesh.pVertexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32)dataOffset, meshData.NumVertices * sizeof(Vertex)));
		CopyData(&vertices[meshData.VertexOffset], sizeof(Vertex) * meshData.NumVertices);

		IndexBufferView ibv(m_pGeometryData->GetGpuHandle() + dataOffset, meshData.NumIndices, false);
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
