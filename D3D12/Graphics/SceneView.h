#pragma once
#include "Core/BitField.h"
#include "Core/ShaderInterop.h"

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
	float JitterWeight = 0.5f;
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
	ShaderInterop::InstanceData InstanceData;
	Blending BlendMode = Blending::Opaque;
	const SubMesh* pMesh = nullptr;
	Matrix WorldMatrix;
	BoundingBox LocalBounds;
	BoundingBox Bounds;
	float Radius;
};
DECLARE_BITMASK_TYPE(Batch::Blending)

using VisibilityMask = BitField<8192>;

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
	RefCountPtr<Buffer> pLightBuffer;
	RefCountPtr<Buffer> pMaterialBuffer;
	RefCountPtr<Buffer> pMeshBuffer;
	RefCountPtr<Buffer> pMeshInstanceBuffer;
	RefCountPtr<Buffer> pSceneTLAS;
	RefCountPtr<Buffer> pTransformsBuffer;
	RefCountPtr<Texture> pSky;
	int FrameIndex = 0;
	VisibilityMask VisibilityMask;
	ShadowData ShadowData;
	ViewTransform View;
};

struct SceneTextures
{
	RefCountPtr<Texture> pColorTarget;
	RefCountPtr<Texture> pNormalsTarget;
	RefCountPtr<Texture> pDepth;
	RefCountPtr<Texture> pAmbientOcclusion;
	RefCountPtr<Texture> pPreviousColorTarget;
};

void DrawScene(CommandContext& context, const SceneView& scene, const VisibilityMask& visibility, Batch::Blending blendModes);
void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes);
ShaderInterop::ViewUniforms GetViewUniforms(const SceneView& sceneView, Texture* pTarget = nullptr);
