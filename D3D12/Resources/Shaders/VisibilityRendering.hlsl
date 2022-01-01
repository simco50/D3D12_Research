#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=3, b0), " \
                "CBV(b100), " \
                "DescriptorTable(SRV(t10, numDescriptors = 11))")

ConstantBuffer<InstanceData> cObject : register(b0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;

    MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	ByteAddressBuffer meshBuffer = tBufferTable[mesh.BufferIndex];

    float3 position = UnpackHalf3(BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.PositionsOffset));
    output.Position = mul(mul(float4(position, 1.0f), world), cView.ViewProjection);
    output.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
	return output;
}

void PSMain(
    InterpolantsVSToPS input,
    uint primitiveIndex : SV_PrimitiveID,
    out uint outPrimitiveMask : SV_TARGET0)
{
#ifdef ALPHA_TEST
	MaterialData material = GetMaterial(cObject.Material);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse != INVALID_HANDLE)
	{
		opacity *= Sample2D(material.Diffuse, sMaterialSampler, input.UV).a;
	}
	if(opacity < 0.5f)
	{
		discard;
	}
#endif

    outPrimitiveMask = (cObject.World << 16) | (primitiveIndex & 0xFFFF);
}
