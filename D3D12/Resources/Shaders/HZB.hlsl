#include "Common.hlsli"
#include "HZB.hlsli"

struct PassParameters
{
    float2 DimensionsInv;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture2D<float> uHZB : register(u0);
Texture2D<float> tSource : register(t0);

[numthreads(16, 16, 1)]
void HZBCreateCS(uint3 threadID : SV_DispatchThreadID)
{
    float2 uv = ((float2)threadID.xy + 0.5f) * cPass.DimensionsInv;
    float4 depths = tSource.Gather(sPointClamp, uv);
    float minDepth = min(min(min(depths.x, depths.y), depths.z), depths.w);
    uHZB[threadID.xy] = minDepth;
}
