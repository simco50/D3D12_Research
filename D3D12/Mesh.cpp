#include "stdafx.h"
#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "GraphicsResource.h"
#include "CommandContext.h"

bool Mesh::Load(const char* pFilePath, ID3D12Device* pDevice, CommandContext* pContext)
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
	};

	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(pFilePath,
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_LimitBoneWeights
	);

	std::vector<Vertex> vertices(pScene->mMeshes[0]->mNumVertices);
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Position = *reinterpret_cast<Vector3*>(&pScene->mMeshes[0]->mVertices[i]);
		vertex.TexCoord = *reinterpret_cast<Vector2*>(&pScene->mMeshes[0]->mTextureCoords[0][i]);
		vertex.Normal = *reinterpret_cast<Vector3*>(&pScene->mMeshes[0]->mNormals[i]);
	}

	std::vector<uint32> indices(pScene->mMeshes[0]->mNumFaces * 3);
	for (size_t i = 0; i < pScene->mMeshes[0]->mNumFaces; ++i)
	{
		for (size_t j = 0; j < 3; ++j)
		{
			assert(pScene->mMeshes[0]->mFaces[i].mNumIndices == 3);
			indices[i * 3 + j] = pScene->mMeshes[0]->mFaces[i].mIndices[j];
		}
	}

	{
		uint32 size = (uint32)vertices.size() * sizeof(Vertex);
		m_pVertexBuffer = std::make_unique<GraphicsBuffer>();
		m_pVertexBuffer->Create(pDevice, size, false);
		m_pVertexBuffer->SetData(pContext, vertices.data(), size);

		m_VertexBufferView.BufferLocation = m_pVertexBuffer->GetGpuHandle();
		m_VertexBufferView.SizeInBytes = sizeof(Vertex) * (uint32)vertices.size();
		m_VertexBufferView.StrideInBytes = sizeof(Vertex);
	}

	{
		uint32 size = (uint32)indices.size() * sizeof(uint32);
		m_IndexCount = (int)indices.size();
		m_pIndexBuffer = std::make_unique<GraphicsBuffer>();
		m_pIndexBuffer->Create(pDevice, size, false);
		m_pIndexBuffer->SetData(pContext, indices.data(), size);

		m_IndexBufferView.BufferLocation = m_pIndexBuffer->GetGpuHandle();
		m_IndexBufferView.SizeInBytes = size;
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}
	return true;
}

void Mesh::Draw(CommandContext* pContext)
{
	pContext->SetIndexBuffer(m_IndexBufferView);
	pContext->SetVertexBuffer(m_VertexBufferView);
	pContext->DrawIndexed(m_IndexCount, 0, 0);
}
