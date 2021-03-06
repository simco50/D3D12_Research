#include "Common.hlsli"
#include "CommonBindings.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t1000, numDescriptors = 128, space = 2), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), "

struct PerObjectData
{
	float4x4 World;
	int Diffuse;
    int Normal;
    int Roughness;
    int Metallic;
};

struct PerViewData
{
	float4x4 ViewProjection;
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
	PSInput result = (PSInput)0;
	result.position = mul(mul(float4(input.position, 1.0f), cObjectData.World), cViewData.ViewProjection);
	result.texCoord = input.texCoord;
	return result;
}

void PSMain(PSInput input)
{
	if(tTableTexture2D[cObjectData.Diffuse].Sample(sDiffuseSampler, input.texCoord).a < 0.5f)
	{
		discard;
	}
}