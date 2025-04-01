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

struct PassParams
{
	float4 ScaleBias;
	Texture2DH<float4> Texture;
};
DEFINE_CONSTANTS(PassParams, 0);

InterpolantsVSToPS VSMain(VertexInput input)
{
	InterpolantsVSToPS output = (InterpolantsVSToPS)0;
	output.Position = float4(input.Position * cPassParams.ScaleBias.xy + cPassParams.ScaleBias.zw, 0.0f, 1.0f);
	output.UV = input.UV;
	output.Color = input.Color;
	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	return input.Color * cPassParams.Texture.Sample(sPointWrap, input.UV);
}
