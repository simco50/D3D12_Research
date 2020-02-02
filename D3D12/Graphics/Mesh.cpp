#include "stdafx.h"
#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "Texture.h"
#include "GraphicsBuffer.h"

bool Mesh::Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext)
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
	};

	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate
		| aiProcess_ConvertToLeftHanded
		| aiProcess_CalcTangentSpace
		| aiProcess_GenUVCoords
	);

	for (uint32 i = 0; i < pScene->mNumMeshes; ++i)
	{
		m_Meshes.push_back(LoadMesh(pScene->mMeshes[i], pGraphics, pContext));
		pContext->ExecuteAndReset(true);
	}

	std::string dirPath = pFilePath;
	dirPath = dirPath.substr(0, dirPath.rfind('/'));

	auto loadTexture = [pGraphics, pContext](std::string basePath, aiMaterial* pMaterial, aiTextureType type) 
	{
		std::unique_ptr<Texture> pTex;
		aiString path;
		aiReturn ret = pMaterial->GetTexture(type, 0, &path);
		pTex = std::make_unique<Texture>();
		if (ret == aiReturn_SUCCESS)
		{
			std::string p = path.C_Str();
			std::stringstream str;
			str << basePath << p;
			pTex->Create(pGraphics, pContext, str.str().c_str());
		}
		else
		{
			switch (type)
			{
			case aiTextureType_NORMALS:
				pTex->Create(pGraphics, pContext, "Resources/textures/dummy_ddn.png");
				break;
			case aiTextureType_SPECULAR:
				pTex->Create(pGraphics, pContext, "Resources/textures/dummy_specular.png");
				break;
			case aiTextureType_DIFFUSE:
			default:
				pTex->Create(pGraphics, pContext, "Resources/textures/dummy.png");
				break;
			}
		}
		return pTex;
	};

	m_Materials.resize(pScene->mNumMaterials);
	for (uint32 i = 0; i < pScene->mNumMaterials; ++i)
	{
		Material& m = m_Materials[i];
		m.pDiffuseTexture = loadTexture(dirPath, pScene->mMaterials[i], aiTextureType_DIFFUSE);
		m.pNormalTexture = loadTexture(dirPath, pScene->mMaterials[i], aiTextureType_NORMALS);
		m.pSpecularTexture = loadTexture(dirPath, pScene->mMaterials[i], aiTextureType_SPECULAR);
		aiString p;
		m.IsTransparent = pScene->mMaterials[i]->GetTexture(aiTextureType_OPACITY, 0, &p) == aiReturn_SUCCESS;
		pContext->ExecuteAndReset(true);
	}

	return true;
}

std::unique_ptr<SubMesh> Mesh::LoadMesh(aiMesh* pMesh, Graphics* pGraphics, CommandContext* pContext)
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
		Vector3 Tangent;
		Vector3 Bitangent;
	};

	std::vector<Vertex> vertices(pMesh->mNumVertices);
	std::vector<uint32> indices(pMesh->mNumFaces * 3);

	for (uint32 j = 0; j < pMesh->mNumVertices; ++j)
	{
		Vertex& vertex = vertices[j];
		vertex.Position = *reinterpret_cast<Vector3*>(&pMesh->mVertices[j]);
		if(pMesh->HasTextureCoords(0))
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


	std::unique_ptr<SubMesh> pSubMesh = std::make_unique<SubMesh>();
	BoundingBox::CreateFromPoints(pSubMesh->m_Bounds, vertices.size(), (XMFLOAT3*)&vertices[0], sizeof(Vertex));

	{
		uint32 size = (uint32)vertices.size() * sizeof(Vertex);
		pSubMesh->m_pVertexBuffer = std::make_unique<Buffer>();
		pSubMesh->m_pVertexBuffer->Create(pGraphics, BufferDesc::CreateVertexBuffer((uint32)vertices.size(), sizeof(Vertex)));
		pSubMesh->m_pVertexBuffer->SetData(pContext, vertices.data(), size);
	}

	{
		uint32 size = (uint32)indices.size() * sizeof(uint32);
		pSubMesh->m_IndexCount = (int)indices.size();
		pSubMesh->m_pIndexBuffer = std::make_unique<Buffer>();
		pSubMesh->m_pIndexBuffer->Create(pGraphics, BufferDesc::CreateIndexBuffer((uint32)size, false));
		pSubMesh->m_pIndexBuffer->SetData(pContext, indices.data(), size);
	}
	pSubMesh->m_MaterialId = pMesh->mMaterialIndex;

	return pSubMesh;
}

SubMesh::~SubMesh()
{

}

void SubMesh::Draw(CommandContext* pContext) const
{
	pContext->SetIndexBuffer(m_pIndexBuffer.get());
	pContext->SetVertexBuffer(m_pVertexBuffer.get());
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}
