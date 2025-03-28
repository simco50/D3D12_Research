#pragma once
#include "RHI/RHI.h"
#include "Core/BitField.h"
#include "RenderGraph/RenderGraphDefinitions.h"
#include <RHI/DescriptorHandle.h>

#include "ShaderInterop.h"

class Renderer;
struct World;
struct Material;
struct Mesh;
struct Light;
class Image;

enum class StencilBit : uint8
{
	None = 0,

	VisibilityBuffer = 1 << 0,
	Terrain = 1 << 1,

	SurfaceTypeMask = VisibilityBuffer | Terrain,
};
DECLARE_BITMASK_TYPE(StencilBit);

struct ViewTransform
{
	Matrix ViewToClip;
	Matrix WorldToView;
	Matrix WorldToClip;
	Matrix WorldToClipPrev;
	Matrix ViewToWorld;
	Matrix ClipToView;
	Matrix ViewToClipUnjittered;
	Matrix WorldToClipUnjittered;

	Vector3 Position;
	Vector3 PositionPrev;

	FloatRect Viewport;
	float FoV = 60.0f * Math::PI / 180;
	float NearPlane = 100.0f;
	float FarPlane = 0.1f;
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

	Vector2u GetDimensions() const
	{
		return Vector2u((uint32)Viewport.GetWidth(), (uint32)Viewport.GetHeight());
	}
};

using VisibilityMask = BitField<8192>;


struct RenderView : ViewTransform
{
	enum class Type
	{
		Default,
		Cull,
	};

	Renderer* pRenderer = nullptr;
	World* pWorld = nullptr;

	VisibilityMask VisibilityMask;

	Ref<Buffer> ViewCB;
	Ref<Buffer> CullViewCB;

	bool RequestFreezeCull = false;
	bool FreezeCull = false;

	bool CameraCut = false;
};


struct ShadowView : RenderView
{
	const Light* pLight = nullptr;
	uint32 ViewIndex = 0;
	Texture* pDepthTexture = nullptr;
};


struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};

	uint32				InstanceID;
	const Mesh* pMesh;
	const Material* pMaterial;
	Matrix				WorldMatrix;
	BoundingBox			Bounds;
	float				Radius;

	Blending			BlendMode = Blending::Opaque;
};
DECLARE_BITMASK_TYPE(Batch::Blending)


struct SceneTextures
{
	RGTexture* pPreviousColor = nullptr;
	RGTexture* pRoughness = nullptr;
	RGTexture* pColorTarget = nullptr;
	RGTexture* pDepth = nullptr;
	RGTexture* pNormals = nullptr;
	RGTexture* pVelocity = nullptr;

	RGTexture* pGBuffer = nullptr;
};


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

struct BindingSlot
{
	static constexpr uint32 PerInstance = 0;
	static constexpr uint32 PerPass = 1;
	static constexpr uint32 PerView = 2;
	static constexpr uint32 UAV = 3;
	static constexpr uint32 SRV = 4;
};

namespace GraphicsCommon
{
	void Create(GraphicsDevice* pDevice);
	void Destroy();

	Texture* GetDefaultTexture(DefaultTexture type);

	extern Ref<CommandSignature> pIndirectDrawSignature;
	extern Ref<CommandSignature> pIndirectDrawIndexedSignature;
	extern Ref<CommandSignature> pIndirectDispatchSignature;
	extern Ref<CommandSignature> pIndirectDispatchMeshSignature;
	extern Ref<RootSignature> pCommonRS;
	extern Ref<RootSignature> pCommonRSWithIA;

	Ref<Texture> CreateTextureFromImage(GraphicsDevice* pDevice, const Image& image, bool sRGB, const char* pName = nullptr);
	Ref<Texture> CreateTextureFromFile(GraphicsDevice* pDevice, const char* pFilePath, bool sRGB, const char* pName = nullptr);
}

enum class RenderPath
{
	Tiled,
	Clustered,
	PathTracing,
	Visibility,
	VisibilityDeferred,
	MAX
};
