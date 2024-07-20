#include "Common.hlsli"

struct SkinnedMeshData
{
    float4x4 SkinTransforms[128];
    uint PositionsOffset;
    uint NormalsOffset;
    uint JointsOffset;
    uint WeightsOffset;
    uint SkinnedPositionsOffset;
    uint SkinnedNormalsOffset;
    uint NumTriangles;
};

ConstantBuffer<SkinnedMeshData>   cMeshInfo            : register(b0, space1);
RWByteAddressBuffer               uMeshData           : register(u0);

struct JointIndices
{
    uint A : 16;
    uint B : 16;
    uint C : 16;
    uint D : 16;
};

[numthreads(64, 1, 1)]
void CSMain(uint threadID : SV_DispatchThreadID)
{
    if(threadID >= cMeshInfo.NumTriangles)
        return;

    float3 position = ByteBufferLoad<float3>(uMeshData, threadID, cMeshInfo.PositionsOffset);

	uint2 normalData    = ByteBufferLoad<uint2>(uMeshData, threadID, cMeshInfo.NormalsOffset);
	float3 normal       = RGB10A2_SNORM::Unpack(normalData.x).xyz;
	float4 tangent      = RGB10A2_SNORM::Unpack(normalData.y);

    JointIndices jointIndices   = ByteBufferLoad<JointIndices>(uMeshData, threadID, cMeshInfo.JointsOffset);
    float4 jointWeights         = RGBA16_FLOAT::Unpack(uMeshData.Load<uint2>(cMeshInfo.WeightsOffset + threadID * sizeof(uint2)));

    float4x4 skinTransform = 
        cMeshInfo.SkinTransforms[jointIndices.A] * jointWeights[0] +
        cMeshInfo.SkinTransforms[jointIndices.B] * jointWeights[1] +
        cMeshInfo.SkinTransforms[jointIndices.C] * jointWeights[2] +
        cMeshInfo.SkinTransforms[jointIndices.D] * jointWeights[3];
    
    float3 transformedPosition  = mul(float4(position, 1), skinTransform).xyz;
    float3 transformedNormal    = mul(normal, (float3x3)skinTransform);
    float4 transformedTangent   = float4(mul(tangent.xyz, (float3x3)skinTransform), tangent.w);
    
    uint2 packedNormal = uint2(
        RGB10A2_SNORM::Pack(float4(transformedNormal, 0)),
        RGB10A2_SNORM::Pack(transformedTangent));

    ByteBufferStore<float3>(uMeshData, transformedPosition, threadID, cMeshInfo.SkinnedPositionsOffset);
    ByteBufferStore<uint2>(uMeshData, packedNormal, threadID, cMeshInfo.SkinnedNormalsOffset);
}
