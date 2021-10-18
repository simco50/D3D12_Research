#pragma once
#include "Core/DescriptorHandle.h"
#include "Core/BitField.h"
#include "Core/ShaderInterop.h"

class Texture;
class Buffer;
class Camera;
class CommandContext;
struct SubMesh;

struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};
	int Index = 0;
	Blending BlendMode = Blending::Opaque;
	const SubMesh* pMesh = nullptr;
	Matrix WorldMatrix;
	BoundingBox LocalBounds;
	BoundingBox Bounds;
	float Radius;
	int Material;
};
DECLARE_BITMASK_TYPE(Batch::Blending)

using VisibilityMask = BitField<2048>;

struct SceneView
{
	Texture* pResolvedDepth = nullptr;
	Texture* pDepthBuffer = nullptr;
	Texture* pRenderTarget = nullptr;
	Texture* pResolvedTarget = nullptr;
	Texture* pPreviousColor = nullptr;
	Texture* pNormals = nullptr;
	Texture* pResolvedNormals = nullptr;
	Texture* pAO = nullptr;
	std::vector<Batch> Batches;
	Buffer* pLightBuffer = nullptr;
	Buffer* pMaterialBuffer = nullptr;
	Buffer* pMeshBuffer = nullptr;
	Buffer* pMeshInstanceBuffer = nullptr;
	Camera* pCamera = nullptr;
	ShaderInterop::ShadowData* pShadowData = nullptr;
	int SceneTLAS = 0;
	int FrameIndex = 0;
	VisibilityMask VisibilityMask;
};

void DrawScene(CommandContext& context, const SceneView& scene, const VisibilityMask& visibility, Batch::Blending blendModes);
void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes);
