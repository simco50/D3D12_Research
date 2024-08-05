#include "Common.hlsli"
#include "Random.hlsli"
#include "CBT.hlsli"
#include "Lighting.hlsli"
#include "D3D12.hlsli"
#include "HZB.hlsli"
#include "Noise.hlsli"
#include "RayTracing/DDGICommon.hlsli"

#define MESH_SHADER_THREAD_GROUP_SIZE 32
#define COMPUTE_THREAD_GROUP_SIZE 256

#ifndef UPDATE_SUBDIVISION
#define UPDATE_SUBDIVISION 1
#endif

#ifndef DEBUG_ALWAYS_SUBDIVIDE
#define DEBUG_ALWAYS_SUBDIVIDE 0
#endif

#ifndef FRUSTUM_CULL
#define FRUSTUM_CULL 1
#endif

#ifndef DISPLACEMENT_LOD
#define DISPLACEMENT_LOD 1
#endif

#ifndef DISTANCE_LOD
#define DISTANCE_LOD 1
#endif

struct CommonParams
{
	float HeightScale;
	float PlaneScale;
	uint NumCBTElements;
};
ConstantBuffer<CommonParams> cCommonParams : register(b1);

Texture2D<float> tDepth : register(t0);
Texture3D<float4> tFog : register(t1);

RWByteAddressBuffer uCBT : register(u0);
RWByteAddressBuffer uDispatchArgs : register(u1);

struct IndirectArgsParams
{
	uint NumCBTElements;
};
ConstantBuffer<IndirectArgsParams> cIndirectArgsParams : register(b0);

// Based on https://www.shadertoy.com/view/MdsSRs
float3 FBM_WithDerivatives(float2 x, float scale, float H, uint numOctaves)
{
	float G = exp2(-H);
	float f = 1.0;
    float t = 0.0;
    float a = 1.0;
    float2 d = 0.0;
    for(uint i = 0; i < numOctaves; i++)
    {
        float3 n = GradientNoise(f * x * scale);
        t += a * n.x;
        d += a * n.yz * f;
        f *= 2.0f;
        a *= G;
    }
	return float3(t, d * scale);
}

static const int TerrainOctaves = 16;

void GetTerrain(float2 worldPosition, uint octaves, out float height, out float3 normal)
{
	// Some random noise with a flat round in the middle
	float dist = saturate((length(worldPosition) - 14) / 16);
	float3 terrain = FBM_WithDerivatives(worldPosition + 100, 0.02f, 1.0f, octaves);
	terrain.x = terrain.x * 0.5f + 0.5f;
	terrain *= cCommonParams.HeightScale;
	height = lerp(0, terrain.x, dist) - 1;
	normal = lerp(float3(0, 1, 0), normalize(float3(-terrain.y, 1.0f, -terrain.z)), dist);
}

[numthreads(1, 1, 1)]
void PrepareDispatchArgsCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt = InitializeCBT(uCBT, cIndirectArgsParams.NumCBTElements);

	uint offset = 0;

	// Dispatch args
	{
		D3D12_DISPATCH_ARGUMENTS args;
		args.ThreadGroupCount = uint3(ceil((float)cbt.NumNodes() / COMPUTE_THREAD_GROUP_SIZE), 1, 1);
		uDispatchArgs.Store(offset, args);
		offset += sizeof(D3D12_DISPATCH_ARGUMENTS);
	}

	// Task/mesh shader args
	{
		D3D12_DISPATCH_ARGUMENTS args;
		args.ThreadGroupCount = uint3(ceil((float)cbt.NumNodes() / MESH_SHADER_THREAD_GROUP_SIZE), 1, 1);
		uDispatchArgs.Store(offset, args);
		offset += sizeof(D3D12_DISPATCH_ARGUMENTS);
	}

	// Draw args
	{
		D3D12_DRAW_ARGUMENTS args;
		args.VertexCountPerInstance = 3 << GEOMETRY_SUBD_LEVEL;
		args.InstanceCount = cbt.NumNodes();
		args.StartVertexLocation = 0;
		args.StartInstanceLocation = 0;
		uDispatchArgs.Store(offset, args);
		offset += sizeof(D3D12_DRAW_ARGUMENTS);
	}

	// Debug draw args
	{
		D3D12_DRAW_ARGUMENTS args;
		args.VertexCountPerInstance = 3;
		args.InstanceCount = cbt.NumNodes();
		args.StartVertexLocation = 0;
		args.StartInstanceLocation = 0;
		uDispatchArgs.Store(offset, args);
		offset += sizeof(D3D12_DRAW_ARGUMENTS);
	}
}

/* SUM REDUCTION ALGORITHM */

struct SumReductionParams
{
	uint Depth;
	uint NumCBTElements;
};
ConstantBuffer<SumReductionParams> cSumReductionParams : register(b0);

#if 1
groupshared uint gsSumCache[COMPUTE_THREAD_GROUP_SIZE];

// Num of nodes writes per thread
static const uint SUM_REDUCTION_LUT[] = {
	0,  32,	 16,  32,  8,  32,  16,  32,
	4,  32,  16,  32,  8,  32,  16,  32,
	2,  32,  16,  32,  8,  32,  16,  32,
	4,  32,  16,  32,  8,  32,  16,  32,
	1
};

[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void SumReductionCS(uint threadID : SV_DispatchThreadID, uint groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupID)
{
	CBT cbt = InitializeCBT(uCBT, cSumReductionParams.NumCBTElements);

	uint count = 1u << cSumReductionParams.Depth;
	uint index = threadID;
	if(index < count)
	{
		index += count;
		uint leftChild = cbt.GetNodeData(cbt.LeftChildIndex(index));
		uint rightChild = cbt.GetNodeData(cbt.RightChildIndex(index));
		gsSumCache[groupThreadID] = leftChild + rightChild;
	}

	GroupMemoryBarrierWithGroupSync();

	uint numThreadGroups = max(1, count / COMPUTE_THREAD_GROUP_SIZE);
	uint nodeSize = cbt.NodeBitSize(index);
	uint numNodesPerThread = SUM_REDUCTION_LUT[nodeSize];
	uint numNodesPerWrite = min(count / numThreadGroups, numNodesPerThread);
	uint numThreads = max(1, count / numThreadGroups / numNodesPerThread);
	uint numNodesPerGroup = numNodesPerWrite * numThreads;

	if(groupThreadID < numThreads)
	{
		uint nodeOffset = groupThreadID * numNodesPerWrite;
		uint groupOffset = groupIndex * numNodesPerGroup;
		uint baseNode = groupOffset + nodeOffset + count;
		uint baseBitIndex = cbt.NodeBitIndex(baseNode);

		for(uint i = 0; i < numNodesPerWrite; ++i)
		{
			uint value = gsSumCache[nodeOffset + i];
			uint bitIndex = baseBitIndex + i * nodeSize;
			CBT::DataMutateArgs args = cbt.GetDataArgs(bitIndex, nodeSize);

			cbt.BitfieldSet_Single_Lockless(args.ElementIndexLSB, args.ElementOffsetLSB, args.BitCountLSB, value);
			if(args.BitCountMSB > 0u)
				cbt.BitfieldSet_Single_Lockless(args.ElementIndexMSB, 0u, args.BitCountMSB, value >> args.BitCountLSB);
		}
	}
}

#else

[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void SumReductionCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt = InitializeCBT(uCBT, cSumReductionParams.NumCBTElements);

	uint count = 1u << cSumReductionParams.Depth;
	uint index = threadID;
	if(index < count)
	{
		index += count;
		uint leftChild = cbt.GetNodeData(cbt.LeftChildIndex(index));
		uint rightChild = cbt.GetNodeData(cbt.RightChildIndex(index));
		cbt.SetNodeData(index, leftChild + rightChild);
	}
}

#endif


[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void CacheBitfieldCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt = InitializeCBT(uCBT, cSumReductionParams.NumCBTElements);

	uint depth = cSumReductionParams.Depth;
	uint count = 1u << depth;
	uint elementCount = count >> 5u;
	if(threadID < elementCount)
	{
		uint nodeIndex = (threadID << 5u) + count;
		uint bitOffset = cbt.NodeBitIndex(nodeIndex);
		uint elementIndex = bitOffset >> 5u;
		uint v = cbt.Storage.Load(4 * elementIndex);
		// Cache the bitfield in the layer above it so that it's immutable during the subdivision pass
		cbt.Storage.Store(4 * (elementIndex - elementCount), v);
	}
}

/* SUBDIVISION ALGORITHM  */

struct UpdateParams
{
	float ScreenSizeBias;
	float HeightmapVarianceBias;
	uint SplitMode;
};
ConstantBuffer<UpdateParams> cUpdateParams : register(b0);

bool HeightmapFlatness(float3x3 tri)
{
	// Need some kind of derivatives of the normal here?
	return true;

	float2 center = (tri[0].xz + tri[1].xz + tri[2].xz) / 3.0f;
	float height;
	float3 normal;
	GetTerrain(center, TerrainOctaves, height, normal);
	return abs(height) >= cUpdateParams.HeightmapVarianceBias;
}

bool TriangleFrustumIntersect(float3x3 tri)
{
	float3 bmin = Min(tri[0], tri[1], tri[2]);
	float3 bmax = Max(tri[0], tri[1], tri[2]);
	AABB aabb = AABBFromMinMax(bmin, bmax);
	FrustumCullData cullData = FrustumCull(aabb.Center.xyz, aabb.Extents.xyz, IDENTITY_MATRIX_4, cView.WorldToClip);
	return cullData.IsVisible;
}

float2 TriangleLOD(float3x3 tri)
{
	float3 p0 = mul(float4(tri[0], 1), cView.WorldToView).xyz;
	float3 p2 = mul(float4(tri[2], 1), cView.WorldToView).xyz;

	float3 c = (p0 + p2) * 0.5f;
	float3 v = (p2 - p0);
	float distSq = dot(c, c);
	float lenSq = dot(v, v);

	return float2(cUpdateParams.ScreenSizeBias + log2(lenSq / distSq), 1.0f);
}

float2 GetLOD(float3x3 tri)
{
#if DEBUG_ALWAYS_SUBDIVIDE
	return 1;
#endif

#if FRUSTUM_CULL
	if(!TriangleFrustumIntersect(tri))
	{
		return float2(0, 0);
	}
#endif

#if DISPLACEMENT_LOD
	if(!HeightmapFlatness(tri))
	{
		return float2(0, 1);
	}
#endif

#if DISTANCE_LOD
	return TriangleLOD(tri);
#endif

	return float2(1, 1);
}

float3x3 GetVertices(uint heapIndex)
{
	float3x3 baseTriangle = float3x3(
		0, 0, 1,
		0, 0, 0,
		1, 0, 0
	);

	float3x3 tri = LEB::TransformAttributes(heapIndex, baseTriangle);
	for(int i = 0; i < 3; ++i)
	{
		float3 v = tri[i];
		v.xz = v.xz - 0.5f;
		v.xz *= cCommonParams.PlaneScale;
		float height;
		float3 normal;
		GetTerrain(v.xz, TerrainOctaves, height, normal);
		v.y += height;
		tri[i] = v;
	}
	return tri;
}

[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void UpdateCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt = InitializeCBT(uCBT, cCommonParams.NumCBTElements);

	if(threadID < cbt.NumNodes())
	{
		uint heapIndex = cbt.LeafToHeapIndex(threadID);

		float3x3 tri = GetVertices(heapIndex);
		float2 lod = GetLOD(tri);

		if(cUpdateParams.SplitMode == 1u)
		{
			if(lod.x >= 1.0f)
			{
				LEB::CBTSplitConformed(cbt, heapIndex);
			}
		}
		else
		{
			if(heapIndex > 1)
			{
				LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
				bool mergeTop = GetLOD(GetVertices(diamond.Top)).x < 1.0f;
				bool mergeBase = GetLOD(GetVertices(diamond.Base)).x < 1.0f;
				if(mergeTop && mergeBase)
				{
					LEB::CBTMergeConformed(cbt, heapIndex);
				}
			}
		}
	}
}

struct VertexOut
{
	float4 Position : SV_Position;
};

/* MESH SHADER TECHNIQUE */

// Must be a multiple of 2 to avoid cracks
// MS max number of triangles is 256. To solve this, execute more mesh shader groups from AS
#ifndef GEOMETRY_SUBD_LEVEL
#define GEOMETRY_SUBD_LEVEL 6
#endif

#ifndef AMPLIFICATION_SHADER_SUBD_LEVEL
#define AMPLIFICATION_SHADER_SUBD_LEVEL 0
#endif

#define NUM_MESH_SHADER_TRIANGLES (1u << GEOMETRY_SUBD_LEVEL)

struct ASPayload
{
	uint IDs[MESH_SHADER_THREAD_GROUP_SIZE];
};

groupshared ASPayload gsPayload;

[numthreads(MESH_SHADER_THREAD_GROUP_SIZE, 1, 1)]
void UpdateAS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt = InitializeCBT(uCBT, cCommonParams.NumCBTElements);

	bool isVisible = false;
	uint heapIndex = 0;

	if(threadID < cbt.NumNodes())
	{
		heapIndex = cbt.LeafToHeapIndex(threadID);

		float3x3 tri = GetVertices(heapIndex);
		float2 lod = GetLOD(tri);

#if UPDATE_SUBDIVISION
		if(cUpdateParams.SplitMode == 1u)
		{
			if(lod.x >= 1.0f)
			{
				LEB::CBTSplitConformed(cbt, heapIndex);
			}
		}
		else
		{
			if(heapIndex > 1)
			{
				LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
				bool mergeTop = GetLOD(GetVertices(diamond.Top)).x < 1.0f;
				bool mergeBase = GetLOD(GetVertices(diamond.Base)).x < 1.0f;
				if(mergeTop && mergeBase)
				{
					LEB::CBTMergeConformed(cbt, heapIndex);
				}
			}
		}
#endif

		isVisible = lod.y > 0.0f;
	}

	if(isVisible)
	{
		uint laneIndex = WavePrefixCountBits(isVisible);
		gsPayload.IDs[laneIndex] = heapIndex;
	}

	uint count = WaveActiveCountBits(isVisible);
	DispatchMesh((1u << AMPLIFICATION_SHADER_SUBD_LEVEL) * count, 1, 1, gsPayload);
}

[outputtopology("triangle")]
[numthreads(NUM_MESH_SHADER_TRIANGLES, 1, 1)]
void RenderMS(
	uint groupThreadID : SV_GroupThreadID,
	uint groupID : SV_GroupID,
	in payload ASPayload payload,
	out vertices VertexOut vertices[NUM_MESH_SHADER_TRIANGLES * 3],
	out indices uint3 triangles[NUM_MESH_SHADER_TRIANGLES])
{
	SetMeshOutputCounts(NUM_MESH_SHADER_TRIANGLES * 3, NUM_MESH_SHADER_TRIANGLES * 1);
	uint outputIndex = groupThreadID;
	uint heapIndex = payload.IDs[groupID / (1u << AMPLIFICATION_SHADER_SUBD_LEVEL)];
	uint subdHeapIndex = (((heapIndex << GEOMETRY_SUBD_LEVEL) | outputIndex) << AMPLIFICATION_SHADER_SUBD_LEVEL) | groupID % (1u << AMPLIFICATION_SHADER_SUBD_LEVEL);
	float3x3 tri = GetVertices(subdHeapIndex);

	for(uint i = 0; i < 3; ++i)
	{
		uint index = outputIndex * 3 + i;
		vertices[index].Position = mul(float4(tri[i], 1), cView.WorldToClip);
	}
	triangles[outputIndex] = uint3(
		outputIndex * 3 + 0,
		outputIndex * 3 + 1,
		outputIndex * 3 + 2);
}

/* VERTEX SHADING TECHNIQUE */
void RenderVS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID, out VertexOut vertex)
{
	CBT cbt = InitializeCBT(uCBT, cCommonParams.NumCBTElements);

	uint heapIndex = cbt.LeafToHeapIndex(instanceID);
	float3 tri = GetVertices((heapIndex << GEOMETRY_SUBD_LEVEL) + (vertexID / 3))[vertexID % 3];

	vertex.Position = mul(float4(tri, 1), cView.WorldToClip);
}

struct PSOut
{
 	float4 Color : SV_Target0;
	float2 Normal : SV_Target1;
	float Roughness : SV_Target2;
};

void ShadePS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	out PSOut output)
{
	float depth = tDepth.SampleLevel(sPointClamp, uv, 0);
	float3 viewPos = ViewPositionFromDepth(uv, depth, cView.ClipToView);
	float3 worldPos = mul(float4(viewPos, 1), cView.ViewToWorld).xyz;

	float height;
	float3 N;
	GetTerrain(worldPos.xz, TerrainOctaves, height, N);

	float3 V = normalize(cView.ViewLocation - worldPos);


	MaterialProperties surface;
	surface.BaseColor = 0.3f * float3(1.0f, 0.5f, 0.3f);
	surface.Normal = N;
	surface.Metalness = 0;
	surface.Emissive = 0;
	surface.Roughness = 0.8f;
	surface.Specular = 0.5f;
	surface.Opacity = 1.0f;
	BrdfData brdfData = GetBrdfData(surface);

	float dither = InterleavedGradientNoise(position.xy);

	float3 lighting = 0.0f;
	for(uint i = 0; i < cView.LightCount; ++i)
	{
		Light light = GetLight(i);
		lighting += DoLight(light, brdfData.Specular, brdfData.Diffuse, brdfData.Roughness, surface.Normal, V, worldPos, viewPos.z, dither);
	}

	lighting += Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(worldPos, N, -V);

	float fogSlice = sqrt((viewPos.z - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tFog.SampleLevel(sLinearClamp, float3(uv, fogSlice), 0);
	lighting = lighting * scatteringTransmittance.w + scatteringTransmittance.rgb;

	output.Color = float4(lighting, 1);
	output.Normal = Octahedral::Pack(surface.Normal);
	output.Roughness = brdfData.Roughness;
}

/* DEBUG VISUALIZATION TECHNIQUE */

struct DebugVisualizeParams
{
	uint NumCBTElements;
};

ConstantBuffer<DebugVisualizeParams> cDebugVisualizeParams : register(b0);

void DebugVisualizeVS(
	uint vertexID : SV_VertexID,
	uint instanceID : SV_InstanceID,
	out float4 pos : SV_Position,
	out float4 color : COLOR)
{
	CBT cbt = InitializeCBT(uCBT, cDebugVisualizeParams.NumCBTElements);

	uint heapIndex = cbt.LeafToHeapIndex(instanceID);

	float3x3 baseTriangle = float3x3(
		0, 1, 0,
		0, 0, 0,
		1, 0, 0
	);

	float3 tri = LEB::TransformAttributes(heapIndex, baseTriangle)[vertexID];
	tri.xy = tri.xy * 2 - 1;
	pos = float4(tri, 1);

	uint state = SeedThread(firstbithigh(heapIndex));
	color = float4(Random01(state), Random01(state), Random01(state), 1);
}

float4 DebugVisualizePS(
	float4 position : SV_Position,
	float4 color : COLOR,
	float3 bary : SV_Barycentrics) : SV_Target
{
	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	return float4(color.xyz * saturate(minBary + 0.7), 1);
}
