#include "Common.hlsli"

struct SkinnedMeshData
{
    uint SkinMatrixOffset;
    uint PositionsOffset;
    uint NormalsOffset;
    uint JointsOffset;
    uint WeightsOffset;
    uint SkinnedPositionsOffset;
    uint SkinnedNormalsOffset;
    uint NumVertices;
};

ConstantBuffer<SkinnedMeshData>   cMeshInfo             : register(b0);
RWByteAddressBuffer               uMeshData             : register(u0);
StructuredBuffer<float4x4>        tSkinMatrices         : register(t0);

[numthreads(64, 1, 1)]
void CSMain(uint threadID : SV_DispatchThreadID)
{
    if(threadID >= cMeshInfo.NumVertices)
        return;

    float3 position = ByteBufferLoad<float3>(uMeshData, threadID, cMeshInfo.PositionsOffset);

	uint2 normalData    = ByteBufferLoad<uint2>(uMeshData, threadID, cMeshInfo.NormalsOffset);
	float3 normal       = RGB10A2_SNORM::Unpack(normalData.x).xyz;
	float4 tangent      = RGB10A2_SNORM::Unpack(normalData.y);

    uint4 jointIndices   = ByteBufferLoad<uint16_t4>(uMeshData, threadID, cMeshInfo.JointsOffset);
    float4 jointWeights  = RGBA16_FLOAT::Unpack(uMeshData.Load<uint2>(cMeshInfo.WeightsOffset + threadID * sizeof(uint2)));

    float4x4 skinTransform = 0;
    [unroll]
    for(int i = 0; i < 4; ++i)
        skinTransform += tSkinMatrices[jointIndices[i] + cMeshInfo.SkinMatrixOffset] * jointWeights[i];
    
    float3 transformedPosition  = mul(float4(position, 1), skinTransform).xyz;
    float3 transformedNormal    = mul(normal, (float3x3)skinTransform);
    float4 transformedTangent   = float4(mul(tangent.xyz, (float3x3)skinTransform), tangent.w);
    
    uint2 packedNormal = uint2(
        RGB10A2_SNORM::Pack(float4(transformedNormal, 0)),
        RGB10A2_SNORM::Pack(transformedTangent));

    ByteBufferStore<float3>(uMeshData, transformedPosition, threadID, cMeshInfo.SkinnedPositionsOffset);
    ByteBufferStore<uint2>(uMeshData, packedNormal, threadID, cMeshInfo.SkinnedNormalsOffset);
}
