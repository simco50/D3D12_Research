#include "Common.hlsli"


struct DebugVertex
{
	float3 Position	: POSITION;
	float4 Color	: COLOR;
};

StructuredBuffer<DebugVertex> tVertices : register(t0);
Texture2D<float> tDepth : register(t1);

void VSMain(
	DebugVertex v,
	out float4 outPosition : SV_Position,
	out float4 outColor : COLOR)
{
	outPosition = mul(float4(v.Position, 1.0f), cView.WorldToClipUnjittered);
	outColor = v.Color;
}

void PSMain(
	float4 position : SV_Position,
	float4 color : COLOR,
	out float4 outColor : SV_Target)
{
	uint2 texel = (uint2)position.xy;
	float depth = tDepth[texel];
	if(depth > position.z)
		color.a *= 0.5f;
	
	outColor = color;
}
