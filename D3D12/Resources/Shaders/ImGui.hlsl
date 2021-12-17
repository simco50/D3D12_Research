#include "CommonBindings.hlsli"

struct ConstantsData
{
	float4x4 ViewProj;
	uint TextureID;
	uint TextureType;
};

ConstantBuffer<ConstantsData> cConstants : register(b0);

struct VertexInput
{
	float2 Position : POSITION;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

InterpolantsVSToPS VSMain(VertexInput input)
{
	InterpolantsVSToPS output = (InterpolantsVSToPS)0;

	output.Position = mul(float4(input.Position.xy, 0.5f, 1.f), cConstants.ViewProj);
	output.Color = input.Color;
	output.UV = input.UV;

	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	float4 texSample = tTexture2DTable[cConstants.TextureID].SampleLevel(sMaterialSampler, input.UV, 0);
	return input.Color * texSample;
}
