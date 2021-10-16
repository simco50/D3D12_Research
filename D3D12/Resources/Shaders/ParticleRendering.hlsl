#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2), visibility=SHADER_VISIBILITY_VERTEX)")

struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
    float Size;
};

cbuffer FrameData : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cView;
    float4x4 cProjection;
}

StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

static const float3 BILLBOARD[] = {
	float3(-1, -1, 0),	// 0
	float3(1, -1, 0),	// 1
	float3(-1, 1, 0),	// 2
	float3(-1, 1, 0),	// 3
	float3(1, -1, 0),	// 4
	float3(1, 1, 0),	// 5
};

[RootSignature(RootSig)]
PS_Input VSMain(uint vertexId : SV_VertexID)
{
    PS_Input output;

    uint vertexID = vertexId % 6;
	uint instanceID = vertexId / 6;

    uint particleIndex = tAliveList[instanceID];
    ParticleData particle = tParticleData[particleIndex];
    float3 q = particle.Size * BILLBOARD[vertexID];
    
    output.position = float4(mul(q, (float3x3)cViewInverse), 1);
    output.position.xyz += particle.Position;
    output.position = mul(output.position, cView);
    output.position = mul(output.position, cProjection);
    output.color = float4(10000, 0, 1, 1);
    output.texCoord = (BILLBOARD[vertexID].xy + 1) / 2.0f;

    return output;
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    float alpha = 1 - saturate(2 * length(input.texCoord.xy - 0.5f));
    return float4(1, 1, 1, alpha);
}