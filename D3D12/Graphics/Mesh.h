#pragma once
#include "Core/GraphicsBuffer.h"
class Buffer;
class CommandContext;
class Texture;
class Graphics;
class CommandContext;
class ShaderResourceView;
class Mesh;

struct SubMesh
{
	void Draw(CommandContext* pContext) const;
	void Destroy();

	int Stride = 0;
	int MaterialId = 0;
	ShaderResourceView* pVertexSRV = nullptr;
	ShaderResourceView* pIndexSRV = nullptr;
	VertexBufferView VerticesLocation;
	IndexBufferView IndicesLocation;
	BoundingBox Bounds;
	Mesh* pParent = nullptr;
};

struct Material
{
	Texture* pDiffuseTexture = nullptr;
	Texture* pNormalTexture = nullptr;
	Texture* pRoughnessTexture = nullptr;
	Texture* pMetallicTexture = nullptr;
	bool IsTransparent;
};

class Mesh
{
public:
	~Mesh();
	bool Load(const char* pFilePath, Graphics* pGraphics, CommandContext* pContext);
	int GetMeshCount() const { return (int)m_Meshes.size(); }
	const SubMesh& GetMesh(const int index) const { return m_Meshes[index]; }
	const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }

	Buffer* GetBLAS() const { return m_pBLAS.get(); }
	Buffer* GetData() const { return m_pGeometryData.get(); }

private:
	std::vector<Material> m_Materials;
	std::unique_ptr<Buffer> m_pGeometryData;
	std::vector<SubMesh> m_Meshes;
	std::vector<std::unique_ptr<Texture>> m_Textures;
	std::map<StringHash, Texture*> m_ExistingTextures;
	std::unique_ptr<Buffer> m_pBLAS;
	std::unique_ptr<Buffer> m_pBLASScratch;
};
