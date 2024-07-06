#pragma once

#include "Graphics/RHI/RHI.h"
#include "Core/BitField.h"
#include "ShaderInterop.h"
#include "AccelerationStructure.h"
#include "RenderGraph/RenderGraphDefinitions.h"
#include "Techniques/ShaderDebugRenderer.h"

#include "entt.hpp"

class LoadedScene;
struct Mesh;
class Image;
struct Material;
struct Light;

enum class StencilBit : uint8
{
	None				= 0,

	VisibilityBuffer	= 1 << 0,
	Terrain				= 1 << 1,

	SurfaceTypeMask		= VisibilityBuffer | Terrain,
};
DECLARE_BITMASK_TYPE(StencilBit);

struct Transform
{
	Vector3 Position	= Vector3::Zero;
	Quaternion Rotation = Quaternion::Identity;
	Vector3 Scale		= Vector3::One;

	Matrix World		= Matrix::Identity;
};

struct Identity
{
	String Name;
};

struct World
{
	entt::entity CreateEntity(const char* pName)
	{
		entt::entity e = Registry.create();
		Registry.emplace<Identity>(e, pName);
		return e;
	}

	Array<Ref<Texture>> Textures;
	Array<Mesh> Meshes;
	Array<Material> Materials;

	entt::registry Registry;
	entt::entity Sunlight;
};

struct ViewTransform
{
	Matrix Projection;
	Matrix View;
	Matrix ViewProjection;
	Matrix ViewProjectionPrev;
	Matrix ViewInverse;
	Matrix ProjectionInverse;
	Matrix UnjtteredViewProjection;
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
	const Mesh* pMesh;
	Matrix WorldMatrix;
	BoundingBox Bounds;
	float Radius;
};
DECLARE_BITMASK_TYPE(Batch::Blending)

using VisibilityMask = BitField<8192>;

struct ShadowView
{
	const Light* pLight = nullptr;
	uint32 ViewIndex = 0;
	ViewTransform View;
	VisibilityMask Visibility;
	Texture* pDepthTexture = nullptr;
};

struct SceneView
{
	const World* pWorld = nullptr;
	Array<Batch> Batches;

	struct SceneBuffer
	{
		uint32 Count;
		Ref<Buffer> pBuffer;
	};

	SceneBuffer LightBuffer;
	SceneBuffer MaterialBuffer;
	SceneBuffer MeshBuffer;
	SceneBuffer InstanceBuffer;
	SceneBuffer DDGIVolumesBuffer;
	SceneBuffer FogVolumesBuffer;
	SceneBuffer LightMatricesBuffer;
	Ref<Texture> pSky;
	AccelerationStructure AccelerationStructure;
	GPUDebugRenderData DebugRenderData;
	Vector2u HZBDimensions;

	VisibilityMask VisibilityMask;
	ViewTransform MainView;
	BoundingBox SceneAABB;

	Array<ShadowView> ShadowViews;
	Vector4 ShadowCascadeDepths;
	uint32 NumShadowCascades;

	int FrameIndex = 0;
	bool CameraCut = false;

	Vector2u GetDimensions() const
	{
		return Vector2u((uint32)MainView.Viewport.GetWidth(), (uint32)MainView.Viewport.GetHeight());
	}
};

struct SceneTextures
{
	RGTexture* pPreviousColor		= nullptr;
	RGTexture* pRoughness			= nullptr;
	RGTexture* pColorTarget			= nullptr;
	RGTexture* pDepth				= nullptr;
	RGTexture* pNormals				= nullptr;
	RGTexture* pVelocity			= nullptr;

	RGTexture* pGBuffer0			= nullptr;
	RGTexture* pGBuffer1			= nullptr;
};

namespace Renderer
{
	void DrawScene(CommandContext& context, Span<Batch> batches, const VisibilityMask& visibility, Batch::Blending blendModes);
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
	constexpr static ResourceFormat DepthStencilFormat = ResourceFormat::D24S8;
	constexpr static ResourceFormat GBufferFormat[] = {
		ResourceFormat::RGBA16_FLOAT,
		ResourceFormat::RG16_SNORM,
		ResourceFormat::R8_UNORM
	};
	constexpr static ResourceFormat DeferredGBufferFormat[] = {
			ResourceFormat::RGBA8_UNORM,
			ResourceFormat::RGB10A2_UNORM,
	};

	extern Ref<CommandSignature> pIndirectDrawSignature;
	extern Ref<CommandSignature> pIndirectDrawIndexedSignature;
	extern Ref<CommandSignature> pIndirectDispatchSignature;
	extern Ref<CommandSignature> pIndirectDispatchMeshSignature;
	extern Ref<RootSignature> pCommonRS;

	Ref<Texture> CreateTextureFromImage(GraphicsDevice* pDevice, const Image& image, bool sRGB, const char* pName = nullptr);
	Ref<Texture> CreateTextureFromFile(GraphicsDevice* pDevice, const char* pFilePath, bool sRGB, const char* pName = nullptr);
}
