#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Core/Paths.h"
#include "Content/Image.h"

#include "External/Stb/stb_image.h"
#include "External/Stb/stb_image_write.h"
#include "External/json/json.hpp"

#pragma warning(push)
#pragma warning(disable: 4702) //unreachable code
#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_EXTERNAL_IMAGE 
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "External/tinygltf/tiny_gltf.h"
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

namespace GltfCallbacks
{
	bool FileExists(const std::string& abs_filename, void*)
	{
		return Paths::FileExists(abs_filename);
	}

	std::string ExpandFilePath(const std::string& filepath, void*)
	{
		DWORD len = ExpandEnvironmentStringsA(filepath.c_str(), NULL, 0);
		char* str = new char[len];
		ExpandEnvironmentStringsA(filepath.c_str(), str, len);
		std::string s(str);
		delete[] str;
		return s;
	}

	bool ReadWholeFile(std::vector<unsigned char>* out, std::string* err, const std::string& filePath, void*)
	{
		std::ifstream s(filePath, std::ios::binary | std::ios::ate);
		if (s.fail())
		{
			return false;
		}
		out->resize((size_t)s.tellg());
		s.seekg(0);
		s.read((char*)out->data(), out->size());
		return true;
	}

	bool WriteWholeFile(std::string* err, const std::string& filePath, const std::vector<unsigned char>& contents, void*)
	{
		std::ofstream s(filePath, std::ios::binary);
		if (s.fail())
		{
			return false;
		}
		s.write((char*)contents.data(), contents.size());
		return true;
	}
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pDevice, CommandContext* pContext, float uniformScale /*= 1.0f*/)
{
	tinygltf::TinyGLTF loader;
	std::string err, warn;

	tinygltf::FsCallbacks callbacks;
	callbacks.ReadWholeFile = GltfCallbacks::ReadWholeFile;
	callbacks.WriteWholeFile = GltfCallbacks::WriteWholeFile;
	callbacks.FileExists = GltfCallbacks::FileExists;
	callbacks.ExpandFilePath = GltfCallbacks::ExpandFilePath;
	loader.SetFsCallbacks(callbacks);

	std::string extension = Paths::GetFileExtenstion(pFilePath);
	tinygltf::Model model;
	bool ret = extension == "gltf" ? loader.LoadASCIIFromFile(&model, &err, &warn, pFilePath) : loader.LoadBinaryFromFile(&model, &err, &warn, pFilePath);
	if (!ret)
	{
		E_LOG(Warning, "GLTF - Failed to load '%s': '%s'", pFilePath, err.c_str());
		return false;
	}
	if (warn.length() > 0)
	{
		E_LOG(Warning, "GLTF - Load warning: %s", warn.c_str());
	}

	// Load unique textures;
	std::map<StringHash, Texture*> textureMap;

	m_Materials.reserve(model.materials.size());
	for (const tinygltf::Material& gltfMaterial : model.materials)
	{
		m_Materials.push_back(Material());
		Material& material = m_Materials.back();

		auto baseColorTexture = gltfMaterial.values.find("baseColorTexture");
		auto metallicRoughnessTexture = gltfMaterial.values.find("metallicRoughnessTexture");
		auto baseColorFactor = gltfMaterial.values.find("baseColorFactor");
		auto roughnessFactor = gltfMaterial.values.find("roughnessFactor");
		auto metallicFactor = gltfMaterial.values.find("metallicFactor");
		auto normalTexture = gltfMaterial.additionalValues.find("normalTexture");
		auto emissiveTexture = gltfMaterial.additionalValues.find("emissiveTexture");
		auto emissiveFactor = gltfMaterial.additionalValues.find("emissiveFactor");
		auto alphaCutoff = gltfMaterial.additionalValues.find("alphaCutoff");
		auto alphaMode = gltfMaterial.additionalValues.find("alphaMode");

		auto RetrieveTexture = [this, &textureMap, &model, pDevice, pContext, pFilePath](bool isValid, auto gltfParameter, Texture** pTarget, bool srgb)
		{
			if (isValid)
			{
				int index = gltfParameter->second.TextureIndex();
				if (index < model.textures.size())
				{
					const tinygltf::Texture& texture = model.textures[index];
					const tinygltf::Image& image = model.images[texture.source];
					StringHash pathHash = StringHash(image.uri.c_str());
					pathHash.Combine((int)srgb);
					auto it = textureMap.find(pathHash);
					std::unique_ptr<Texture> pTex = std::make_unique<Texture>(pDevice, image.uri.c_str());
					if (it == textureMap.end())
					{
						bool success = false;
						if (image.uri.empty())
						{
							Image img;
							img.SetSize(image.width, image.height, 4);
							if (img.SetData(image.image.data()))
							{
								success = pTex->Create(pContext, img, srgb);
							}
						}
						else
						{
							success = pTex->Create(pContext, Paths::Combine(Paths::GetDirectoryPath(pFilePath), image.uri).c_str(), srgb);
						}
						if (success)
						{
							m_Textures.push_back(std::move(pTex));
							textureMap[pathHash] = m_Textures.back().get();
							*pTarget = m_Textures.back().get();
						}
					}
					else
					{
						*pTarget = it->second;
					}
				}
			}
		};

		RetrieveTexture(baseColorTexture != gltfMaterial.values.end(), baseColorTexture, &material.pDiffuseTexture, true);
		RetrieveTexture(normalTexture != gltfMaterial.additionalValues.end(), normalTexture, &material.pNormalTexture, false);
		RetrieveTexture(emissiveTexture != gltfMaterial.additionalValues.end(), emissiveTexture, &material.pEmissiveTexture, true);
		RetrieveTexture(metallicRoughnessTexture != gltfMaterial.values.end(), metallicRoughnessTexture, &material.pRoughnessMetalnessTexture, false);

		if (baseColorFactor != gltfMaterial.values.end())
		{
			material.BaseColorFactor.x = (float)baseColorFactor->second.ColorFactor()[0];
			material.BaseColorFactor.y = (float)baseColorFactor->second.ColorFactor()[1];
			material.BaseColorFactor.z = (float)baseColorFactor->second.ColorFactor()[2];
			material.BaseColorFactor.w = (float)baseColorFactor->second.ColorFactor()[3];
		}
		if (emissiveFactor != gltfMaterial.additionalValues.end())
		{
			material.EmissiveFactor.x = (float)emissiveFactor->second.ColorFactor()[0];
			material.EmissiveFactor.y = (float)emissiveFactor->second.ColorFactor()[1];
			material.EmissiveFactor.z = (float)emissiveFactor->second.ColorFactor()[2];
		}
		if (roughnessFactor != gltfMaterial.values.end())
		{
			material.RoughnessFactor = (float)roughnessFactor->second.Factor();
		}
		if (metallicFactor != gltfMaterial.values.end())
		{
			material.MetalnessFactor = (float)metallicFactor->second.Factor();
		}
		if (alphaMode != gltfMaterial.additionalValues.end())
		{
			material.IsTransparent = alphaMode->second.string_value != "OPAQUE";
		}
		if (alphaCutoff != gltfMaterial.additionalValues.end())
		{
			material.AlphaCutoff = (float)alphaMode->second.Factor();
		}
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
	std::vector<std::vector<int>> meshToPrimitives;
	int primitiveIndex = 0;

	for (const tinygltf::Mesh& mesh : model.meshes)
	{
		std::vector<int> primtives;
		for (const tinygltf::Primitive& primitive : mesh.primitives)
		{
			primtives.push_back(primitiveIndex++);
			MeshData meshData;
			uint32 vertexOffset = (uint32)vertices.size();
			meshData.VertexOffset = vertexOffset;

			// Indices
			const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
			const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
			const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

			int indexStride = indexAccessor.ByteStride(indexBufferView);
			size_t indexCount = indexAccessor.count;
			uint32 indexOffset = (uint32)indices.size();
			indices.resize(indices.size() + indexCount);

			meshData.IndexOffset = indexOffset;
			meshData.NumIndices = (uint32)indexCount;

			const unsigned char* indexData = indexBuffer.data.data() + indexAccessor.byteOffset + indexBufferView.byteOffset;

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

			meshData.MaterialIndex = primitive.material;

			for (const auto& attribute : primitive.attributes)
			{
				const std::string name = attribute.first;
				int index = attribute.second;

				const tinygltf::Accessor& accessor = model.accessors[index];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
				int stride = accessor.ByteStride(bufferView);
				const unsigned char* data = buffer.data.data() + accessor.byteOffset + bufferView.byteOffset;

				if (meshData.NumVertices == 0)
				{
					vertices.resize(vertices.size() + accessor.count);
					meshData.NumVertices = (uint32)accessor.count;
				}

				if (name == "POSITION")
				{
					check(stride == sizeof(Vector3));
					for (size_t i = 0; i < accessor.count; ++i)
					{
						vertices[i + vertexOffset].Position = ((Vector3*)data)[i];
					}
				}
				else if (name == "NORMAL")
				{
					check(stride == sizeof(Vector3));
					for (size_t i = 0; i < accessor.count; ++i)
					{
						vertices[i + vertexOffset].Normal = ((Vector3*)data)[i];
					}
				}
				else if (name == "TANGENT")
				{
					check(stride == sizeof(Vector4));
					for (size_t i = 0; i < accessor.count; ++i)
					{
						vertices[i + vertexOffset].Tangent = Vector3(((Vector4*)data)[i]);
						float sign = ((Vector4*)data)[i].w;
						vertices[i + vertexOffset].Bitangent = Vector3(sign, sign, sign);
					}
				}
				else if (name == "TEXCOORD_0")
				{
					check(stride == sizeof(Vector2));
					for (size_t i = 0; i < accessor.count; ++i)
					{
						vertices[i + vertexOffset].TexCoord = ((Vector2*)data)[i];
					}
				}
				else
				{

				}
			}
			meshDatas.push_back(meshData);
		}
		meshToPrimitives.push_back(primtives);
	}

	// Generate bitangents
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

	uint64 dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext](void* pSource, uint64 size)
	{
		m_pGeometryData->SetData(pContext, pSource, size, dataOffset);
		dataOffset += size;
		dataOffset = Math::AlignUp<uint64>(dataOffset, sBufferAlignment);
	};

	const tinygltf::Scene& gltfScene = model.scenes[Math::Max(0, model.defaultScene)];
	for (size_t i = 0; i < gltfScene.nodes.size(); i++)
	{
		int nodeIdx = gltfScene.nodes[i];
		struct QueuedNode
		{
			int Index;
			Matrix Transform;
		};
		std::queue<QueuedNode> toProcess;
		QueuedNode rootNode;
		rootNode.Index = nodeIdx;
		rootNode.Transform = Matrix::CreateScale(uniformScale);
		toProcess.push(rootNode);
		while (!toProcess.empty())
		{
			QueuedNode currentNode = toProcess.front();
			toProcess.pop();
			const tinygltf::Node& node = model.nodes[currentNode.Index];
			Vector3 scale = node.scale.size() == 0 ? Vector3::One : Vector3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
			Quaternion rotation = node.rotation.size() == 0 ? Quaternion::Identity : Quaternion((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
			Vector3 translation = node.translation.size() == 0 ? Vector3::Zero : Vector3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);

			Matrix matrix = node.matrix.size() == 0 ? Matrix::Identity : Matrix(
				(float)node.matrix[0], (float)node.matrix[1], (float)node.matrix[2], (float)node.matrix[3],
				(float)node.matrix[4], (float)node.matrix[5], (float)node.matrix[6], (float)node.matrix[7],
				(float)node.matrix[8], (float)node.matrix[9], (float)node.matrix[10], (float)node.matrix[11],
				(float)node.matrix[12], (float)node.matrix[13], (float)node.matrix[14], (float)node.matrix[15]
			);

			SubMeshInstance newNode;
			newNode.Transform = currentNode.Transform * matrix * Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation) * Matrix::CreateTranslation(translation);
			if (node.mesh >= 0)
			{
				for (int primitive : meshToPrimitives[node.mesh])
				{
					newNode.MeshIndex = primitive;
					m_MeshInstances.push_back(newNode);
				}
			}

			for (int child : node.children)
			{
				QueuedNode q;
				q.Index = child;
				q.Transform = newNode.Transform;
				toProcess.push(q);
			}
		}
	}

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
