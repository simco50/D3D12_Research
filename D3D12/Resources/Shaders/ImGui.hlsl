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

struct VertexParams
{
	float4 ScaleBias;
};

struct PixelParams
{
	uint TextureIndex;
};

ConstantBuffer<PixelParams> cPixelParams : register(b0);
ConstantBuffer<VertexParams> cVertexParams : register(b0);

InterpolantsVSToPS VSMain(VertexInput input)
{
	InterpolantsVSToPS output = (InterpolantsVSToPS)0;
	output.Position = float4(input.Position * cVertexParams.ScaleBias.xy + cVertexParams.ScaleBias.zw, 0.0f, 1.0f);
	output.UV = input.UV;
	output.Color = input.Color;
	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	return input.Color * Sample2D(cPixelParams.TextureIndex, sPointWrap, input.UV);
}
