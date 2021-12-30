#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=3, b0), " \
				"CBV(b100)")

ConstantBuffer<InstanceData> cObject : register(b0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS result = (InterpolantsVSToPS)0;
	MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	ByteAddressBuffer meshBuffer = tBufferTable[mesh.BufferIndex];

	float3 position = UnpackHalf3(meshBuffer.Load<uint2>(mesh.PositionsOffset + vertexId * sizeof(uint2)));
	result.Position = mul(mul(float4(position, 1.0f), world), cView.ViewProjection);
	result.UV = UnpackHalf2(meshBuffer.Load<uint>(mesh.UVsOffset + vertexId * sizeof(uint)));
	return result;
}

void PSMain(InterpolantsVSToPS input)
{
	MaterialData material = GetMaterial(cObject.Material);
	if(Sample2D(material.Diffuse, sMaterialSampler, input.UV).a < material.AlphaCutoff)
	{
		discard;
	}
}
