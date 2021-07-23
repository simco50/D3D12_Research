#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"

#define RootSig \
                "RootConstants(num32BitConstants=2, b0), " \
                "CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
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
    uint2 Position;
    uint UV;
    float3 Normal;
    float4 Tangent;
};

[RootSignature(RootSig)]
float4 VSMain(uint VertexId : SV_VertexID) : SV_POSITION
{
    MeshData mesh = tMeshes[cObjectData.Mesh];
    float3 Position = UnpackHalf3(tBufferTable[mesh.VertexBuffer].Load<uint2>(VertexId * sizeof(Vertex)));
    return mul(mul(float4(Position, 1.0f), mesh.World), cViewData.ViewProjection);
}

void PSMain(
    float4 position : SV_POSITION,
    uint primitiveIndex : SV_PrimitiveID,
    float3 barycentrics : SV_Barycentrics,
    out uint outPrimitiveMask : SV_TARGET0,
    out float2 ouBarycentrics : SV_TARGET1)
{
    outPrimitiveMask = (cObjectData.Mesh << 16) | (primitiveIndex & 0xFFFF);
    ouBarycentrics = barycentrics.xy;
}