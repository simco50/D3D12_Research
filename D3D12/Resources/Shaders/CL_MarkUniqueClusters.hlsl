#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(UAV(u1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer PerObjectParameters : register(b0)
{
    float4x4 cWorldView;
    float4x4 cWorldViewProjection;
}

cbuffer Parameters : register(b1)
{
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

[RootSignature(RootSig)]
PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    output.positionVS = mul(float4(input.position, 1), cWorldView);
    output.position = mul(float4(input.position, 1), cWorldViewProjection);
    output.texCoord = input.texCoord;
    return output;
}

[earlydepthstencil]
void MarkClusters_PS(PS_Input input)
{
    uint3 clusterIndex3D = uint3(floor(input.position.xy / cClusterSize), GetSliceFromDepth(input.positionVS.z));
    uint clusterIndex1D = clusterIndex3D.x + (cClusterDimensions.x * (clusterIndex3D.y + cClusterDimensions.y * clusterIndex3D.z));
    uActiveClusters[clusterIndex1D] = 1;
}