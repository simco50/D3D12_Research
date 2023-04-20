#include "Common.hlsli"
// Hacky shader to resolve 4xMSAA depth texture

Texture2DMS<float> tDepth : register(t0);

float4 VSMain(uint vertexID : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

void PSMain(float4 position : SV_Position, out float outDepth : SV_Depth)
{
	uint2 texel = (uint2)floor(position.xy);
	uint2 rem = texel % 2;
	uint sampleIndex = rem.x + rem.y * 2;
	outDepth = tDepth.Load(texel >> 1, sampleIndex);
}
