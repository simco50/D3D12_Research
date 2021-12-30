#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b100)")

struct VertexInput
{
	float3 Position : POSITION;
	uint Color : COLOR;
};

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float4 Color : COLOR;
};

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(VertexInput input)
{
	InterpolantsVSToPS output = (InterpolantsVSToPS)0;

	output.Position = mul(float4(input.Position, 1.0f), cView.ViewProjection);
	output.Color = UIntToColor(input.Color);

	return output;
}

float4 PSMain(InterpolantsVSToPS input) : SV_Target
{
	return input.Color;
}
