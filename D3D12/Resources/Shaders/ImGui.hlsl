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

struct PixelParams
{
	float4x4 Projection;
};

struct VertexParams
{
	uint TextureIndex;
};

ConstantBuffer<VertexParams> cVertexParams : register(b0);
ConstantBuffer<PixelParams> cPixelParams : register(b0);

InterpolantsVSToPS VSMain(VertexInput input)
{
	InterpolantsVSToPS output = (InterpolantsVSToPS)0;
	output.Position = mul(float4(input.Position, 0.0f, 1.0f), cPixelParams.Projection);
	output.UV = input.UV;
	output.Color = input.Color;
	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	return input.Color * Sample2D(cVertexParams.TextureIndex, sPointWrap, input.UV);
}
