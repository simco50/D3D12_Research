#include "Common.hlsli"

#define BLOCK_SIZE 16
#define THREAD_COUNT (BLOCK_SIZE * BLOCK_SIZE)

struct PrepareParams
{
	Texture2DH<float> DepthMap;
	RWTexture2DH<float2> OutputMap;
};
DEFINE_CONSTANTS(PrepareParams, 0);

struct ReduceParams
{
	Texture2DH<float2> ReductionMap;
	RWTexture2DH<float2> OutputMap;
};
DEFINE_CONSTANTS(ReduceParams, 0);

groupshared float2 gsDepthSamples[BLOCK_SIZE * BLOCK_SIZE];

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void PrepareReduceDepth(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
	uint2 samplePos = groupId.xy * BLOCK_SIZE + groupThreadId.xy;

	uint2 dimensions;
	cPrepareParams.DepthMap.Get().GetDimensions(dimensions.x, dimensions.y);
	samplePos = min(samplePos, dimensions - 1);
	float depth = cPrepareParams.DepthMap[samplePos];
	if(depth > 0.0f)
	{
		depth = LinearizeDepth01(depth);
	}
	gsDepthSamples[groupIndex] = float2(depth, depth);

	GroupMemoryBarrierWithGroupSync();

	for(uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
	{
		if(groupIndex < s)
		{
			gsDepthSamples[groupIndex].x = min(gsDepthSamples[groupIndex].x, gsDepthSamples[groupIndex + s].x);
			gsDepthSamples[groupIndex].y = max(gsDepthSamples[groupIndex].y, gsDepthSamples[groupIndex + s].y);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if(groupIndex == 0)
	{
		cPrepareParams.OutputMap.Store(groupId.xy, gsDepthSamples[0]);
	}
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void ReduceDepth(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
	uint2 dimensions;
	cReduceParams.ReductionMap.Get().GetDimensions(dimensions.x, dimensions.y);

	uint2 samplePos = groupId.xy * BLOCK_SIZE + groupThreadId.xy;
	samplePos = min(samplePos, dimensions - 1);

	float minDepth = cReduceParams.ReductionMap[samplePos].x;
	float maxDepth = cReduceParams.ReductionMap[samplePos].y;
	gsDepthSamples[groupIndex] = float2(minDepth, maxDepth);

	GroupMemoryBarrierWithGroupSync();

	for(uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
	{
		if(groupIndex < s)
		{
			gsDepthSamples[groupIndex].x = min(gsDepthSamples[groupIndex].x, gsDepthSamples[groupIndex + s].x);
			gsDepthSamples[groupIndex].y = max(gsDepthSamples[groupIndex].y, gsDepthSamples[groupIndex + s].y);
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if(groupIndex == 0)
	{
		cReduceParams.OutputMap.Store(groupId.xy, gsDepthSamples[0]);
	}
}
