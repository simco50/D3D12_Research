#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=3, b0), " \
                "CBV(b100), " \
                "DescriptorTable(SRV(t10, numDescriptors = 11))")

ConstantBuffer<InstanceData> cObject : register(b0);

struct Vertex
{
    uint2 Position;
    uint UV;
    float3 Normal;
    float4 Tangent;
};

[RootSignature(RootSig)]
float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION
{
    MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	ByteAddressBuffer meshBuffer = tBufferTable[mesh.BufferIndex];

    float3 position = UnpackHalf3(meshBuffer.Load<uint2>(mesh.PositionsOffset + vertexId * sizeof(uint2)));
    return mul(mul(float4(position, 1.0f), world), cView.ViewProjection);
}

void PSMain(
    float4 position : SV_POSITION,
    uint primitiveIndex : SV_PrimitiveID,
    float3 barycentrics : SV_Barycentrics,
    out uint outPrimitiveMask : SV_TARGET0)
{
    outPrimitiveMask = (cObject.World << 16) | (primitiveIndex & 0xFFFF);
}
