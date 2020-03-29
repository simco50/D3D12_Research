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

	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
		Vector3 Tangent;
		Vector3 Bitangent;
	};

	uint32 vertexCount = 0;
	uint32 indexCount = 0;
	for (uint32 i = 0; i < pScene->mNumMeshes; ++i)
	{
		vertexCount += pScene->mMeshes[i]->mNumVertices;
		indexCount += pScene->mMeshes[i]->mNumFaces * 3;
	}

	m_pVertexBuffer = std::make_unique<Buffer>(pGraphics);
	m_pVertexBuffer->Create(BufferDesc::CreateVertexBuffer(vertexCount, sizeof(Vertex)));
	m_pIndexBuffer = std::make_unique<Buffer>(pGraphics);
	m_pIndexBuffer->Create(BufferDesc::CreateIndexBuffer(indexCount, false));

	uint32 vertexOffset = 0;
	uint32 indexOffset = 0;
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
				assert(face.mNumIndices == 3);
				indices[j * 3 + k] = face.mIndices[k];
			}
		}

		BoundingBox::CreateFromPoints(pSubMesh->m_Bounds, vertices.size(), (Vector3*)&vertices[0], sizeof(Vertex));
		pSubMesh->m_MaterialId = pMesh->mMaterialIndex;
		pSubMesh->m_VertexCount = (uint32)vertices.size();
		pSubMesh->m_IndexCount = (uint32)indices.size();
		pSubMesh->m_VertexOffset = vertexOffset;
		pSubMesh->m_IndexOffset = indexOffset;
		pSubMesh->m_VertexByteOffset = vertexOffset * sizeof(Vertex);
		pSubMesh->m_IndexByteOffset = indexOffset * sizeof(uint32);
		pSubMesh->m_pParent = this;

		m_pVertexBuffer->SetData(pContext, vertices.data(), sizeof(Vertex)* vertices.size(), vertexOffset * sizeof(Vertex));
		m_pIndexBuffer->SetData(pContext, indices.data(), sizeof(uint32)* indices.size(), indexOffset * sizeof(uint32));
		vertexOffset += (uint32)vertices.size();
		indexOffset += (uint32)indices.size();

		m_Meshes.push_back(std::move(pSubMesh));
	}

	std::string dirPath = Paths::GetDirectoryPath(pFilePath);

	auto loadTexture = [pGraphics, pContext](const char* basePath, aiMaterial* pMaterial, aiTextureType type, bool srgb)
	{
		std::unique_ptr<Texture> pTex;
		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, 0, &path);
		pTex = std::make_unique<Texture>(pGraphics);
		bool success = ret == aiReturn_SUCCESS;
		if (success)
		{
			std::string p = path.C_Str();
			std::stringstream str;
			str << basePath << "/" << p;
			success = pTex->Create(pContext, str.str().c_str(), srgb);
		}
		if(!success)
		{
			switch (type)
			{
			case aiTextureType_NORMALS:
				pTex->Create(pContext, "Resources/textures/dummy_ddn.png", srgb);
				break;
			case aiTextureType_SPECULAR:
				pTex->Create(pContext, "Resources/textures/dummy_specular.png", srgb);
				break;
			case aiTextureType_DIFFUSE:
			default:
				pTex->Create(pContext, "Resources/textures/dummy.png", srgb);
				break;
			}
		}
		return pTex;
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
	pContext->SetIndexBuffer(m_pParent->GetIndexBuffer());
	pContext->SetVertexBuffer(m_pParent->GetVertexBuffer());
	pContext->DrawIndexed(m_IndexCount, m_IndexOffset, m_VertexOffset);
}
