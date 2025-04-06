#include "Common.hlsli"

struct PassParams
{
    uint SkinMatrixOffset;
    uint PositionsOffset;
    uint NormalsOffset;
    uint JointsOffset;
    uint WeightsOffset;
    uint SkinnedPositionsOffset;
    uint SkinnedNormalsOffset;
    uint NumVertices;

    StructuredBufferH<float4x4> SkinMatrices;
    RWByteBufferH               MeshData;
};
DEFINE_CONSTANTS(PassParams, 0);

[numthreads(64, 1, 1)]
void CSMain(uint threadID : SV_DispatchThreadID)
{
    if(threadID >= cPassParams.NumVertices)
        return;

    float3 position = cPassParams.MeshData.LoadStructure<float3>(threadID, cPassParams.PositionsOffset);

	uint2 normalData    = cPassParams.MeshData.LoadStructure<uint2>(threadID, cPassParams.NormalsOffset);
	float3 normal       = RGB10A2_SNORM::Unpack(normalData.x).xyz;
	float4 tangent      = RGB10A2_SNORM::Unpack(normalData.y);

    uint4 jointIndices   = cPassParams.MeshData.LoadStructure<uint16_t4>(threadID, cPassParams.JointsOffset);
    float4 jointWeights  = RGBA16_FLOAT::Unpack(cPassParams.MeshData.Load<uint2>(cPassParams.WeightsOffset + threadID * sizeof(uint2)));

    float4x4 skinTransform = 0;
    [unroll]
    for(int i = 0; i < 4; ++i)
        skinTransform += cPassParams.SkinMatrices[jointIndices[i] + cPassParams.SkinMatrixOffset] * jointWeights[i];
    
    float3 transformedPosition  = mul(float4(position, 1), skinTransform).xyz;
    float3 transformedNormal    = mul(normal, (float3x3)skinTransform);
    float4 transformedTangent   = float4(mul(tangent.xyz, (float3x3)skinTransform), tangent.w);
    
    uint2 packedNormal = uint2(
        RGB10A2_SNORM::Pack(float4(transformedNormal, 0)),
        RGB10A2_SNORM::Pack(transformedTangent));

    cPassParams.MeshData.StoreStructure<float3>(threadID, cPassParams.SkinnedPositionsOffset, transformedPosition);
    cPassParams.MeshData.StoreStructure<uint2>(threadID, cPassParams.SkinnedNormalsOffset, packedNormal);
}
