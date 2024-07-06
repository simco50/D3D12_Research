#include "Common.hlsli"

void WithTexCoordVS(
    uint vertexID : SV_VertexID,
    out float4 position : SV_Position,
    out float2 uv : TEXCOORD)
{
    uv = float2((vertexID << 1) & 2, vertexID & 2);
	position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}