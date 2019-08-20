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

struct VS_Input
{
	uint vertexId : SV_VERTEXID;
};

struct GS_Input
{
    uint index : INDEX;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

GS_Input VSMain(VS_Input input)
{
    GS_Input output;
    output.index = input.vertexId;
    return output;
}

[maxvertexcount(4)]
void GSMain(point GS_Input input[1], inout TriangleStream<PS_Input> outputStream)
{
    static float size = 4;
    static float3 vertices[4] = {
        float3(-size, size, 0),
        float3(size, size, 0),
        float3(-size, -size, 0),
        float3(size, -size, 0)
    };

    ParticleData p = tParticleData[input[0].index];
    float3 transformedVertices[4];
    [unroll]
    for(uint i = 0; i < 4; ++i)
    {
        PS_Input vertex;
        vertex.position = mul(float4(mul(vertices[i], (float3x3)cViewInverse) + p.Position, 1), cViewProjection);
        vertex.color = float4(1, 1, 0, 1);
        outputStream.Append(vertex);
    }
}

float4 PSMain(PS_Input input) : SV_TARGET
{
    return input.color;
}