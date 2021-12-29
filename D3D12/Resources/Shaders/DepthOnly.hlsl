#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=2, b0), " \
				"CBV(b1), " \
				"CBV(b100)")

struct PassData
{
	float4x4 ViewProjection;
};

ConstantBuffer<PerObjectData> cObject : register(b0);
ConstantBuffer<PassData> cPass : register(b1);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS result = (InterpolantsVSToPS)0;
	MeshInstance instance = GetMeshInstance(cObject.Index);
	MeshData mesh = GetMesh(instance.Mesh);
	ByteAddressBuffer meshBuffer = tBufferTable[mesh.BufferIndex];

	float3 position = UnpackHalf3(meshBuffer.Load<uint2>(mesh.PositionsOffset + vertexId * sizeof(uint2)));
	result.Position = mul(mul(float4(position, 1.0f), instance.World), cPass.ViewProjection);
	result.UV = UnpackHalf2(meshBuffer.Load<uint>(mesh.UVsOffset + vertexId * sizeof(uint)));
	return result;
}

void PSMain(InterpolantsVSToPS input)
{
	MeshInstance instance = GetMeshInstance(cObject.Index);
	MaterialData material = GetMaterial(instance.Material);
	if(Sample2D(material.Diffuse, sMaterialSampler, input.UV).a < material.AlphaCutoff)
	{
		discard;
	}
}
