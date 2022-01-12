
struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 PositionWS : POSITION_WS;
	float3 PositionVS : POSITION_VS;
	float2 UV : TEXCOORD;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	uint Seed : SEED;
};

Texture2D tFoo;
SamplerState sLinearClamp;

float4 GetColor(InterpolantsVSToPS interpolants)
{
	%code%
	return _local_2;
}

void PSMain(InterpolantsVSToPS input,
			out float4 outColor : SV_Target0)
{
	outColor = GetColor(input);
}
