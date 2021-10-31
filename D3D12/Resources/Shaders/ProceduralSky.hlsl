#include "CommonBindings.hlsli"
#include "SkyCommon.hlsli"

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

struct Constants
{
	float4x4 View;
	float4x4 Projection;
	float3 SunDirection;
};

ConstantBuffer<Constants> cConstants : register(b0);

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
	VSOutput output;
    float3 positionVS = mul(CUBE[vertexId].xyz, (float3x3)cConstants.View);
    output.PositionCS = mul(float4(positionVS, 1.0f), cConstants.Projection);
	output.PositionCS.z = 0.0001f;
	output.TexCoord = CUBE[vertexId].xyz;
	return output;
}

float4 PSMain(in VSOutput input) : SV_Target
{
	float3 dir = normalize(input.TexCoord);
	return float4(CIESky(dir, cConstants.SunDirection), 1.0f);
}
