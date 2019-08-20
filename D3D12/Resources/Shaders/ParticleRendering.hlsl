struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
};

cbuffer FrameData : register(b0)
{
    float4x4 cViewInverse;
    float4x4 cViewProjection;
}

StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct VS_Input
{
	uint vertexId : SV_VERTEXID;
};

struct PS_Input
{
    float4 position : SV_POSITION;
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

PS_Input VSMain(VS_Input input)
{
    PS_Input output;

    uint vertexID = input.vertexId % 6;
	uint instanceID = input.vertexId / 6;

    uint particleIndex = tAliveList[instanceID];
    ParticleData particle = tParticleData[particleIndex];
    float3 q = 10*BILLBOARD[vertexID];
    
    output.position = float4(mul(q + particle.Position, (float3x3)cViewInverse), 1);
    output.position = mul(output.position, cViewProjection);
    output.color = float4(1, 1, 0, 1);

    return output;
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    return input.color;
}