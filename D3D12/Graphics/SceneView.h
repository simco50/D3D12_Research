#pragma once

#include "Graphics/RHI/RHI.h"
#include "Core/BitField.h"
#include "ShaderInterop.h"
#include "AccelerationStructure.h"
#include "RenderGraph/RenderGraphDefinitions.h"
#include "Techniques/ShaderDebugRenderer.h"
#include "Techniques/DDGI.h"

class Mesh;
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
	Vector3 Position;
	Vector3 PositionPrev;

	FloatRect Viewport;
	float FoV = 60.0f * Math::PI / 180;
	float NearPlane = 1.0f;
	float FarPlane = 500.0f;
	int JitterIndex = 0;
	Vector2 Jitter;
	Vector2 JitterPrev;

	bool IsPerspective = true;
	BoundingFrustum PerspectiveFrustum;
	OrientedBoundingBox OrthographicFrustum;

	bool IsInFrustum(const BoundingBox& bb) const
	{
		return IsPerspective ? PerspectiveFrustum.Contains(bb) : OrthographicFrustum.Contains(bb);
	}
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
	std::string DebugName;
	ViewTransform View;
	VisibilityMask Visibility;
	Texture* pDepthTexture = nullptr;
};

struct SceneView
{
	const World* pWorld = nullptr;
	std::vector<Batch> Batches;
	RefCountPtr<Buffer> pLightBuffer;
	RefCountPtr<Buffer> pMaterialBuffer;
	RefCountPtr<Buffer> pMeshBuffer;
	RefCountPtr<Buffer> pInstanceBuffer;
	RefCountPtr<Buffer> pDDGIVolumesBuffer;
	RefCountPtr<Texture> pSky;
	AccelerationStructure AccelerationStructure;
	GPUDebugRenderData DebugRenderData;
	Vector2u HZBDimensions;

	VisibilityMask VisibilityMask;
	ViewTransform MainView;
	BoundingBox SceneAABB;

	std::vector<ShadowView> ShadowViews;
	Vector4 ShadowCascadeDepths;
	uint32 NumShadowCascades;

	uint32 NumLights = 0;
	uint32 NumDDGIVolumes = 0;
	int FrameIndex = 0;
	bool CameraCut = false;

	Vector2u GetDimensions() const
	{
		return Vector2u((uint32)MainView.Viewport.GetWidth(), (uint32)MainView.Viewport.GetHeight());
	}
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
	ShaderInterop::ViewUniforms GetViewUniforms(const SceneView* pView, const ViewTransform* pViewTransform, Texture* pTarget = nullptr);
	ShaderInterop::ViewUniforms GetViewUniforms(const SceneView* pView, Texture* pTarget = nullptr);
	void UploadSceneData(CommandContext& context, SceneView* pView, const World* pWorld);
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
	CheckerPattern,
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
