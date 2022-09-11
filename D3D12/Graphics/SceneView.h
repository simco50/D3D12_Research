#pragma once
#include "Core/BitField.h"
#include "ShaderInterop.h"
#include "AccelerationStructure.h"
#include "RenderGraph/RenderGraphDefinitions.h"

class Texture;
class Buffer;
class Mesh;
class CommandContext;
class ShaderResourceView;
class CommandSignature;
class GraphicsDevice;
class Image;
struct SubMesh;
struct Light;

struct DDGIVolume
{
	Vector3 Origin;
	Vector3 Extents;
	Vector3i NumProbes;
	int32 MaxNumRays;
	int32 NumRays;
	RefCountPtr<Texture> pIrradianceHistory;
	RefCountPtr<Texture> pDepthHistory;
	RefCountPtr<Buffer> pProbeOffset;
	RefCountPtr<Buffer> pProbeStates;
};

struct World
{
	std::vector<Light> Lights;
	std::vector<std::unique_ptr<Mesh>> Meshes;
	std::vector<DDGIVolume> DDGIVolumes;
};

struct ViewTransform
{
	Matrix Projection;
	Matrix View;
	Matrix ViewProjection;
	Matrix ViewProjectionPrev;
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
	SubMesh* pMesh = nullptr;
	Matrix WorldMatrix;
	BoundingBox Bounds;
	float Radius;
};
DECLARE_BITMASK_TYPE(Batch::Blending)

using VisibilityMask = BitField<8192>;

struct ShadowView
{
	Matrix ViewProjection;
	bool IsPerspective;
	Texture* pDepthTexture = nullptr;
	OrientedBoundingBox OrtographicFrustum;
	BoundingFrustum PerspectiveFrustum;
	VisibilityMask Visibility;
};

struct SceneView
{
	std::vector<Batch> Batches;
	RefCountPtr<Buffer> pLightBuffer;
	RefCountPtr<Buffer> pMaterialBuffer;
	RefCountPtr<Buffer> pMeshBuffer;
	RefCountPtr<Buffer> pMeshInstanceBuffer;
	RefCountPtr<Buffer> pTransformsBuffer;
	RefCountPtr<Buffer> pDDGIVolumesBuffer;
	uint32 NumDDGIVolumes = 0;
	RefCountPtr<Texture> pSky;
	int FrameIndex = 0;
	Vector2 HZBDimensions;
	VisibilityMask VisibilityMask;
	ViewTransform View;
	BoundingBox SceneAABB;
	AccelerationStructure AccelerationStructure;

	std::vector<ShadowView> ShadowViews;
	Vector4 ShadowCascadeDepths;
	uint32 NumShadowCascades;

	Vector2i GetDimensions() const;
};

struct SceneTextures
{
	RGTexture* pVisibilityBuffer;
	RGTexture* pPreviousColor;
	RGTexture* pRoughness;
	RGTexture* pColorTarget;
	RGTexture* pDepth;
	RGTexture* pResolvedDepth;
	RGTexture* pNormals;
	RGTexture* pVelocity;
	RGTexture* pAmbientOcclusion;
	RGTexture* pHZB;
};

namespace Renderer
{
	void DrawScene(CommandContext& context, const SceneView* pView, const VisibilityMask& visibility, Batch::Blending blendModes);
	void DrawScene(CommandContext& context, const SceneView* pView, Batch::Blending blendModes);
	ShaderInterop::ViewUniforms GetViewUniforms(const SceneView* pView, Texture* pTarget = nullptr);
	void UploadSceneData(CommandContext& context, SceneView* pView, World* pWorld);
}

enum class DefaultTexture
{
	White2D,
	Black2D,
	Magenta2D,
	Gray2D,
	Normal2D,
	RoughnessMetalness,
	BlackCube,
	Black3D,
	ColorNoise256,
	BlueNoise512,
	MAX,
};

namespace GraphicsCommon
{
	void Create(GraphicsDevice* pDevice);
	void Destroy();

	Texture* GetDefaultTexture(DefaultTexture type);

	extern RefCountPtr<CommandSignature> pIndirectDrawSignature;
	extern RefCountPtr<CommandSignature> pIndirectDispatchSignature;
	extern RefCountPtr<CommandSignature> pIndirectDispatchMeshSignature;

	RefCountPtr<Texture> CreateTextureFromImage(CommandContext& context, Image& image, bool sRGB, const char* pName = nullptr);
	RefCountPtr<Texture> CreateTextureFromFile(CommandContext& context, const char* pFilePath, bool sRGB, const char* pName = nullptr);
}
