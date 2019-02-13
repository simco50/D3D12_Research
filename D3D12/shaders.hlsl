cbuffer Data : register(b0)
{
	float4 Color;
}

struct VSInput
{
	float3 position : POSITION;
	float4 color : COLOR;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = float4(input.position, 1.0f);
	result.color = input.color;

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}