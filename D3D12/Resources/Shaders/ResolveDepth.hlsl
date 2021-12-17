#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))")

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

[RootSignature(RootSig)]
[numthreads(16, 16, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	uint2 dimensions;
	uint sampleCount;
	tInputTexture.GetDimensions(dimensions.x, dimensions.y, sampleCount);
	if(threadId.x < dimensions.x && threadId.y < dimensions.y)
	{
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
}
