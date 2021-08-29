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

ConstantBuffer<CommonArgs> cCommonArgs : register(b0);
ConstantBuffer<SumReductionData> cSumReductionData : register(b1);

[numthreads(1, 1, 1)]
void PrepareDispatchArgsCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uIndirectArgs.Store3(0, uint3(cbt.NumNodes(), 1, 1));
	uIndirectArgs.Store2(4 * 3, uint2(cbt.NumNodes() * 3, 1));
}

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
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

[numthreads(16, 1, 1)]
void UpdateCS(uint threadID : SV_DispatchThreadID)
{
	
}

void RenderVS(uint vertexID : SV_VertexID, out float4 pos : SV_POSITION, out float4 color : COLOR)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint triangleIndex = vertexID / 3;
	uint vertexIndex = vertexID % 3;
	uint heapIndex = cbt.LeafToHeapIndex(triangleIndex);

	float3x3 baseTriangle = float3x3(
		0, 1, 0,
		0, 0, 0,
		1, 0, 0
	);

	float3x3 tri = LEB::GetTriangleVertices(heapIndex, baseTriangle);
	float2 position = tri[vertexIndex].xy;
	position = position * 2 - 1;
	position.y = -position.y;
	pos = float4(position, 0.0f, 1.0f);

	uint state = SeedThread(heapIndex);
	color = float4(Random01(state), Random01(state), Random01(state), 1);
}

float4 RenderPS(float4 position : SV_POSITION, float4 color : COLOR) : SV_TARGET
{
	return color;
}