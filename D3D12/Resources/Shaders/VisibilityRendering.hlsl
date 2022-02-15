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

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;

    MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	ByteAddressBuffer meshBuffer = tBufferTable[mesh.BufferIndex];

    float3 position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
    output.Position = mul(mul(float4(position, 1.0f), world), cView.ViewProjection);
    output.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
	return output;
}

VisBufferData PSMain(
    InterpolantsVSToPS input,
    uint primitiveIndex : SV_PrimitiveID) : SV_TARGET0
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
	Data.PrimitiveID = primitiveIndex;
	return Data;
}
