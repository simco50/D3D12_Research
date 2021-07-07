#include "Common.hlsli"
#include "CommonBindings.hlsli"

#define RootSig \
				"RootConstants(num32BitConstants=2, b0), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t11, numDescriptors = 1)), " \
				"DescriptorTable(UAV(u1, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
				GLOBAL_BINDLESS_TABLE ", " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"

struct ObjectData
{
	uint Mesh;
	uint Material;
};

struct ViewData
{
    int4 ClusterDimensions;
    int2 ClusterSize;
	float2 LightGridParams;
    float4x4 View;
    float4x4 ViewProjection;
};

ConstantBuffer<ObjectData> cObjectData : register(b0);
ConstantBuffer<ViewData> cViewData : register(b1);

RWStructuredBuffer<uint> uActiveClusters : register(u1);

uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cViewData.LightGridParams.x - cViewData.LightGridParams.y);
}

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 positionVS : VIEWSPACE_POSITION;
    float2 texCoord : TEXCOORD;
};

[RootSignature(RootSig)]
PSInput MarkClusters_VS(uint VertexId : SV_VertexID)
{
    PSInput output = (PSInput)0;
    MeshData mesh = tMeshes[cObjectData.Mesh];
	VSInput input = tBufferTable[mesh.VertexBuffer].Load<VSInput>(VertexId * sizeof(VSInput));
    float4 wPos = mul(float4(input.position, 1), mesh.World);
    output.positionVS = mul(wPos, cViewData.View);
    output.position = mul(wPos, cViewData.ViewProjection);
    output.texCoord = input.texCoord;
    return output;
}

[earlydepthstencil]
void MarkClusters_PS(PSInput input)
{
    uint3 clusterIndex3D = uint3(floor(input.position.xy / cViewData.ClusterSize), GetSliceFromDepth(input.positionVS.z));
    uint clusterIndex1D = clusterIndex3D.x + (cViewData.ClusterDimensions.x * (clusterIndex3D.y + cViewData.ClusterDimensions.y * clusterIndex3D.z));
    uActiveClusters[clusterIndex1D] = 1;
}