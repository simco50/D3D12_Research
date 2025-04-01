#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "Noise.hlsli"
#include "GBuffer.hlsli"

struct PassParams
{
	Texture2DH<uint4> GBuffer;
	Texture2DH<float> Depth;
	Texture2DH<float4> PreviousSceneColor;
	Texture3DH<float4> Fog;
	StructuredBufferH<uint> LightGrid;
	Texture2DH<float> AO;
	RWTexture2DH<float4> Output;
};
DEFINE_CONSTANTS(PassParams, 0);

float3 DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.ViewportDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
	uint lightGridOffset = tileIndex1D * TILED_LIGHTING_NUM_BUCKETS;

	float3 lighting = 0.0f;
	for(uint bucketIndex = 0; bucketIndex < TILED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
	{
		uint bucket = cPassParams.LightGrid[lightGridOffset + bucketIndex];
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


[numthreads(8, 8, 1)]
void ShadeCS(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;
	if(any(texel >= cView.ViewportDimensions))
		return;

	float2 uv = TexelToUV(texel, cView.ViewportDimensionsInv);
	float depth = cPassParams.Depth[texel];
	float3 viewPos = ViewPositionFromDepth(uv, depth, cView.ClipToView);
	float3 worldPos = mul(float4(viewPos, 1), cView.ViewToWorld).xyz;
	float linearDepth = viewPos.z;

	MaterialProperties surface = (MaterialProperties)0;
	GBufferData gbuffer = LoadGBuffer(cPassParams.GBuffer[texel]);
	surface.BaseColor = gbuffer.BaseColor;
	surface.Specular = gbuffer.Specular;
	surface.Normal = gbuffer.Normal;
	surface.Roughness = gbuffer.Roughness;
	surface.Metalness = gbuffer.Metalness;
	surface.Emissive = gbuffer.Emissive;

	float ambientOcclusion = cPassParams.AO.SampleLevel(sLinearClamp, uv, 0);
	float dither = InterleavedGradientNoise(texel);

	BrdfData brdfData = GetBrdfData(surface);

	float3 V = normalize(cView.ViewLocation - worldPos);
	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(worldPos, surface.Normal, V, brdfData.Roughness, cPassParams.Depth.Get(), cPassParams.PreviousSceneColor.Get(), dither, ssrWeight);

	float3 lighting = 0;
	lighting += DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, worldPos, texel, linearDepth, dither);
	lighting += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(worldPos, surface.Normal, -V);
	lighting += ssr;
	lighting += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = cPassParams.Fog.SampleLevel(sLinearClamp, float3(uv, fogSlice), 0);
	lighting = lighting * scatteringTransmittance.w + scatteringTransmittance.rgb;

	cPassParams.Output.Store(texel, float4(lighting, surface.Opacity));
}
