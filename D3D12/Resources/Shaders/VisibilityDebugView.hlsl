#include "Common.hlsli"
#include "Random.hlsli"
#include "VisibilityBuffer.hlsli"
#include "ColorMaps.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
StructuredBuffer<MeshletCandidate> tMeshletCandidates : register(t1);
Texture2D<uint> tDebugData : register(t2);
RWTexture2D<float4> uColorTarget : register(u0);

struct DebugRenderData
{
	uint Mode;
};

ConstantBuffer<DebugRenderData> cDebugRenderData : register(b0);

[numthreads(8, 8, 1)]
void DebugRenderCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 texel = dispatchThreadId.xy;
	if(any(texel >= cView.TargetDimensions))
		return;

	VisBufferData visibility = (VisBufferData)tVisibilityTexture[texel];
	MeshletCandidate candidate = tMeshletCandidates[visibility.MeshletCandidateIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	uint meshletIndex = candidate.MeshletIndex;
	uint primitiveID = visibility.PrimitiveID;

	float2 screenUV = ((float2)texel.xy + 0.5f) * cView.TargetDimensionsInv;
	VisBufferVertexAttribute vertex = GetVertexAttributes(screenUV, instance, meshletIndex, primitiveID);

	float3 color = 0;
	if(cDebugRenderData.Mode == 1)
	{
		uint seed = SeedThread(candidate.InstanceID);
		color = RandomColor(seed);
	}
	else if(cDebugRenderData.Mode == 2)
	{
		uint seed = SeedThread(meshletIndex);
		color = RandomColor(seed);
	}
	else if(cDebugRenderData.Mode == 3)
	{
		uint seed = SeedThread(primitiveID);
		color = RandomColor(seed);
	}
	else if(cDebugRenderData.Mode == 4)
	{
		uint overdraw = tDebugData[texel];
		color = Magma(saturate(0.05f * overdraw));
	}


	uColorTarget[texel] = float4(color * saturate(Wireframe(vertex.Barycentrics) + 0.8), 1);
}
