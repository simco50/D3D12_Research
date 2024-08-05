#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "Noise.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
Texture2D<float> tAO :	register(t1);
Texture2D<float> tDepth : register(t2);
Texture2D tPreviousSceneColor :	register(t3);
Texture3D<float4> tFog : register(t4);
StructuredBuffer<MeshletCandidate> tVisibleMeshlets : register(t5);
StructuredBuffer<uint> tLightGrid : register(t6);

float3 DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.ViewportDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
	uint lightGridOffset = tileIndex1D * TILED_LIGHTING_NUM_BUCKETS;

	float3 lighting = 0;
	for(uint bucketIndex = 0; bucketIndex < TILED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
	{
		uint bucket = tLightGrid[lightGridOffset + bucketIndex];
		while(bucket)
		{
			uint bitIndex = firstbitlow(bucket);
			bucket ^= 1u << bitIndex;

			uint lightIndex = bitIndex + bucketIndex * 32;
			Light light = GetLight(lightIndex);
			lighting += DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);
		}
	}
	return lighting;
}

struct PSOut
{
 	float4 Color : SV_Target0;
	float2 Normal : SV_Target1;
	float Roughness : SV_Target2;
};

bool VisibilityShadingCommon(uint2 texel, out PSOut output)
{
	uint candidateIndex, primitiveID;
	if(!UnpackVisBuffer(tVisibilityTexture[texel], candidateIndex, primitiveID))
		return false;

	float2 uv = (0.5f + texel) * cView.ViewportDimensionsInv;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, uv, 0);
	float dither = InterleavedGradientNoise(texel);

	MeshletCandidate candidate = tVisibleMeshlets[candidateIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);

	// Vertex Shader
	VisBufferVertexAttribute vertex = GetVertexAttributes(uv, instance, candidate.MeshletIndex, primitiveID);
	float linearDepth = vertex.LinearDepth;

	// Surface Shader
	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, vertex);
	BrdfData brdfData = GetBrdfData(surface);

	float3 V = normalize(cView.ViewLocation - vertex.Position);
	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(vertex.Position, surface.Normal, V, brdfData.Roughness, tDepth, tPreviousSceneColor, dither, ssrWeight);

	float3 outRadiance = 0;
	outRadiance += DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, vertex.Position, texel, linearDepth, dither);
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(vertex.Position, surface.Normal, -V);
	outRadiance += ssr;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tFog.SampleLevel(sLinearClamp, float3(uv, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	float reflectivity = saturate(Square(1 - brdfData.Roughness));

	output.Color = float4(outRadiance, surface.Opacity);
	output.Normal = Octahedral::Pack(surface.Normal);
	output.Roughness = reflectivity;
	return true;
}

void ShadePS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	out PSOut output)
{
	VisibilityShadingCommon((uint2)position.xy, output);
}

RWTexture2D<float4> uColorTarget : register(u0);
RWTexture2D<float2> uNormalsTarget : register(u1);
RWTexture2D<float> uRoughnessTarget : register(u2);

[numthreads(8, 8, 1)]
void ShadeCS(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;

	PSOut output;
	if(VisibilityShadingCommon(texel, output))
	{
		uColorTarget[texel] = output.Color;
		uNormalsTarget[texel] = output.Normal;
		uRoughnessTarget[texel] = output.Roughness;
	}
}
