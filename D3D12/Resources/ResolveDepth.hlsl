#include "Common.hlsl"
#include "Constants.hlsl"

#ifndef DEPTH_RESOLVE_MIN
#define DEPTH_RESOLVE_MIN 1
#endif

#ifndef DEPTH_RESOLVE_MAX
#define DEPTH_RESOLVE_MAX 0
#endif

#ifndef DEPTH_RESOLVE_AVERAGE
#define DEPTH_RESOLVE_AVERAGE 0
#endif

Texture2DMS<float> tInputTexture : register(t0);
RWTexture2D<float> uOutputTexture : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 threadId : SV_DISPATCHTHREADID)
{
	uint2 dimensions;
	uint sampleCount;
	tInputTexture.GetDimensions(dimensions.x, dimensions.y, sampleCount);
	float result = 1;
	for(uint i = 0; i < sampleCount; ++i)
	{
#if DEPTH_RESOLVE_MIN
		result = min(result, tInputTexture.Load(threadId.xy, i).r);
#elif DEPTH_RESOLVE_MAX
		result = max(result, tInputTexture.Load(threadId.xy, i).r);
#elif DEPTH_RESOLVE_AVERAGE
		result += tInputTexture.Load(threadId.xy, i).r / sampleCount;
#endif
	}
	uOutputTexture[threadId.xy] = result;
}