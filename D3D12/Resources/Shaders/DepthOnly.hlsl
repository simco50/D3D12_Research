#include "Common.hlsli"

ConstantBuffer<InstanceIndex> cObject : register(b0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS result = (InterpolantsVSToPS)0;
	InstanceData instance = GetInstance(cObject.ID);
	MeshData mesh = GetMesh(instance.MeshIndex);

	float3 position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	result.Position = mul(mul(float4(position, 1.0f), instance.LocalToWorld), cView.ViewProjection);
	result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
	return result;
}

void PSMain(InterpolantsVSToPS input)
{
	InstanceData instance = GetInstance(cObject.ID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	if(Sample2D(material.Diffuse, sMaterialSampler, input.UV).a < material.AlphaCutoff)
	{
		discard;
	}
}
