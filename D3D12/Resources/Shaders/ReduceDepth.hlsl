#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \

#define BLOCK_SIZE 16
#define THREAD_COUNT (BLOCK_SIZE * BLOCK_SIZE)

#if WITH_MSAA
Texture2DMS<float> tDepthMap : register(t0);
#else
Texture2D<float> tDepthMap : register(t0);
#endif

Texture2D<float2> tReductionMap : register(t0);
RWTexture2D<float2> uOutputMap : register(u0);

cbuffer ShaderParameters : register(b0)
{
    float cNear;
    float cFar;
}

struct CS_INPUT
{
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
};

groupshared float2 gsDepthSamples[BLOCK_SIZE * BLOCK_SIZE];

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void PrepareReduceDepth(CS_INPUT input)
{
    uint2 samplePos = input.GroupId.xy * BLOCK_SIZE + input.GroupThreadId.xy;
#if WITH_MSAA
    uint2 dimensions;
    uint sampleCount;
    tDepthMap.GetDimensions(dimensions.x, dimensions.y, sampleCount);
    samplePos = min(samplePos, dimensions - 1);

    float depthMin = 10000000000000000.0f;
    float depthMax = 0.0f;

    for(uint sampleIdx = 0; sampleIdx < sampleCount; ++sampleIdx)
    {
        float depth = tDepthMap.Load(samplePos, sampleIdx);
        if(depth > 0.0f)
        {
            depth = LinearizeDepth(depth, cNear, cFar);
            depthMin = min(depthMin, depth);
            depthMax = max(depthMax, depth);
        }
    }
    gsDepthSamples[input.GroupIndex] = float2(depthMin, depthMax);
#else
    uint2 dimensions;
    tDepthMap.GetDimensions(dimensions.x, dimensions.y);
    samplePos = min(samplePos, dimensions - 1);
    float depth = tDepthMap[samplePos];
    if(depth > 0.0f)
    {
        depth = LinearizeDepth(depth, cNear, cFar);
    }
    gsDepthSamples[input.GroupIndex] = float2(depth, depth);

#endif

    GroupMemoryBarrierWithGroupSync();

    for(uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
    {
        if(input.GroupIndex < s)
        {
            gsDepthSamples[input.GroupIndex].x = min(gsDepthSamples[input.GroupIndex].x, gsDepthSamples[input.GroupIndex + s].x);
            gsDepthSamples[input.GroupIndex].y = max(gsDepthSamples[input.GroupIndex].y, gsDepthSamples[input.GroupIndex + s].y);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if(input.GroupIndex == 0)
    {
        uOutputMap[input.GroupId.xy] = gsDepthSamples[0];
    }
}

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void ReduceDepth(CS_INPUT input)
{
    uint2 dimensions;
    tReductionMap.GetDimensions(dimensions.x, dimensions.y);

    uint2 samplePos = input.GroupId.xy * BLOCK_SIZE + input.GroupThreadId.xy;
    samplePos = min(samplePos, dimensions - 1);

    float minDepth = tReductionMap[samplePos].x;
    float maxDepth = tReductionMap[samplePos].y;
    gsDepthSamples[input.GroupIndex] = float2(minDepth, maxDepth);

    GroupMemoryBarrierWithGroupSync();

    for(uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
    {
        if(input.GroupIndex < s)
        {
            gsDepthSamples[input.GroupIndex].x = min(gsDepthSamples[input.GroupIndex].x, gsDepthSamples[input.GroupIndex + s].x);
            gsDepthSamples[input.GroupIndex].y = max(gsDepthSamples[input.GroupIndex].y, gsDepthSamples[input.GroupIndex + s].y);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if(input.GroupIndex == 0)
    {
        uOutputMap[input.GroupId.xy] = gsDepthSamples[0];
    }
}