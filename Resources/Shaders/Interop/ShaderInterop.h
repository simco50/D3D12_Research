#pragma once

#ifdef __cplusplus
namespace ShaderInterop
{
using float2 = Vector2;
using float3 = Vector3;
using float4 = Vector4;
using uint = uint32;
using uint2 = Vector2u;
using uint3 = Vector3u;
using uint4 = Vector4u;
using int2 = Vector2i;
using int3 = Vector3i;
using int4 = Vector4i;
using float4x4 = Matrix;

template<typename ComponentType> using Texture1DH = TextureView;
template<typename ComponentType> using Texture2DH = TextureView;
template<typename ComponentType> using Texture3DH = TextureView;
template<typename ComponentType> using TextureCubeH = TextureView;
template<typename ComponentType> using RWTexture1DH = RWTextureView;
template<typename ComponentType> using RWTexture2DH = RWTextureView;
template<typename ComponentType> using RWTexture3DH = RWTextureView;

template<typename ComponentType> using StructuredBufferH = BufferView;
template<typename ComponentType> using RWStructuredBufferH = RWBufferView;
template<typename ComponentType> using TypedBufferH = BufferView;
template<typename ComponentType> using RWTypedBufferH = RWBufferView;
								using ByteBufferH = BufferView;
								using RWByteBufferH = RWBufferView;
								using TLASH = TLASView;
#endif

#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )
#define PAD uint MACRO_CONCAT(padding, __COUNTER__)

static const int MESHLET_MAX_TRIANGLES = 124;
static const int MESHLET_MAX_VERTICES = 64;

// Per material shader data
struct MaterialData
{
	Texture2DH<float4> Diffuse;
	Texture2DH<float3> Normal;
	Texture2DH<float4> RoughnessMetalness;
	Texture2DH<float4> Emissive;
	float4 BaseColorFactor;
	float4 EmissiveFactor;
	float MetalnessFactor;
	float RoughnessFactor;
	float AlphaCutoff;
	uint RasterBin;
};

struct MeshData
{
	ByteBufferH DataBuffer;
	uint PositionsOffset;
	uint UVsOffset;
	uint NormalsOffset;
	uint ColorsOffset;
	uint IndicesOffset;
	uint IndexByteSize;

	uint MeshletOffset;
	uint MeshletVertexOffset;
	uint MeshletTriangleOffset;
	uint MeshletBoundsOffset;
	uint MeshletCount;
};

struct Meshlet
{
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;

	struct Triangle
	{
		uint V0 : 10;
		uint V1 : 10;
		uint V2 : 10;
		uint : 2;
	};

	struct Bounds
	{
		float3 LocalCenter;
		float3 LocalExtents;
	};
};

struct InstanceData
{
	float4x4 LocalToWorld;
	float4x4 LocalToWorldPrev;
	float3 LocalBoundsOrigin;
	uint pad0;
	float3 LocalBoundsExtents;
	uint ID;
	uint MaterialIndex;
	uint MeshIndex;
	uint2 pad2;
};

struct Light
{
	float3 Position;
	uint Color;
	float3 Direction;
	float Intensity;
	float2 SpotlightAngles;
	float Range;
	float InvShadowSize;

	Texture2DH<float> ShadowMap;
	uint MatrixIndex;
	Texture2DH<float> MaskTexture;

	// flags
	uint IsEnabled : 1;
	uint IsSpot : 1;
	uint IsPoint: 1;
	uint IsDirectional : 1;
	uint IsVolumetric : 1;
	uint CastShadows : 1;

#ifndef __cplusplus
	float3 GetColor() { return unpack_u8u32(Color).rgb / 255.0f * Intensity; }
#endif
};

struct DDGIVolume
{
	float3 BoundsMin;
	uint NumRaysPerProbe;
	float3 ProbeSize;
	uint MaxRaysPerProbe;
	uint3 ProbeVolumeDimensions;
	Texture2DH<float4> IrradianceTexture;
	Texture2DH<float2> DepthTexture;
	TypedBufferH<float4> ProbeOffsetBuffer;
	TypedBufferH<float> ProbeStatesBuffer;
	uint pad0;
};

struct FogVolume
{
	float3 Location;
	float3 Extents;
	float3 Color;
	float DensityChange;
	float DensityBase;
};

struct Glyph
{
	float2 MinUV;
	float2 MaxUV;
	float2 Dimensions;
	float2 Offset;
	float AdvanceX;
};

struct ViewUniforms
{
	float4x4 WorldToView;
	float4x4 ViewToWorld;
	float4x4 ViewToClip;
	float4x4 ClipToView;
	float4x4 WorldToClip;
	float4x4 WorldToClipPrev;
	float4x4 ClipToWorld;
	float4x4 UVToPrevUV;
	float4x4 WorldToClipUnjittered;

	float3 ViewLocation;
	float3 ViewLocationPrev;

	float2 ViewportDimensions;
	float2 ViewportDimensionsInv;
	float2 ViewJitter;
	float2 ViewJitterPrev;

	float NearZ;
	float FarZ;
	float FoV;

	float4 CascadeDepths;
	uint NumCascades;
	uint FrameIndex;
	float DeltaTime;
	uint NumInstances;

	uint SsrSamples;
	uint LightCount;
	uint NumDDGIVolumes;

	StructuredBufferH<InstanceData> InstancesBuffer;
	StructuredBufferH<MeshData> MeshesBuffer;
	StructuredBufferH<MaterialData> MaterialsBuffer;
	StructuredBufferH<Light> LightsBuffer;
	StructuredBufferH<float4x4> LightMatricesBuffer;
	TextureCubeH<float4> SkyTexture;
	StructuredBufferH<DDGIVolume> DDGIVolumesBuffer;
	TLASH TLAS;

	RWByteBufferH DebugRenderData;
	StructuredBufferH<Glyph> FontData;
	uint FontSize;
};

#ifdef __cplusplus
}
#endif