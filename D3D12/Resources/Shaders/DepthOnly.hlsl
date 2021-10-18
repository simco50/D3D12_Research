#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=2, b0), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t10, numDescriptors = 11))")

struct PerViewData
{
	float4x4 ViewProjection;
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

[RootSignature(RootSig)]
PSInput VSMain(uint vertexId : SV_VertexID)
{
	PSInput result = (PSInput)0;
	MeshInstance instance = tMeshInstances[cObjectData.Index];
    MeshData mesh = tMeshes[instance.Mesh];

    float3 position = UnpackHalf3(LoadByteAddressData<uint2>(mesh.PositionStream, vertexId));
	result.position = mul(mul(float4(position, 1.0f), instance.World), cViewData.ViewProjection);
	result.texCoord = UnpackHalf2(LoadByteAddressData<uint>(mesh.UVStream, vertexId));
	return result;
}

void PSMain(PSInput input)
{
	MeshInstance instance = tMeshInstances[cObjectData.Index];
	MaterialData material = tMaterials[instance.Material];
	if(Sample2D(material.Diffuse, sMaterialSampler, input.texCoord).a < material.AlphaCutoff)
	{
		discard;
	}
}