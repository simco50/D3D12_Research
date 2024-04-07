#include "Common.hlsli"

Texture2D<float> tSourceMask : register(t0);

Texture2D<uint2> tSource : register(t0);
RWTexture2D<uint2> uTarget : register(u0);
RWTexture2D<float> uResult : register(u0);

struct PassParams
{
	uint2 Dimensions;
	float2 DimensionsInv;
	uint2 SampleDilation;
};
ConstantBuffer<PassParams> cPass : register(b0);

[numthreads(8, 8, 1)]
void JumpFloodInitCS(uint3 threadId : SV_DispatchThreadID)
{
	int2 texel = threadId.xy;
	if(any(texel >= cPass.Dimensions))
		return;

#if 0
	int2 points[] = {
		int2(30, 30),
		int2(50, 80),
		int2(100, 20),
	};
	for(int i = 0; i < ArraySize(points); ++i)
	{
		if(all(points[i] == texel))
		{
			uTarget[texel] = texel;
			return;
		}
	}

	uTarget[texel] = 0;
#else
	float d = tSourceMask[texel];
	uTarget[texel] = d == 0 ? 0 : texel;
#endif
}


[numthreads(8, 8, 1)]
void JumpFloodCS(uint3 threadId : SV_DispatchThreadID)
{
	int2 texel = threadId.xy;
	if(any(texel >= cPass.Dimensions))
		return;

	float bestDistSq = FLT_MAX;
	uint2 bestValue = 0;

	[unroll]
	for(int i = -1; i <= 1; ++i)
	{
		uint2 location = clamp(texel + i * cPass.SampleDilation, 0, cPass.Dimensions - 1);
		uint2 v = tSource[location];
		float2 delta = (float2)v - texel;
		float distSq = dot(delta, delta);
		if(distSq < bestDistSq)
		{
			bestDistSq = distSq;
			bestValue = v;
		}
	}

	uTarget[texel] = bestValue;
}

struct ApplyParams
{
	uint Width;
};

ConstantBuffer<ApplyParams> cApplyParams : register(b0);

[numthreads(8, 8, 1)]
void JumpFloodApplyCS(uint3 threadId : SV_DispatchThreadID)
{
	int2 texel = threadId.xy;
	if(any(texel >= cView.TargetDimensions))
		return;

	float mask = 0;
	uint2 v = tSource[texel];
	if(all(v != 0))
	{
		float d = distance(v, texel);
		mask = saturate(cApplyParams.Width - d) * any(v != texel);
	}
	uResult[texel] = mask;
}
