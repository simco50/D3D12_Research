#pragma once
class GraphicsBuffer;
class GraphicsCommandContext;
class Texture2D;
class Graphics;
struct aiMesh;

class SubMesh
{
	friend class Mesh;

public:
	void Draw(GraphicsCommandContext* pContext);
	int GetMaterialId() const { return m_MaterialId; }

private:
	int m_MaterialId = 0;
	int m_IndexCount = 0;
	int m_VertexCount = 0;
	std::unique_ptr<GraphicsBuffer> m_pVertexBuffer;
	std::unique_ptr<GraphicsBuffer> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
};

struct Material
{
	std::unique_ptr<Texture2D> pDiffuseTexture;
	std::unique_ptr<Texture2D> pNormalTexture;
};

class Mesh
{
public:
	bool Load(const char* pFilePath, Graphics* pGraphics, GraphicsCommandContext* pContext);
	int GetMeshCount() const { return (int)m_Meshes.size(); }
	SubMesh* GetMesh(const int index) const { return m_Meshes[index].get(); }
	const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }

private:
	std::unique_ptr<SubMesh> LoadMesh(aiMesh* pMesh, ID3D12Device* pDevice, GraphicsCommandContext* pContext);

	std::vector<std::unique_ptr<SubMesh>> m_Meshes;
	std::vector<Material> m_Materials;
};