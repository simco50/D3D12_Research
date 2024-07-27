#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "Noise.hlsli"
#include "DeferredCommon.hlsli"

Texture2D<float4> tGBuffer0 				: register(t0);
Texture2D<float2> tGBuffer1					: register(t1);
Texture2D<float2> tGBuffer2					: register(t2);
Texture2D<float> tDepth 					: register(t3);
Texture2D tPreviousSceneColor 				: register(t4);
Texture3D<float4> tFog 						: register(t5);
StructuredBuffer<uint> tLightGrid 			: register(t6);
Texture2D<float> tAO						: register(t7);

RWTexture2D<float4> uOutput 				: register(u0);

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.ViewportDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
	uint lightGridOffset = tileIndex1D * TILED_LIGHTING_NUM_BUCKETS;

	LightResult totalResult = (LightResult)0;
	for(uint bucketIndex = 0; bucketIndex < TILED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
	{
		uint bucket = tLightGrid[lightGridOffset + bucketIndex];
		while(bucket)
		{
			uint bitIndex = firstbitlow(bucket);
			bucket ^= 1u << bitIndex;

			uint lightIndex = bitIndex + bucketIndex * 32;
			Light light = GetLight(lightIndex);
			totalResult = totalResult + DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);
		}
	}
	return totalResult;
}


[numthreads(8, 8, 1)]
void ShadeCS(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;
	if(any(texel >= cView.ViewportDimensions))
		return;

	float2 uv = (texel + 0.5f) * cView.ViewportDimensionsInv;
	float depth = tDepth[texel];
	float3 viewPos = ViewPositionFromDepth(uv, depth, cView.ClipToView);
	float3 worldPos = mul(float4(viewPos, 1), cView.ViewToWorld).xyz;
	float linearDepth = viewPos.z;

	float4 gbuffer0 = tGBuffer0[texel];
	float2 gbuffer1 = tGBuffer1[texel];
	float2 gbuffer2 = tGBuffer2[texel];

	MaterialProperties surface = (MaterialProperties)0;
	UnpackGBuffer0(gbuffer0, surface.BaseColor, surface.Specular);
	UnpackGBuffer1(gbuffer1, surface.Normal);
	UnpackGBuffer2(gbuffer2, surface.Roughness, surface.Metalness);

	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, uv, 0);
	float dither = InterleavedGradientNoise(texel);

	BrdfData brdfData = GetBrdfData(surface);

	float3 V = normalize(cView.ViewLocation - worldPos);
	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(worldPos, surface.Normal, V, brdfData.Roughness, tDepth, tPreviousSceneColor, dither, ssrWeight);

	LightResult result = DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, worldPos, texel, linearDepth, dither);

	float3 outRadiance = 0;
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(worldPos, surface.Normal, -V);
	outRadiance += result.Diffuse + result.Specular;
	outRadiance += ssr;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tFog.SampleLevel(sLinearClamp, float3(uv, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	uOutput[texel] = float4(outRadiance, surface.Opacity);
}
