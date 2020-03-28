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

	uint32 GetVertexByteOffset() const { return m_VertexByteOffset; }
	uint32 GetIndexByteOffset() const { return m_IndexByteOffset; }
	uint32 GetVertexCount() const { return m_VertexCount; }
	uint32 GetIndexCount() const { return m_IndexCount; }

private:
	int m_MaterialId = 0;
	uint32 m_IndexCount = 0;
	uint32 m_VertexCount = 0;
	uint32 m_IndexOffset = 0;
	uint32 m_VertexOffset = 0;
	uint32 m_VertexByteOffset = 0;
	uint32 m_IndexByteOffset = 0;
	BoundingBox m_Bounds;
	Mesh* m_pParent;
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

	Buffer* GetVertexBuffer() const { return m_pVertexBuffer.get(); }
	Buffer* GetIndexBuffer() const { return m_pIndexBuffer.get(); }

private:
	std::vector<std::unique_ptr<SubMesh>> m_Meshes;
	std::vector<Material> m_Materials;
	std::unique_ptr<Buffer> m_pVertexBuffer;
	std::unique_ptr<Buffer> m_pIndexBuffer;
};