#include "Common.hlsli"
#include "Random.hlsli"
#include "VisibilityBuffer.hlsli"
#include "ColorMaps.hlsli"

struct PassParams
{
	uint Mode;
	Texture2DH<uint> VisibilityTexture;
	StructuredBufferH<MeshletCandidate> MeshletCandidates;
	Texture2DH<uint> DebugData;
	RWTexture2DH<float4> Output;
};
DEFINE_CONSTANTS(PassParams, 0);

[numthreads(8, 8, 1)]
void DebugRenderCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 texel = dispatchThreadId.xy;
	if(any(texel >= cView.ViewportDimensions))
		return;

	float3 color = 0;
	float2 screenUV = TexelToUV(texel, cView.ViewportDimensionsInv);

	uint candidateIndex, primitiveID;
	if(UnpackVisBuffer(cPassParams.VisibilityTexture[texel], candidateIndex, primitiveID))
	{
		MeshletCandidate candidate = cPassParams.MeshletCandidates[candidateIndex];
		InstanceData instance = GetInstance(candidate.InstanceID);
		uint meshletIndex = candidate.MeshletIndex;

		VisBufferVertexAttribute vertex = GetVertexAttributes(screenUV, instance, meshletIndex, primitiveID);

		bool enableWireframe = true;
		if(cPassParams.Mode == 1)
		{
			uint seed = SeedThread(candidate.InstanceID);
			color = RandomColor(seed);
		}
		else if(cPassParams.Mode == 2)
		{
			uint seed = SeedThread(meshletIndex);
			color = RandomColor(seed);
		}
		else if(cPassParams.Mode == 3)
		{
			uint seed = SeedThread(primitiveID);
			color = RandomColor(seed);
		}
		else if(cPassParams.Mode == 4)
		{
			uint overdraw = cPassParams.DebugData[texel];
			color = Viridis(saturate(0.05f * overdraw));
		}
		if(enableWireframe)
			color *= saturate(Wireframe(vertex.Barycentrics) + 0.8);
	}

	if(cPassParams.Mode == 4)
	{
		if(texel.y > cView.ViewportDimensions.y - 50)
			color = Viridis(screenUV.x);
	}

	cPassParams.Output.Store(texel, float4(color, 1));
}
