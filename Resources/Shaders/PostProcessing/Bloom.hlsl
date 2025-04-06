#include "Common.hlsli"

// Based on
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
// https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/

#ifndef KARIS_AVERAGE
#define KARIS_AVERAGE 0
#endif


struct DownsampleParams
{
	float2 TargetDimensionsInv;
	uint SourceMip;
	Texture2DH<float4> Source;
	RWTexture2DH<float4> Target;
};
DEFINE_CONSTANTS(DownsampleParams, 0);

float3 ComputePartialAverage(float3 v0, float3 v1, float3 v2, float3 v3)
{
#if KARIS_AVERAGE
	float w0 = 1.0f / (1.0f + GetLuminance(v0));
	float w1 = 1.0f / (1.0f + GetLuminance(v1));
	float w2 = 1.0f / (1.0f + GetLuminance(v2));
	float w3 = 1.0f / (1.0f + GetLuminance(v3));
	return (v0 * w0 + v1 * w1 + v2 * w2 + v3 * w3) / (w0 + w1 + w2 + w3);
#else
	return 0.25f * (v0 + v1 + v2 + v3);
#endif
}

[numthreads(8, 8, 1)]
void DownsampleCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 uv = TexelToUV(threadId.xy, cDownsampleParams.TargetDimensionsInv);

	SamplerState sSampler = sLinearClamp;
	uint mip = cDownsampleParams.SourceMip;

	float3 outColor = 0;
	float3 M0 = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2(-1.0f,  1.0f)).xyz;
	float3 M1 = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 1.0f,  1.0f)).xyz;
	float3 M2 = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2(-1.0f, -1.0f)).xyz;
	float3 M3 = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 1.0f, -1.0f)).xyz;

	float3 TL = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2(-2.0f, 2.0f)).xyz;
	float3 T  = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 0.0f, 2.0f)).xyz;
	float3 TR = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 2.0f, 2.0f)).xyz;
	float3 L  = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2(-2.0f, 0.0f)).xyz;
	float3 C  = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 0.0f, 0.0f)).xyz;
	float3 R  = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 2.0f, 0.0f)).xyz;
	float3 BL = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2(-2.0f,-2.0f)).xyz;
	float3 B  = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 0.0f,-2.0f)).xyz;
	float3 BR = cDownsampleParams.Source.SampleLevel(sSampler, uv, mip, int2( 2.0f,-2.0f)).xyz;

	outColor += ComputePartialAverage(M0, M1, M2, M3) * 0.5f;
	outColor += ComputePartialAverage(TL, T, C, L) * 0.125f;
	outColor += ComputePartialAverage(TR, T, C, R) * 0.125f;
	outColor += ComputePartialAverage(BL, B, C, L) * 0.125f;
	outColor += ComputePartialAverage(BR, B, C, R) * 0.125f;

	cDownsampleParams.Target.Store(threadId.xy, float4(outColor, 1));
}

struct UpsampleParams
{
	float2 TargetDimensionsInv;
	uint SourceCurrentMip;
	uint SourcePreviousMip;
	float Radius;
	Texture2DH<float4> Source;
	Texture2DH<float4> PreviousSource;
	RWTexture2DH<float4> Target;
};
DEFINE_CONSTANTS(UpsampleParams, 0);

[numthreads(8, 8, 1)]
void UpsampleCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 uv = TexelToUV(threadId.xy, cUpsampleParams.TargetDimensionsInv);
	float3 currentColor = cUpsampleParams.Source.SampleLevel(sPointClamp, uv, cUpsampleParams.SourceCurrentMip).xyz;

	SamplerState sSampler = sLinearBorder;
	uint mip = cUpsampleParams.SourcePreviousMip;

	float3 outColor = 0;
	outColor += 0.0625f * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2( -1.0f,  1.0f)).xyz;
	outColor += 0.125f  * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2(  0.0f,  1.0f)).xyz;
	outColor += 0.0625f * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2(  1.0f,  1.0f)).xyz;
	outColor += 0.125f  * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2( -1.0f,  0.0f)).xyz;
	outColor += 0.25f   * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2(  0.0f,  0.0f)).xyz;
	outColor += 0.125f  * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2(  1.0f,  0.0f)).xyz;
	outColor += 0.0625f * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2( -1.0f, -1.0f)).xyz;
	outColor += 0.125f  * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2(  0.0f, -1.0f)).xyz;
	outColor += 0.0625f * cUpsampleParams.PreviousSource.SampleLevel(sSampler, uv, mip, int2(  1.0f, -1.0f)).xyz;
	cUpsampleParams.Target.Store(threadId.xy, float4(lerp(currentColor, outColor, cUpsampleParams.Radius) , 1));
}
