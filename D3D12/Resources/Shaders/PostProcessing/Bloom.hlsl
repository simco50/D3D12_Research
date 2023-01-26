#include "Common.hlsli"

// Based on
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
// https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/

#ifndef KARIS_AVERAGE
#define KARIS_AVERAGE 0
#endif

Texture2D<float4> tSource : register(t0);
Texture2D<float4> tPreviousSource : register(t1);
RWTexture2D<float4> uTarget : register(u0);

struct DownsampleParams
{
	float2 TargetDimensionsInv;
	uint SourceMip;
};
ConstantBuffer<DownsampleParams> cDownsampleParams : register(b0);

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
	float2 UV = (threadId.xy + 0.5f) * cDownsampleParams.TargetDimensionsInv;

	SamplerState sSampler = sLinearClamp;
	uint mip = cDownsampleParams.SourceMip;

	float3 outColor = 0;
	float3 M0 = tSource.SampleLevel(sSampler, UV, mip, int2(-1.0f,  1.0f)).xyz;
	float3 M1 = tSource.SampleLevel(sSampler, UV, mip, int2( 1.0f,  1.0f)).xyz;
	float3 M2 = tSource.SampleLevel(sSampler, UV, mip, int2(-1.0f, -1.0f)).xyz;
	float3 M3 = tSource.SampleLevel(sSampler, UV, mip, int2( 1.0f, -1.0f)).xyz;

	float3 TL = tSource.SampleLevel(sSampler, UV, mip, int2(-2.0f, 2.0f)).xyz;
	float3 T  = tSource.SampleLevel(sSampler, UV, mip, int2( 0.0f, 2.0f)).xyz;
	float3 TR = tSource.SampleLevel(sSampler, UV, mip, int2( 2.0f, 2.0f)).xyz;
	float3 L  = tSource.SampleLevel(sSampler, UV, mip, int2(-2.0f, 0.0f)).xyz;
	float3 C  = tSource.SampleLevel(sSampler, UV, mip, int2( 0.0f, 0.0f)).xyz;
	float3 R  = tSource.SampleLevel(sSampler, UV, mip, int2( 2.0f, 0.0f)).xyz;
	float3 BL = tSource.SampleLevel(sSampler, UV, mip, int2(-2.0f,-2.0f)).xyz;
	float3 B  = tSource.SampleLevel(sSampler, UV, mip, int2( 0.0f,-2.0f)).xyz;
	float3 BR = tSource.SampleLevel(sSampler, UV, mip, int2( 2.0f,-2.0f)).xyz;

	outColor += ComputePartialAverage(M0, M1, M2, M3) * 0.5f;
	outColor += ComputePartialAverage(TL, T, C, L) * 0.125f;
	outColor += ComputePartialAverage(TR, T, C, R) * 0.125f;
	outColor += ComputePartialAverage(BL, B, C, L) * 0.125f;
	outColor += ComputePartialAverage(BR, B, C, R) * 0.125f;

	uTarget[threadId.xy] = float4(outColor, 1);
}

struct UpsampleParams
{
	float2 TargetDimensionsInv;
	uint SourceCurrentMip;
	uint SourcePreviousMip;
	float Radius;
};
ConstantBuffer<UpsampleParams> cUpsampleParams : register(b0);

[numthreads(8, 8, 1)]
void UpsampleCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 UV = (threadId.xy + 0.5f) * cUpsampleParams.TargetDimensionsInv;
	float3 currentColor = tSource.SampleLevel(sPointClamp, UV, cUpsampleParams.SourceCurrentMip).xyz;

	SamplerState sSampler = sLinearBorder;
	uint mip = cUpsampleParams.SourcePreviousMip;

	float3 outColor = 0;
	outColor += 0.0625f * tPreviousSource.SampleLevel(sSampler, UV, mip, int2( -1.0f,  1.0f)).xyz;
	outColor += 0.125f  * tPreviousSource.SampleLevel(sSampler, UV, mip, int2(  0.0f,  1.0f)).xyz;
	outColor += 0.0625f * tPreviousSource.SampleLevel(sSampler, UV, mip, int2(  1.0f,  1.0f)).xyz;
	outColor += 0.125f  * tPreviousSource.SampleLevel(sSampler, UV, mip, int2( -1.0f,  0.0f)).xyz;
	outColor += 0.25f   * tPreviousSource.SampleLevel(sSampler, UV, mip, int2(  0.0f,  0.0f)).xyz;
	outColor += 0.125f  * tPreviousSource.SampleLevel(sSampler, UV, mip, int2(  1.0f,  0.0f)).xyz;
	outColor += 0.0625f * tPreviousSource.SampleLevel(sSampler, UV, mip, int2( -1.0f, -1.0f)).xyz;
	outColor += 0.125f  * tPreviousSource.SampleLevel(sSampler, UV, mip, int2(  0.0f, -1.0f)).xyz;
	outColor += 0.0625f * tPreviousSource.SampleLevel(sSampler, UV, mip, int2(  1.0f, -1.0f)).xyz;
	uTarget[threadId.xy] = float4(lerp(currentColor, outColor, cUpsampleParams.Radius) , 1);
}
