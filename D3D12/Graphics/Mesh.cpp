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

bool Mesh::Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext)
{
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate
		| aiProcess_ConvertToLeftHanded
		| aiProcess_CalcTangentSpace
		| aiProcess_GenUVCoords
	);

	uint32 vertexCount = 0;
	uint32 indexCount = 0;
	for (uint32 i = 0; i < pScene->mNumMeshes; ++i)
	{
		vertexCount += pScene->mMeshes[i]->mNumVertices;
		indexCount += pScene->mMeshes[i]->mNumFaces * 3;
	}

	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
		Vector3 Tangent;
		Vector3 Bitangent;
	};

	m_pGeometryData = std::make_unique<Buffer>(pGraphics, "Mesh VertexBuffer");
	m_pGeometryData->Create(BufferDesc::CreateBuffer(vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32)));

	uint32 dataOffset = 0;
	for (uint32 i = 0; i < pScene->mNumMeshes; ++i)
	{
		const aiMesh* pMesh = pScene->mMeshes[i];
		std::unique_ptr<SubMesh> pSubMesh = std::make_unique<SubMesh>();
		std::vector<Vertex> vertices(pMesh->mNumVertices);
		std::vector<uint32> indices(pMesh->mNumFaces * 3);

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

		for (uint32 j = 0; j < pMesh->mNumFaces; ++j)
		{
			const aiFace& face = pMesh->mFaces[j];
			for (uint32 k = 0; k < 3; ++k)
			{
				check(face.mNumIndices == 3);
				indices[j * 3 + k] = face.mIndices[k];
			}
		}

		BoundingBox::CreateFromPoints(pSubMesh->m_Bounds, vertices.size(), (Vector3*)&vertices[0], sizeof(Vertex));
		pSubMesh->m_MaterialId = pMesh->mMaterialIndex;
		pSubMesh->m_VertexCount = (uint32)vertices.size();
		pSubMesh->m_VerticesLocation = m_pGeometryData->GetGpuHandle() + dataOffset;
		m_pGeometryData->SetData(pContext, vertices.data(), sizeof(Vertex) * vertices.size(), dataOffset);
		dataOffset += (uint32)vertices.size() * sizeof(Vertex);
		pContext->FlushResourceBarriers();

		pSubMesh->m_IndexCount = (uint32)indices.size();
		pSubMesh->m_IndicesLocation = m_pGeometryData->GetGpuHandle() + dataOffset;
		m_pGeometryData->SetData(pContext, indices.data(), sizeof(uint32) * indices.size(), dataOffset);
		dataOffset += (uint32)indices.size() * sizeof(uint32);
		pContext->FlushResourceBarriers();

		pSubMesh->m_Stride = sizeof(Vertex);
		pSubMesh->m_pParent = this;

		m_Meshes.push_back(std::move(pSubMesh));
	}

	std::string dirPath = Paths::GetDirectoryPath(pFilePath);

	m_Textures.push_back(std::make_unique<Texture>(pGraphics, "Dummy Texture"));
	m_Textures.back()->Create(pContext, "Resources/Textures/dummy.dds", true);
	m_Textures.push_back(std::make_unique<Texture>(pGraphics, "Dummy Texture"));
	m_Textures.back()->Create(pContext, "Resources/Textures/dummy_ddn.dds", true);
	m_Textures.push_back(std::make_unique<Texture>(pGraphics, "Dummy Texture"));
	m_Textures.back()->Create(pContext, "Resources/Textures/dummy_specular.dds", true);

	auto loadTexture = [this, pGraphics, pContext](const char* basePath, aiMaterial* pMaterial, aiTextureType type, bool srgb)
	{
		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, 0, &path);
		bool success = ret == aiReturn_SUCCESS;
		std::string pathStr = path.C_Str();
		if (!success)
		{
			switch (type)
			{
			case aiTextureType_NORMALS:		return m_Textures[1].get();
			case aiTextureType_SPECULAR:	return m_Textures[2].get();
			default:						return m_Textures[0].get();
			}
		}
		StringHash pathHash = StringHash(pathStr.c_str());
		auto it = m_ExistingTextures.find(pathHash);
		if (it != m_ExistingTextures.end())
		{
			return it->second;
		}
		std::unique_ptr<Texture> pTex;
		pTex = std::make_unique<Texture>(pGraphics, pathStr.c_str());
		std::stringstream str;
		str << basePath << pathStr;
		success = pTex->Create(pContext, str.str().c_str(), srgb);
		if (success)
		{
			E_LOG(Info, "Loading '%s'", pathStr.c_str());
			m_Textures.push_back(std::move(pTex));
			m_ExistingTextures[pathHash] = m_Textures.back().get();
			return m_Textures.back().get();
		}
		return (Texture*)nullptr;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32 i = 0; i < pScene->mNumMaterials; ++i)
	{
		Material& m = m_Materials[i];
		m.pDiffuseTexture = loadTexture(dirPath.c_str(), pScene->mMaterials[i], aiTextureType_DIFFUSE, true);
		m.pNormalTexture = loadTexture(dirPath.c_str(), pScene->mMaterials[i], aiTextureType_NORMALS, false);
		m.pSpecularTexture = loadTexture(dirPath.c_str(), pScene->mMaterials[i], aiTextureType_SPECULAR, false);
		aiString p;
		m.IsTransparent = pScene->mMaterials[i]->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
	}
	return true;
}

SubMesh::~SubMesh()
{

}

void SubMesh::Draw(CommandContext* pContext) const
{
	pContext->SetIndexBuffer(IndexBufferView(m_IndicesLocation, m_IndexCount, false));
	pContext->SetVertexBuffer(VertexBufferView(m_VerticesLocation, m_VertexCount, m_Stride));
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}
