#pragma once
#include "Core/BitField.h"
#include "ShaderInterop.h"
#include "AccelerationStructure.h"
#include "RenderGraph/RenderGraphDefinitions.h"
#include "Techniques/ShaderDebugRenderer.h"
#include "Techniques/DDGI.h"

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
	bool Perspective = true;
	Vector3 Position;
	Vector3 PositionPrev;

	FloatRect Viewport;
	float FoV = 60.0f * Math::PI / 180;
	float NearPlane = 1.0f;
	float FarPlane = 500.0f;
	float OrthographicSize = 1;
	int JitterIndex = 0;
	Vector2 Jitter;
	Vector2 JitterPrev;
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
	uint32 InstanceID;
	Blending BlendMode = Blending::Opaque;
	SubMesh* pMesh;
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
	RefCountPtr<Buffer> pInstanceBuffer;
	RefCountPtr<Buffer> pDDGIVolumesBuffer;
	uint32 NumDDGIVolumes = 0;
	RefCountPtr<Texture> pSky;
	int FrameIndex = 0;
	Vector2u HZBDimensions;
	VisibilityMask VisibilityMask;
	ViewTransform View;
	BoundingBox SceneAABB;
	AccelerationStructure AccelerationStructure;
	GPUDebugRenderData DebugRenderData;

	std::vector<ShadowView> ShadowViews;
	Vector4 ShadowCascadeDepths;
	uint32 NumShadowCascades;
	uint32 NumLights;

	Vector2u GetDimensions() const;
};

struct SceneTextures
{
	RGTexture* pPreviousColor = nullptr;
	RGTexture* pRoughness = nullptr;
	RGTexture* pColorTarget = nullptr;
	RGTexture* pDepth = nullptr;
	RGTexture* pResolvedDepth = nullptr;
	RGTexture* pNormals = nullptr;
	RGTexture* pVelocity = nullptr;
	RGTexture* pAmbientOcclusion = nullptr;
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

	constexpr static ResourceFormat ShadowFormat = ResourceFormat::D16_UNORM;
	constexpr static ResourceFormat DepthStencilFormat = ResourceFormat::D32_FLOAT;

	extern RefCountPtr<CommandSignature> pIndirectDrawSignature;
	extern RefCountPtr<CommandSignature> pIndirectDrawIndexedSignature;
	extern RefCountPtr<CommandSignature> pIndirectDispatchSignature;
	extern RefCountPtr<CommandSignature> pIndirectDispatchMeshSignature;

	RefCountPtr<Texture> CreateTextureFromImage(CommandContext& context, const Image& image, bool sRGB, const char* pName = nullptr);
	RefCountPtr<Texture> CreateTextureFromFile(CommandContext& context, const char* pFilePath, bool sRGB, const char* pName = nullptr);
}
