#include "Common.hlsli"

struct VertexInput
{
	float2 Position : POSITION;
	float2 UV : TEXCOORD;
	float4 Color : COLOR;
};

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
	float4 Color : COLOR;
};

struct PassParameters
{
	float4x4 Projection;
};

ConstantBuffer<PassParameters> cPass : register(b0);
Texture2D tTexture : register(t0);

InterpolantsVSToPS VSMain(VertexInput input)
{
	InterpolantsVSToPS output = (InterpolantsVSToPS)0;
	output.Position = mul(float4(input.Position, 0.0f, 1.0f), cPass.Projection);
	output.UV = input.UV;
	output.Color = input.Color;
	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	float4 color = tTexture.Sample(sPointWrap, input.UV);
	return input.Color * color;
}
