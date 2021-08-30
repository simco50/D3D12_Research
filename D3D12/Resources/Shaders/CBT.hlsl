#include "Random.hlsli"
#include "CBT.hlsli"

#define DEBUG_ALWAYS_SUBDIVIDE 1

#define RootSig "CBV(b0), " \
				"CBV(b1), " \
				"DescriptorTable(UAV(u0, numDescriptors = 2)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1)), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR)"

RWByteAddressBuffer uCBT : register(u0);
RWByteAddressBuffer uIndirectArgs : register(u1);
Texture2D tHeightmap : register(t0);
SamplerState sSampler : register(s0);

struct CommonArgs
{
	uint NumElements;
};

struct SumReductionData
{
	uint Depth;
};

struct SubdivisionData
{
	float4x4 Transform;
	float4x4 ViewInverse;
};

struct RenderData
{
	float4x4 Transform;
	float4x4 ViewProjection;
	float HeightmapSizeInv;
};

ConstantBuffer<CommonArgs> cCommonArgs : register(b0);
ConstantBuffer<SumReductionData> cSumReductionData : register(b1);
ConstantBuffer<SubdivisionData> cSubdivisionData : register(b1);
ConstantBuffer<RenderData> cRenderData : register(b1);

[numthreads(1, 1, 1)]
void PrepareDispatchArgsCS(uint3 threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uint numThreads = ceil((float)cbt.NumNodes() / 64);
	uIndirectArgs.Store(0, numThreads);
	uIndirectArgs.Store(4, 1);
	uIndirectArgs.Store(8, 1);
	
	uint offset = 4 * 3;
	uint numVertices = 3;
	uint numInstances = cbt.NumNodes();
	uIndirectArgs.Store(offset + 0, numVertices);
	uIndirectArgs.Store(offset + 4, numInstances);
	uIndirectArgs.Store(offset + 8, 0);
	uIndirectArgs.Store(offset + 12, 0);
}

[RootSignature(RootSig)]
[numthreads(64, 1, 1)]
void SumReductionCS(uint3 threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uint count = 1u << cSumReductionData.Depth;
	uint index = threadID.x;
	if(index < count)
	{
		index += count;
		uint leftChild = cbt.GetData(cbt.LeftChildIndex(index));
		uint rightChild = cbt.GetData(cbt.RightChildIndex(index));
		cbt.SetData(index, leftChild + rightChild);
	}
}

[numthreads(64, 1, 1)]
void UpdateCS(uint3 threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uint heapIndex = cbt.LeafToHeapIndex(threadID.x);

	if(DEBUG_ALWAYS_SUBDIVIDE)
	{
		LEB::CBTSplitConformed(cbt, heapIndex);
	}

	if(!DEBUG_ALWAYS_SUBDIVIDE && heapIndex > 1)
	{
		LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
		if(1)
		{
			LEB::CBTMergeConformed(cbt, heapIndex);
		}
	}
}

void RenderVS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID, out float4 pos : SV_POSITION, out float2 uv : TEXCOORD)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint heapIndex = cbt.LeafToHeapIndex(instanceID);

	float3 tri = LEB::GetTriangleVertices(heapIndex)[vertexID];
	uv = tri.xz;

	tri.y += tHeightmap.SampleLevel(sSampler, tri.xz, 0).r;

	tri = mul(float4(tri, 1), cRenderData.Transform).xyz;
	pos = mul(float4(tri, 1), cRenderData.ViewProjection);
}

float4 RenderPS(
	float4 position : SV_POSITION, 
	float2 uv : TEXCOORD, 
	float3 bary : SV_Barycentrics) : SV_TARGET
{
	float tl = tHeightmap.SampleLevel(sSampler, uv, 0, uint2(-1, -1)).r;
	float t  = tHeightmap.SampleLevel(sSampler, uv, 0, uint2( 0, -1)).r;
	float tr = tHeightmap.SampleLevel(sSampler, uv, 0, uint2( 1, -1)).r;
	float l  = tHeightmap.SampleLevel(sSampler, uv, 0, uint2(-1,  0)).r;
	float r  = tHeightmap.SampleLevel(sSampler, uv, 0, uint2( 1,  0)).r;
	float bl = tHeightmap.SampleLevel(sSampler, uv, 0, uint2(-1,  1)).r;
	float b  = tHeightmap.SampleLevel(sSampler, uv, 0, uint2( 0,  1)).r;
	float br = tHeightmap.SampleLevel(sSampler, uv, 0, uint2( 1,  1)).r;

	float dX = tr + 2 * r + br - tl - 2 * l - bl;
	float dY = bl + 2 * b + br - tl - 2 * t - tr;
	float3 normal = normalize(float3(dX, 1.0f / 100, dY));

	float3 dir = normalize(float3(1, 1, 1));
	float4 color = float4(saturate(dot(dir, normalize(normal)).xxx), 1);

	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	return float4(color.xyz * saturate(minBary + 0.7), 1);
}

void DebugVisualizeVS(
	uint vertexID : SV_VertexID, 
	uint instanceID : SV_InstanceID,
	out float4 pos : SV_POSITION, 
	out float4 color : COLOR)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint heapIndex = cbt.LeafToHeapIndex(instanceID);

	float3 tri = LEB::GetTriangleVertices(heapIndex)[vertexID];
	tri.y = tri.z;
	tri.xy = tri.xy * 2 - 1;
	pos = float4(tri, 1);
	
	uint state = SeedThread(heapIndex);
	color = float4(Random01(state), Random01(state), Random01(state), 1);
}

float4 DebugVisualizePS(
	float4 position : SV_POSITION, 
	float4 color : COLOR, 
	float3 bary : SV_Barycentrics) : SV_TARGET
{
	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	return float4(color.xyz * saturate(minBary + 0.7), 1);
}