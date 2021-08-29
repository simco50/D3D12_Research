#include "Random.hlsli"
#include "CBT.hlsli"

#define RootSig "CBV(b0), " \
				"CBV(b1), " \
				"DescriptorTable(UAV(u0, numDescriptors = 2)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))"

RWByteAddressBuffer uCBT : register(u0);
RWByteAddressBuffer uIndirectArgs : register(u1);

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
	uint2 MouseLocation;
	float Scale;
};

ConstantBuffer<CommonArgs> cCommonArgs : register(b0);
ConstantBuffer<SumReductionData> cSumReductionData : register(b1);
ConstantBuffer<SubdivisionData>  cSubdivisionData : register(b1);

[numthreads(1, 1, 1)]
void PrepareDispatchArgsCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uIndirectArgs.Store3(0, uint3(ceil((float)cbt.NumNodes() / 64), 1, 1));
	uIndirectArgs.Store2(4 * 3, uint2(cbt.NumNodes() * 3, 1));
}

[RootSignature(RootSig)]
[numthreads(64, 1, 1)]
void SumReductionCS(uint threadID : SV_DispatchThreadID)
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

float Sign(float2 p1, float2 p2, float2 p3)
{
	return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

bool PointInTriangle(float2 pt, uint heapIndex, float scale)
{
	float d1, d2, d3;
	bool has_neg, has_pos;

	float3x3 tri = LEB::GetTriangleVertices(heapIndex);
	tri *= scale;

	d1 = Sign(pt, tri[0].xy, tri[1].xy);
	d2 = Sign(pt, tri[1].xy, tri[2].xy);
	d3 = Sign(pt, tri[2].xy, tri[0].xy);

	has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

	return !(has_neg && has_pos);
}

[numthreads(64, 1, 1)]
void UpdateCS(uint3 threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uint heapIndex = cbt.LeafToHeapIndex(threadID.x);

	if(PointInTriangle(cSubdivisionData.MouseLocation, heapIndex, cSubdivisionData.Scale))
	{
		LEB::CBTSplitConformed(cbt, heapIndex);
	}

	if(heapIndex > 1)
	{
		LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
		if(!PointInTriangle(cSubdivisionData.MouseLocation, diamond.Base, cSubdivisionData.Scale)  &&
			!PointInTriangle(cSubdivisionData.MouseLocation, diamond.Top, cSubdivisionData.Scale))
		{
			LEB::CBTMergeConformed(cbt, heapIndex);
		}
	}
}

void RenderVS(uint vertexID : SV_VertexID, out float4 pos : SV_POSITION, out float4 color : COLOR)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint triangleIndex = vertexID / 3;
	uint vertexIndex = vertexID % 3;
	uint heapIndex = cbt.LeafToHeapIndex(triangleIndex);

	float3x3 tri = LEB::GetTriangleVertices(heapIndex);
	float2 position = tri[vertexIndex].xy;
	position = position * 2 - 1;
	position.y = -position.y;
	pos = float4(position, 0.0f, 1.0f);

	uint state = SeedThread(heapIndex);
	color = float4(Random01(state), Random01(state), Random01(state), 1);
}

float4 RenderPS(float4 position : SV_POSITION, float4 color : COLOR, float3 bary : SV_Barycentrics) : SV_TARGET
{
	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	return float4(color.xyz * minBary, 1);
}