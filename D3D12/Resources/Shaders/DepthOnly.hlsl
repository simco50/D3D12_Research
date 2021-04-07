#include "Common.hlsli"
#include "CommonBindings.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
				GLOBAL_BINDLESS_TABLE ", " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), "

struct PerObjectData
{
	float4x4 World;
	int Diffuse;
    int Normal;
    int RoughnessMetalness;
	uint VertexBuffer;
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
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

[RootSignature(RootSig)]
PSInput VSMain(uint VertexId : SV_VertexID)
{
	PSInput result = (PSInput)0;
	VSInput input = tBufferTable[cObjectData.VertexBuffer].Load<VSInput>(VertexId * sizeof(VSInput));
	result.position = mul(mul(float4(input.position, 1.0f), cObjectData.World), cViewData.ViewProjection);
	result.texCoord = input.texCoord;
	return result;
}

void PSMain(PSInput input)
{
	if(tTexture2DTable[cObjectData.Diffuse].Sample(sDiffuseSampler, input.texCoord).a < 0.5f)
	{
		discard;
	}
}