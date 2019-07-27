#include "Common.hlsl"

cbuffer Parameters : register(b0)
{
    float4x4 cWorldView;
    float4x4 cProjection;
    uint4 cClusterDimensions;
    float2 cClusterSize;
	float cSliceMagicA;
	float cSliceMagicB;
}

RWStructuredBuffer<uint> uUniqueClusters : register(u1);

SamplerState sDiffuseSampler : register(s0);
Texture2D tDiffuseTexture : register(t0);

uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cSliceMagicA - cSliceMagicB);
}

struct VS_Input
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float4 positionVS : VIEWSPACE_POSITION;
    float2 texCoord : TEXCOORD;
};

PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    output.positionVS = mul(float4(input.position, 1), cWorldView);
    output.position = mul(output.positionVS, cProjection);
    output.texCoord = input.texCoord;
    return output;
}

void MarkClusters_PS(PS_Input input)
{
    uint zSlice = GetSliceFromDepth(input.positionVS.z);
    uint2 clusterIndexXY = floor(input.position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + (clusterIndexXY.y * cClusterDimensions.x) + (zSlice * (cClusterDimensions.x * cClusterDimensions.y));

#ifdef ALPHA_BLEND
    float s = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord).a;
    if(s < 0.01f)
    {
        discard;
    }
    uUniqueClusters[clusterIndex1D] = ceil(s);
#else
    uUniqueClusters[clusterIndex1D] = 1;
#endif
}