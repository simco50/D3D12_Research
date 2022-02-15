#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "VisibilityBuffer.hlsli"

ConstantBuffer<InstanceData> cObject : register(b0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

InterpolantsVSToPS FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	InterpolantsVSToPS result;
	float3 Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	float3 positionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.ViewProjection);
	result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
	return result;
}

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	InterpolantsVSToPS result = FetchVertexAttributes(mesh, world, vertexId);
	return result;
}

VisBufferData PSMain(
    InterpolantsVSToPS input,
    uint primitiveID : SV_PrimitiveID) : SV_TARGET0
{
#ifdef ALPHA_TEST
	MaterialData material = GetMaterial(cObject.Material);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse != INVALID_HANDLE)
	{
		opacity *= Sample2D(material.Diffuse, sMaterialSampler, input.UV).a;
	}
	if(opacity < material.AlphaCutoff)
	{
		discard;
	}
#endif

	VisBufferData Data;
	Data.ObjectID = cObject.World;
	Data.PrimitiveID = primitiveID;
	return Data;
}
