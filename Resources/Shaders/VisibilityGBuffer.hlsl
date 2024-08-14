#include "Common.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "DeferredCommon.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
StructuredBuffer<MeshletCandidate> tVisibleMeshlets : register(t1);

struct PSOut
{
 	float4 GBuffer0 : SV_Target0;
 	float2 GBuffer1 : SV_Target1;
 	float2 GBuffer2 : SV_Target2;
};

bool VisibilityShadingCommon(uint2 texel, out PSOut output)
{
	uint candidateIndex, primitiveID;
	if(!UnpackVisBuffer(tVisibilityTexture[texel], candidateIndex, primitiveID))
		return false;

	MeshletCandidate candidate = tVisibleMeshlets[candidateIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);

	float2 uv = TexelToUV(texel, cView.ViewportDimensionsInv);

	// Vertex Shader
	VisBufferVertexAttribute vertex = GetVertexAttributes(uv, instance, candidate.MeshletIndex, primitiveID);

	// Surface Shader
	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, vertex);

	output.GBuffer0 = PackGBuffer0(surface.BaseColor, surface.Specular);
	output.GBuffer1 = PackGBuffer1(surface.Normal);
	output.GBuffer2 = PackGBuffer2(surface.Roughness, surface.Metalness);

	return true;
}

void ShadePS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	out PSOut output)
{
	VisibilityShadingCommon((uint2)position.xy, output);
}
