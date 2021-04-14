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
	void Destroy();

	int Stride = 0;
	int MaterialId = 0;
	ShaderResourceView* pVertexSRV = nullptr;
	ShaderResourceView* pIndexSRV = nullptr;
	VertexBufferView VerticesLocation;
	IndexBufferView IndicesLocation;
	BoundingBox Bounds;
	Mesh* pParent = nullptr;

	Buffer* pBLAS = nullptr;
	Buffer* pBLASScratch = nullptr;
};

struct SubMeshInstance
{
	int MeshIndex;
	Matrix Transform;
};

struct Material
{
	Texture* pDiffuseTexture = nullptr;
	Texture* pNormalTexture = nullptr;
	Texture* pRoughnessMetalnessTexture = nullptr;
	Texture* pEmissiveTexture = nullptr;
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
	const std::vector<SubMeshInstance>& GetMeshInstances() const { return m_MeshInstances; }

	Buffer* GetData() const { return m_pGeometryData.get(); }

private:
	std::vector<Material> m_Materials;
	std::unique_ptr<Buffer> m_pGeometryData;
	std::vector<SubMesh> m_Meshes;
	std::vector<SubMeshInstance> m_MeshInstances;
	std::vector<std::unique_ptr<Texture>> m_Textures;
};
