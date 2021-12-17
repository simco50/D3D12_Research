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

struct PassData
{
	float4x4 ViewInverse;
	float4x4 View;
	float4x4 Projection;
};

ConstantBuffer<PassData> cPassData : register(b0);
StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
	float4 Color : COLOR;
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
InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;

	uint vertexID = vertexId % 6;
	uint instanceID = vertexId / 6;

	uint particleIndex = tAliveList[instanceID];
	ParticleData particle = tParticleData[particleIndex];
	float3 q = particle.Size * BILLBOARD[vertexID];

	output.Position = float4(mul(q, (float3x3)cPassData.ViewInverse), 1);
	output.Position.xyz += particle.Position;
	output.Position = mul(output.Position, cPassData.View);
	output.Position = mul(output.Position, cPassData.Projection);
	output.Color = float4(10000, 0, 1, 1);
	output.UV = (BILLBOARD[vertexID].xy + 1) / 2.0f;

	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	float alpha = 1 - saturate(2 * length(input.UV.xy - 0.5f));
	return float4(1, 1, 1, alpha);
}
