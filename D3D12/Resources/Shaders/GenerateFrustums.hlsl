#include "Common.hlsl"
#include "Constants.hlsl"

cbuffer ShaderParameters : register(b0)
{
    float4x4 cProjectionInverse;
    float2 cScreenDimensions;
    uint2 cNumThreads;
}

RWStructuredBuffer<Frustum> uOutFrustums : register(u0);

struct CS_INPUT
{
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
};

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    float3 eyePos = float3(0, 0, 0);

    // Compute the 4 corner points on the far clipping plane to use as the 
    // frustum vertices.
    float4 screenSpace[4];
    screenSpace[0] = float4(input.DispatchThreadId.xy * BLOCK_SIZE, 1.0f, 1.0f);
    screenSpace[1] = float4(float2(input.DispatchThreadId.x + 1, input.DispatchThreadId.y) * BLOCK_SIZE, 1.0f, 1.0f);
    screenSpace[2] = float4(float2(input.DispatchThreadId.x, input.DispatchThreadId.y + 1) * BLOCK_SIZE, 1.0f, 1.0f);
    screenSpace[3] = float4(float2(input.DispatchThreadId.x + 1, input.DispatchThreadId.y + 1) * BLOCK_SIZE, 1.0f, 1.0f);

    float3 viewSpace[4];
    for(int i = 0; i < 4; ++i)
    {
        viewSpace[i] = ScreenToView(screenSpace[i], cScreenDimensions, cProjectionInverse).xyz;
    }

    Frustum frustum;
    frustum.Planes[0] = CalculatePlane(eyePos, viewSpace[2], viewSpace[0]);
    frustum.Planes[1] = CalculatePlane(eyePos, viewSpace[1], viewSpace[3]);
    frustum.Planes[2] = CalculatePlane(eyePos, viewSpace[0], viewSpace[1]);
    frustum.Planes[3] = CalculatePlane(eyePos, viewSpace[3], viewSpace[2]);

    if(input.DispatchThreadId.x < cNumThreads.x && input.DispatchThreadId.y < cNumThreads.y)
    {
        uint arrayIndex = input.DispatchThreadId.x + (input.DispatchThreadId.y * cNumThreads.x);
        uOutFrustums[arrayIndex] = frustum;
    }
}