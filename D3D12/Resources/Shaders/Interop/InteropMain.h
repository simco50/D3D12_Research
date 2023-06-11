#pragma once

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
	uint : 32;
	float3 LocalBoundsExtents;
	uint ID;
	uint MaterialIndex;
	uint MeshIndex;
	PAD;
	PAD;
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
	float3 GetColor() { return unpack_u8u32(Color).rgb / 255.0f; }
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
	PAD;
};

struct ViewUniforms
{
	float4 CascadeDepths;
	uint NumCascades;
	PAD;
	PAD;
	PAD;

	float4x4 View;
	float4x4 ViewInverse;
	float4x4 Projection;
	float4x4 ProjectionInverse;
	float4x4 ViewProjection;
	float4x4 ViewProjectionPrev;
	float4x4 ViewProjectionInverse;
	float4x4 ReprojectionMatrix;
	float3 ViewLocation;
	PAD;
	float3 ViewLocationPrev;
	PAD;
	float4 FrustumPlanes[6];
	float2 TargetDimensions;
	float2 TargetDimensionsInv;
	float2 ViewportDimensions;
	float2 ViewportDimensionsInv;
	float2 ViewJitter;
	float2 ViewJitterPrev;
	float NearZ;
	float FarZ;
	float FoV;

	uint FrameIndex;
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
