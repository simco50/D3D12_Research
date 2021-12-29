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

struct InterpolantsVSToPS
{
	float4 PositionCS : SV_Position;
	float3 UV : TEXCOORD;
};

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;
	float3 positionVS = mul(CUBE[vertexId].xyz, (float3x3)cView.View);
	output.PositionCS = mul(float4(positionVS, 1.0f), cView.Projection);
	output.PositionCS.z = 0.0001f;
	output.UV = CUBE[vertexId].xyz;
	return output;
}

float4 PSMain(in InterpolantsVSToPS input) : SV_Target
{
	float3 dir = normalize(input.UV);
	return float4(CIESky(dir), 1.0f);
}
