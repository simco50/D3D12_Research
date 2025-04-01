#include "Common.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "GBuffer.hlsli"

struct PassParams
{
	Texture2DH<uint> VisibilityTexture;
	StructuredBufferH<MeshletCandidate> VisibleMeshlets;
};
DEFINE_CONSTANTS(PassParams, 0);

struct PSOut
{
 	uint4 GBuffer : SV_Target0;
};

bool VisibilityShadingCommon(uint2 texel, out PSOut output)
{
	uint candidateIndex, primitiveID;
	if(!UnpackVisBuffer(cPassParams.VisibilityTexture[texel], candidateIndex, primitiveID))
		return false;

	MeshletCandidate candidate = cPassParams.VisibleMeshlets[candidateIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);

	float2 uv = TexelToUV(texel, cView.ViewportDimensionsInv);

	// Vertex Shader
	VisBufferVertexAttribute vertex = GetVertexAttributes(uv, instance, candidate.MeshletIndex, primitiveID);

	// Surface Shader
	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, vertex);

	GBufferData gbuffer;
	gbuffer.BaseColor = surface.BaseColor;
	gbuffer.Specular = surface.Specular;
	gbuffer.Normal = surface.Normal;
	gbuffer.Roughness = surface.Roughness;
	gbuffer.Metalness = surface.Metalness;
	gbuffer.Emissive = surface.Emissive;

	output.GBuffer = PackGBuffer(gbuffer);

	return true;
}

void ShadePS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	out PSOut output)
{
	VisibilityShadingCommon((uint2)position.xy, output);
}
