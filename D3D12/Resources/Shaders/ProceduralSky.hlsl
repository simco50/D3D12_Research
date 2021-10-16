#include "CommonBindings.hlsli"
#include "SkyCommon.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_ALL)")

static const float4 CUBE[]=
{
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(-1.0,1.0,1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,-1.0,1.0),
	float4(-1.0,-1.0,1.0,1.0),
	float4(1.0,-1.0,-1.0,1.0),
	float4(1.0,1.0,-1.0,1.0),
	float4(1.0,1.0,1.0,1.0),
	float4(-1.0,1.0,-1.0,1.0),
};

cbuffer VSConstants : register(b0)
{
	float4x4 cView;
	float4x4 cProjection;
	float3 cBias;
	float3 cSunDirection;
}

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

[RootSignature(RootSig)]
VSOutput VSMain(uint vertexId : SV_VertexID)
{
	VSOutput output;
    float3 positionVS = mul(CUBE[vertexId].xyz, (float3x3)cView);
    output.PositionCS = mul(float4(positionVS, 1.0f), cProjection);
	output.PositionCS.z = 0.0001f;
	output.TexCoord = CUBE[vertexId].xyz;
	return output;
}

float4 PSMain(in VSOutput input) : SV_Target
{
	float3 dir = normalize(input.TexCoord);
	return float4(CIESky(dir, cSunDirection), 1.0f);
}
