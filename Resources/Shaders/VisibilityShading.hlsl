#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "Noise.hlsli"

struct PassParams
{
	Texture2DH<uint> 					VisibilityTexture;
	Texture2DH<float> 					AO;
	Texture2DH<float> 					Depth;
	Texture2DH<float4>					PreviousSceneColor;
	Texture3DH<float4>					Fog;
	StructuredBufferH<MeshletCandidate> VisibleMeshlets;
	StructuredBufferH<uint> 			LightGrid;

	// For compute
	RWTexture2DH<float4> 				ColorTarget;
	RWTexture2DH<float2> 				NormalsTarget;
	RWTexture2DH<float> 				RoughnessTarget;
};
DEFINE_CONSTANTS(PassParams, 0);

float3 DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.ViewportDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
	uint lightGridOffset = tileIndex1D * TILED_LIGHTING_NUM_BUCKETS;

	float3 lighting = 0;
	for(uint bucketIndex = 0; bucketIndex < TILED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
	{
		uint bucket = cPassParams.LightGrid[lightGridOffset + bucketIndex];

		bucket = WaveActiveBitOr(bucket);
		bucket = WaveReadLaneFirst(bucket);

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
	if(!UnpackVisBuffer(cPassParams.VisibilityTexture[texel], candidateIndex, primitiveID))
		return false;

	float2 uv = (0.5f + texel) * cView.ViewportDimensionsInv;
	float ambientOcclusion = cPassParams.AO.SampleLevel(sLinearClamp, uv, 0);
	float dither = InterleavedGradientNoise(texel);

	MeshletCandidate candidate = cPassParams.VisibleMeshlets[candidateIndex];
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
	float3 ssr = ScreenSpaceReflections(vertex.Position, surface.Normal, V, brdfData.Roughness, cPassParams.Depth.Get(), cPassParams.PreviousSceneColor.Get(), dither, ssrWeight);

	float3 outRadiance = 0;
	outRadiance += DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, vertex.Position, texel, linearDepth, dither);
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(vertex.Position, surface.Normal, -V);
	outRadiance += ssr;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = cPassParams.Fog.SampleLevel(sLinearClamp, float3(uv, fogSlice), 0);
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

[numthreads(8, 8, 1)]
void ShadeCS(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;

	PSOut output;
	if(VisibilityShadingCommon(texel, output))
	{
		cPassParams.ColorTarget.Store(texel, output.Color);
		cPassParams.NormalsTarget.Store(texel, output.Normal);
		cPassParams.RoughnessTarget.Store(texel, output.Roughness);
	}
}
