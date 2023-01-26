#pragma once
#include "RHI/Buffer.h"

class Buffer;
class CommandContext;
class Texture;
class CommandContext;
class ShaderResourceView;
class Mesh;
struct World;

struct SubMesh
{
	int MaterialId = 0;

	ResourceFormat PositionsFormat = ResourceFormat::RGB32_FLOAT;
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

	RefCountPtr<Buffer> pBLAS;
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
	float MetalnessFactor = 0.0f;
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
	Span<SubMeshInstance> GetMeshInstances() const { return m_MeshInstances; }
	Span<SubMesh> GetMeshes() const { return m_Meshes; }
	Span<Material> GetMaterials() { return m_Materials; }
	Buffer* GetData() const { return m_pGeometryData; }

private:
	std::vector<Material> m_Materials;
	RefCountPtr<Buffer> m_pGeometryData;
	std::vector<SubMesh> m_Meshes;
	std::vector<SubMeshInstance> m_MeshInstances;
	std::vector<RefCountPtr<Texture>> m_Textures;
};
