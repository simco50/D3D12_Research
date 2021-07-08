#include "Common.hlsli"
#include "CommonBindings.hlsli"

#define RootSig \
				"RootConstants(num32BitConstants=2, b0), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t10, numDescriptors = 11)), " \
				GLOBAL_BINDLESS_TABLE ", " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), "

struct PerObjectData
{
	uint Mesh;
	uint Material;
};

struct PerViewData
{
	float4x4 ViewProjection;
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct Vertex
{
	uint2 position;
	uint texCoord;
	float3 normal;
	float4 tangent;
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
    MeshData mesh = tMeshes[cObjectData.Mesh];
	Vertex input = tBufferTable[mesh.VertexBuffer].Load<Vertex>(VertexId * sizeof(Vertex));
	result.position = mul(mul(float4(UnpackHalf3(input.position), 1.0f), mesh.World), cViewData.ViewProjection);
	result.texCoord = UnpackHalf2(input.texCoord);
	return result;
}

void PSMain(PSInput input)
{
	MaterialData material = tMaterials[cObjectData.Material];
	if(tTexture2DTable[material.Diffuse].Sample(sDiffuseSampler, input.texCoord).a < material.AlphaCutoff)
	{
		discard;
	}
}