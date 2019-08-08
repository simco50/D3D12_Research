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

RWStructuredBuffer<uint> uActiveClusters : register(u1);

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
    uint3 clusterIndex3D = uint3(floor(input.position.xy / cClusterSize), GetSliceFromDepth(input.positionVS.z));
    uint clusterIndex1D = clusterIndex3D.x + (cClusterDimensions.x * (clusterIndex3D.y + cClusterDimensions.y * clusterIndex3D.z));

#ifdef ALPHA_BLEND
    float s = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord).a;
    if(s < 0.01f)
    {
        discard;
    }
    uActiveClusters[clusterIndex1D] = ceil(s);
#else
    uActiveClusters[clusterIndex1D] = 1;
#endif
}