#pragma once
#include "Core/Buffer.h"

class Buffer;
class CommandContext;
class Texture;
class CommandContext;
class ShaderResourceView;
class Mesh;

struct SubMesh
{
	void Destroy();

	int PositionsStride = 0;
	int MaterialId = 0;
	DXGI_FORMAT PositionsFormat = DXGI_FORMAT_R32G32B32_FLOAT;

	VertexBufferView PositionStreamLocation;
	VertexBufferView UVStreamLocation;
	VertexBufferView NormalStreamLocation;
	VertexBufferView ColorsStreamLocation;
	IndexBufferView IndicesLocation;
	uint32 MeshletsLocation;
	uint32 MeshletVerticesLocation;
	uint32 MeshletTrianglesLocation;
	uint32 MeshletBoundsLocation;
	uint32 NumMeshlets;

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

enum class MaterialAlphaMode
{
	Opaque,
	Masked,
	Blend,
};

struct Material
{
	std::string Name = "Unnamed Material";
	Color BaseColorFactor = Color(1, 1, 1, 1);
	Color EmissiveFactor = Color(0, 0, 0, 1);
	float MetalnessFactor = 1.0f;
	float RoughnessFactor = 1.0f;
	float AlphaCutoff = 0.5f;
	Texture* pDiffuseTexture = nullptr;
	Texture* pNormalTexture = nullptr;
	Texture* pRoughnessMetalnessTexture = nullptr;
	Texture* pEmissiveTexture = nullptr;
	MaterialAlphaMode AlphaMode;
};

class Mesh
{
public:
	~Mesh();
	bool Load(const char* pFilePath, GraphicsDevice* pDevice, CommandContext* pContext, float scale = 1.0f);
	int GetMeshCount() const { return (int)m_Meshes.size(); }
	SubMesh& GetMesh(const int index) { return m_Meshes[index]; }
	const Material& GetMaterial(int materialId) const { return m_Materials[materialId]; }
	const std::vector<SubMeshInstance>& GetMeshInstances() const { return m_MeshInstances; }
	const std::vector<SubMesh>& GetMeshes() const { return m_Meshes; }
	std::vector<Material>& GetMaterials() { return m_Materials; }
	Buffer* GetData() const { return m_pGeometryData.get(); }

private:
	std::vector<Material> m_Materials;
	std::unique_ptr<Buffer> m_pGeometryData;
	std::vector<SubMesh> m_Meshes;
	std::vector<SubMeshInstance> m_MeshInstances;
	std::vector<std::unique_ptr<Texture>> m_Textures;
};
