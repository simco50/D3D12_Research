#pragma once
#include "Core/DescriptorHandle.h"
#include "Core/BitField.h"
#include "Core/ShaderInterop.h"
#include "Light.h"

class Texture;
class Buffer;
class CommandContext;
struct SubMesh;

struct ViewTransform
{
	Matrix Projection;
	Matrix View;
	Matrix ViewProjection;
	Matrix ViewInverse;
	Matrix ProjectionInverse;
	Matrix PreviousViewProjection;
	bool Perspective = true;
	Vector3 Position;

	FloatRect Viewport;
	float FoV = 60.0f * Math::PI / 180;
	float NearPlane = 1.0f;
	float FarPlane = 500.0f;
	float OrthographicSize = 1;
	int JitterIndex = 0;
	float JitterWeight = 1.0f;
	Vector2 Jitter;
	Vector2 PreviousJitter;
	BoundingFrustum Frustum;
};

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
};
DECLARE_BITMASK_TYPE(Batch::Blending)

using VisibilityMask = BitField<2048>;

struct ShadowData
{
	Matrix LightViewProjections[MAX_SHADOW_CASTERS];
	Vector4 CascadeDepths;
	uint32 NumCascades;
	uint32 ShadowMapOffset;
};

struct SceneView
{
	std::vector<Batch> Batches;
	Buffer* pLightBuffer = nullptr;
	Buffer* pMaterialBuffer = nullptr;
	Buffer* pMeshBuffer = nullptr;
	Buffer* pMeshInstanceBuffer = nullptr;
	Buffer* pSceneTLAS = nullptr;
	int FrameIndex = 0;
	VisibilityMask VisibilityMask;
	ShadowData ShadowData;
	ViewTransform View;
};

void DrawScene(CommandContext& context, const SceneView& scene, const VisibilityMask& visibility, Batch::Blending blendModes);
void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes);
ShaderInterop::ViewUniforms GetViewUniforms(const SceneView& sceneView, Texture* pTarget = nullptr);
