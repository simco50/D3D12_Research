cbuffer Parameters : register(b0)
{
    float4x4 cWorldView;
    float4x4 cProjection;
    float2 cScreenDimensions;
    float cNearZ;
    float cFarZ;
    uint4 cClusterDimensions;
    float2 cClusterSize;
    //float cFormulaConstantA;
    //float cFormulaConstantB;
}

RWStructuredBuffer<uint> uUniqueClusters : register(u1);

uint GetSliceFromDepth(float depth)
{
    float aConstant = cClusterDimensions.z / log(cFarZ / cNearZ);
    float bConstant = (cClusterDimensions.z * log(cNearZ)) / log(cFarZ / cNearZ);
    return floor(log(depth) * aConstant - bConstant);
}

struct VS_Input
{
    float3 position : POSITION;
};

struct PS_Input
{
    float4 position : SV_POSITION;
    float4 positionVS : VIEWSPACE_POSITION;
};

PS_Input MarkClusters_VS(VS_Input input)
{
    PS_Input output = (PS_Input)0;
    output.positionVS = mul(float4(input.position, 1), cWorldView);
    output.position = mul(output.positionVS, cProjection);
    return output;
}

void MarkClusters_PS(PS_Input input)
{
    uint zSlice = GetSliceFromDepth(input.positionVS.z);
    uint2 clusterIndexXY = floor(input.position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + (clusterIndexXY.y * cClusterDimensions.x) + (zSlice * (cClusterDimensions.x * cClusterDimensions.y));
    uUniqueClusters[clusterIndex1D] = 1;
}