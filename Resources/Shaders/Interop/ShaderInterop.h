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
#endif

#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )
#define PAD uint MACRO_CONCAT(padding, __COUNTER__)

static const int MESHLET_MAX_TRIANGLES = 124;
static const int MESHLET_MAX_VERTICES = 64;

// Per material shader data
struct MaterialData
{
	uint Diffuse;
	uint Normal;
	uint RoughnessMetalness;
	uint Emissive;
	float4 BaseColorFactor;
	float4 EmissiveFactor;
	float MetalnessFactor;
	float RoughnessFactor;
	float AlphaCutoff;
	uint RasterBin;
};

struct MeshData
{
	uint BufferIndex;
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

	uint ShadowMapIndex;
	uint MatrixIndex;
	uint MaskTexture;

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
	uint IrradianceIndex;
	uint DepthIndex;
	uint ProbeOffsetIndex;
	uint ProbeStatesIndex;
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
	uint pad0;
	float3 ViewLocationPrev;
	uint pad1;

	float2 ViewportDimensions;
	float2 ViewportDimensionsInv;
	float2 ViewJitter;
	float2 ViewJitterPrev;

	float NearZ;
	float FarZ;
	float FoV;
	uint pad2;

	float4 CascadeDepths;
	uint NumCascades;
	uint FrameIndex;
	float DeltaTime;
	uint NumInstances;

	uint SsrSamples;
	uint LightCount;
	uint NumDDGIVolumes;

	uint InstancesIndex;
	uint MeshesIndex;
	uint MaterialsIndex;
	uint LightsIndex;
	uint LightMatricesIndex;
	uint SkyIndex;
	uint DDGIVolumesIndex;
	uint TLASIndex;

	uint DebugRenderDataIndex;
	uint FontDataIndex;
	uint FontSize;
};

#ifdef __cplusplus
}
#endif