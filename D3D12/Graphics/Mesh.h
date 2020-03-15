#pragma once
class Buffer;
class CommandContext;
class Texture;
class Graphics;
class CommandContext;
struct aiMesh;

class SubMesh
{
	friend class Mesh;

public:
	~SubMesh();

	void Draw(CommandContext* pContext) const;
	int GetMaterialId() const { return m_MaterialId; }
	const BoundingBox& GetBounds() const { return m_Bounds; }

	Buffer* GetVertexBuffer() const { return m_pVertexBuffer.get(); }
	Buffer* GetIndexBuffer() const { return m_pIndexBuffer.get(); }

private:
	int m_MaterialId = 0;
	int m_IndexCount = 0;
	int m_VertexCount = 0;
	BoundingBox m_Bounds;
	std::unique_ptr<Buffer> m_pVertexBuffer;
	std::unique_ptr<Buffer> m_pIndexBuffer;
};

struct Material
{
	std::unique_ptr<Texture> pDiffuseTexture;
	std::unique_ptr<Texture> pNormalTexture;
	std::unique_ptr<Texture> pSpecularTexture;
	std::unique_ptr<Texture> pAlphaTexture;
	bool IsTransparent;
};

class Mesh
{
public:
	bool Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext);
	int GetMeshCount() const { return (int)m_Meshes.size(); }
	SubMesh* GetMesh(const int index) const { return m_Meshes[index].get(); }
	const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }

private:
	std::unique_ptr<SubMesh> LoadMesh(aiMesh* pMesh, Graphics* pGraphics, CommandContext* pContext);

	std::vector<std::unique_ptr<SubMesh>> m_Meshes;
	std::vector<Material> m_Materials;
};