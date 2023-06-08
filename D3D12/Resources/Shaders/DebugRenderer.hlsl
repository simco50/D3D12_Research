#include "Common.hlsli"

struct Vertex
{
	float3 Position;
	uint Color;
};

StructuredBuffer<Vertex> tVertices : register(t0);

void VSMain(
	uint vertexID : SV_VertexID,
	out float4 outPosition : SV_Position,
	out float4 outColor : COLOR)
{
	Vertex v = tVertices[vertexID];
	outPosition = mul(float4(v.Position, 1.0f), cView.ViewProjection);
	outColor = Unpack_RGBA8_UNORM(v.Color);
}

void PSMain(
	float4 position : SV_Position, 
	float4 color : COLOR,
	out float4 outColor : SV_Target)
{
	outColor = color;
}
