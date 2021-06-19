#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Core/Paths.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/pbrmaterial.h"

Mesh::~Mesh()
{
	for (SubMesh& subMesh : m_Meshes)
	{
		subMesh.Destroy();
	}
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pDevice, CommandContext* pContext, float scale /*= 1.0f*/)
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
		Vector3 Tangent;
		Vector3 Bitangent;
	};

	Assimp::Importer importer;
	importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, scale);
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate
		| aiProcess_GlobalScale
		| aiProcess_ConvertToLeftHanded
		| aiProcess_CalcTangentSpace
		| aiProcess_GenUVCoords
	);

	if (!pScene)
	{
		E_LOG(Warning, "Failed to load Mesh '%s'", pFilePath);
		return false;
	}

	uint32 vertexCount = 0;
	uint32 indexCount = 0;
	for (uint32 i = 0; i < pScene->mNumMeshes; ++i)
	{
		vertexCount += pScene->mMeshes[i]->mNumVertices;
		indexCount += pScene->mMeshes[i]->mNumFaces * 3;
	}

	// SubAllocated buffers need to be 16 byte aligned
	static constexpr uint64 sBufferAlignment = 16;
	uint64 bufferSize = vertexCount * sizeof(Vertex) +
		indexCount * sizeof(uint32) + pScene->mNumMeshes * sBufferAlignment;

	m_pGeometryData = pDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Mesh VertexBuffer");

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COPY_DEST);

	uint64 dataOffset = 0;
	auto CopyData = [this, &dataOffset, &pContext](void* pSource, uint64 size)
	{
		m_pGeometryData->SetData(pContext, pSource, size, dataOffset);
		dataOffset += size;
		dataOffset = Math::AlignUp<uint64>(dataOffset, sBufferAlignment);
	};

	std::queue<aiNode*> nodesToProcess;
	nodesToProcess.push(pScene->mRootNode);
	while (!nodesToProcess.empty())
	{
		aiNode* pNode = nodesToProcess.front();
		nodesToProcess.pop();
		SubMeshInstance newNode;
		aiMatrix4x4 t = pNode->mTransformation;
		if (pNode->mParent)
		{
			t = pNode->mParent->mTransformation * t;
		}
		newNode.Transform = Matrix(&t.a1).Transpose();
		for (uint32 i = 0; i < pNode->mNumMeshes; ++i)
		{
			newNode.MeshIndex = pNode->mMeshes[i];
			m_MeshInstances.push_back(newNode);
		}
		for (uint32 i = 0; i < pNode->mNumChildren; ++i)
		{
			nodesToProcess.push(pNode->mChildren[i]);
		}
	}

	for (uint32 i = 0; i < pScene->mNumMeshes; ++i)
	{
		const aiMesh* pMesh = pScene->mMeshes[i];
		std::vector<Vertex> vertices(pMesh->mNumVertices);

		for (uint32 j = 0; j < pMesh->mNumVertices; ++j)
		{
			Vertex& vertex = vertices[j];
			vertex.Position = *reinterpret_cast<Vector3*>(&pMesh->mVertices[j]);
			if (pMesh->HasTextureCoords(0))
				vertex.TexCoord = *reinterpret_cast<Vector2*>(&pMesh->mTextureCoords[0][j]);
			vertex.Normal = *reinterpret_cast<Vector3*>(&pMesh->mNormals[j]);
			if (pMesh->HasTangentsAndBitangents())
			{
				vertex.Tangent = *reinterpret_cast<Vector3*>(&pMesh->mTangents[j]);
				vertex.Bitangent = *reinterpret_cast<Vector3*>(&pMesh->mBitangents[j]);
			}
		}

		std::vector<uint32> indices(pMesh->mNumFaces * 3);
		for (uint32 j = 0; j < pMesh->mNumFaces; ++j)
		{
			const aiFace& face = pMesh->mFaces[j];
			for (uint32 k = 0; k < 3; ++k)
			{
				check(face.mNumIndices == 3);
				indices[j * 3 + k] = face.mIndices[k];
			}
		}

		SubMesh subMesh;
		BoundingBox::CreateFromPoints(subMesh.Bounds, vertices.size(), (Vector3*)&vertices[0], sizeof(Vertex));
		subMesh.MaterialId = pMesh->mMaterialIndex;

		VertexBufferView vbv(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)vertices.size(), sizeof(Vertex));
		subMesh.VerticesLocation = vbv;
		subMesh.pVertexSRV = new ShaderResourceView();
		subMesh.pVertexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32)dataOffset, (uint32)vertices.size() * sizeof(Vertex)));
		CopyData(vertices.data(), sizeof(Vertex) * vertices.size());

		IndexBufferView ibv(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)indices.size(), false);
		subMesh.IndicesLocation = ibv;
		subMesh.pIndexSRV = new ShaderResourceView();
		subMesh.pIndexSRV->Create(m_pGeometryData.get(), BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true, (uint32)dataOffset, (uint32)indices.size() * sizeof(uint32)));
		CopyData(indices.data(), sizeof(uint32) * indices.size());

		subMesh.Stride = sizeof(Vertex);
		subMesh.pParent = this;
		m_Meshes.push_back(subMesh);
	}

	pContext->InsertResourceBarrier(m_pGeometryData.get(), D3D12_RESOURCE_STATE_COMMON);
	pContext->FlushResourceBarriers();

	std::string dirPath = Paths::GetDirectoryPath(pFilePath);

	std::map<StringHash, Texture*> textureMap;

	auto loadTexture = [this, pDevice, pContext, &textureMap](const char* basePath, const aiMaterial* pMaterial, aiTextureType type, int index, bool srgb)
	{
		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, index, &path);
		bool success = ret == aiReturn_SUCCESS;
		std::string pathStr = path.C_Str();
		if (success)
		{
			StringHash pathHash = StringHash(pathStr.c_str());
			auto it = textureMap.find(pathHash);
			if (it != textureMap.end())
			{
				return it->second;
			}
			std::unique_ptr<Texture> pTex;
			pTex = std::make_unique<Texture>(pDevice, pathStr.c_str());
			std::string str;
			str += basePath + pathStr;
			success = pTex->Create(pContext, str.c_str(), srgb);
			if (success)
			{
				m_Textures.push_back(std::move(pTex));
				textureMap[pathHash] = m_Textures.back().get();
				return m_Textures.back().get();
			}
		}
		return (Texture*)nullptr;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32 i = 0; i < pScene->mNumMaterials; ++i)
	{
		const aiMaterial* pSceneMaterial = pScene->mMaterials[i];
		Material& m = m_Materials[i];
		m.pDiffuseTexture = loadTexture(dirPath.c_str(), pSceneMaterial, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, true);
		m.pNormalTexture = loadTexture(dirPath.c_str(), pSceneMaterial, aiTextureType_NORMALS, 0, false);
		m.pRoughnessMetalnessTexture = loadTexture(dirPath.c_str(), pSceneMaterial, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, false);
		m.pEmissiveTexture = loadTexture(dirPath.c_str(), pSceneMaterial, aiTextureType_EMISSIVE, 0, false);
		
		if (!m.pDiffuseTexture)
		{
			m.pDiffuseTexture = loadTexture(dirPath.c_str(), pSceneMaterial, aiTextureType_DIFFUSE, 0, true);
		}

		aiString alphaMode;
		if (pSceneMaterial->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS)
		{
			m.IsTransparent = strcmp(alphaMode.C_Str(), "OPAQUE") != 0;
		}
		else
		{
			aiString p;
			m.IsTransparent = pScene->mMaterials[i]->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
		}

		uint32 max = 4;
		aiReturn result = pSceneMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, &m.BaseColorFactor.x, &max);
		result = pSceneMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, m.MetalnessFactor);
		result = pSceneMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, m.RoughnessFactor);
	}

	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		ID3D12GraphicsCommandList4* pCmd = pContext->GetRaytracingCommandList();

		//Bottom Level Acceleration Structure
		{
			for (int i = 0; i < GetMeshCount(); ++i)
			{
				SubMesh& subMesh = m_Meshes[i];
				const Material& material = m_Materials[subMesh.MaterialId];
				D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
				geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
				if (material.IsTransparent == false)
				{
					geometryDesc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
				}
				geometryDesc.Triangles.IndexBuffer = subMesh.IndicesLocation.Location;
				geometryDesc.Triangles.IndexCount = subMesh.IndicesLocation.Elements;
				geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexBuffer.StartAddress = subMesh.VerticesLocation.Location;
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = subMesh.VerticesLocation.Stride;
				geometryDesc.Triangles.VertexCount = subMesh.VerticesLocation.Elements;
				geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
				prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				prebuildInfo.Flags =
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
					| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
				prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				prebuildInfo.NumDescs = 1;
				prebuildInfo.pGeometryDescs = &geometryDesc;

				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
				pDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

				std::unique_ptr<Buffer> pBLASScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess), "BLAS Scratch Buffer");
				std::unique_ptr<Buffer> pBLAS = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::AccelerationStructure), "BLAS Buffer");

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
				asDesc.Inputs = prebuildInfo;
				asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
				asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
				asDesc.SourceAccelerationStructureData = 0;

				pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
				pContext->InsertUavBarrier(subMesh.pBLAS);
				pContext->FlushResourceBarriers();

				subMesh.pBLAS = pBLAS.release();
				if (0) //#todo: Can delete scratch buffer if no upload is required
				{
					subMesh.pBLASScratch = pBLASScratch.release();
				}
			}
		}
	}

	return true;
}

void SubMesh::Destroy()
{
	delete pIndexSRV;
	delete pVertexSRV;

	delete pBLAS;
	delete pBLASScratch;
}
