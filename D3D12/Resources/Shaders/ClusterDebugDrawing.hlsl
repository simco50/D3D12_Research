#include "Common.hlsli"

#define RootSigVS "CBV(b0, visibility=SHADER_VISIBILITY_GEOMETRY), " \
				"DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_VERTEX), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

#define RootSigMS "CBV(b0, visibility=SHADER_VISIBILITY_MESH), " \
				"DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_MESH), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

cbuffer PerFrameData : register(b0)
{
	float4x4 cProjection;
}

StructuredBuffer<AABB> tAABBs : register(t0);
StructuredBuffer<uint> tCompactedClusters : register(t1);
StructuredBuffer<uint2> tLightGrid : register(t2);
Texture2D tHeatmapTexture : register(t3);
SamplerState sHeatmapSampler : register(s0);

struct GSInput
{
	float4 color : COLOR;
	float4 center : CENTER;
	float4 extents : EXTENTS;
    int lightCount : LIGHTCOUNT;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

[RootSignature(RootSigMS)]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void MSMain(
    in uint groupId : SV_GroupID,
    out vertices PSInput outVerts[8], 
    out indices uint3 outIndices[12])
{
    const uint vertexCount = 8;
    const uint primitiveCount = 12;

    uint clusterIndex = tCompactedClusters[groupId];
    uint lightCount = tLightGrid[clusterIndex].y;
    
    if(lightCount <= 0)
    {
        return;
    }

    SetMeshOutputCounts(vertexCount, primitiveCount);

    float4 color = tHeatmapTexture.SampleLevel(sHeatmapSampler, float2((float)lightCount / 30.0f, 0), 0);
    AABB aabb = tAABBs[clusterIndex];

    float4 center = aabb.Center;
    float4 extents = float4(aabb.Extents.xyz, 1);

    outVerts[0].position = mul(center + extents * float4(-1.0, -1.0,  1.0, 1.0), cProjection);
    outVerts[1].position = mul(center + extents * float4( 1.0, -1.0,  1.0, 1.0), cProjection);
    outVerts[2].position = mul(center + extents * float4( 1.0,  1.0,  1.0, 1.0), cProjection);
    outVerts[3].position = mul(center + extents * float4(-1.0,  1.0,  1.0, 1.0), cProjection);
    outVerts[4].position = mul(center + extents * float4(-1.0, -1.0, -1.0, 1.0), cProjection);
    outVerts[5].position = mul(center + extents * float4( 1.0, -1.0, -1.0, 1.0), cProjection);
    outVerts[6].position = mul(center + extents * float4( 1.0,  1.0, -1.0, 1.0), cProjection);
    outVerts[7].position = mul(center + extents * float4(-1.0,  1.0, -1.0, 1.0), cProjection);

    for(int i = 0; i < vertexCount; ++i)
    {   
        outVerts[i].color = color;
    }

    outIndices[0] = uint3(0, 1, 2);
    outIndices[1] = uint3(2, 3, 0);
    outIndices[2] = uint3(1, 5, 6);
    outIndices[3] = uint3(6, 2, 1);
    outIndices[4] = uint3(7, 6, 5);
    outIndices[5] = uint3(5, 4, 7);
    outIndices[6] = uint3(4, 0, 3);
    outIndices[7] = uint3(3, 7, 4);
    outIndices[8] = uint3(4, 5, 1);
    outIndices[9] = uint3(1, 0, 4);
    outIndices[10] = uint3(3, 2, 6);
    outIndices[11] = uint3(6, 7, 3);
}

[RootSignature(RootSigVS)]
GSInput VSMain(uint vertexId : SV_VertexID)
{
	GSInput result;
    uint clusterIndex = tCompactedClusters[vertexId];
    AABB aabb = tAABBs[clusterIndex];

	result.center = aabb.Center;
	result.extents = aabb.Extents;

    result.lightCount = tLightGrid[clusterIndex].y;
	result.color = tHeatmapTexture.SampleLevel(sHeatmapSampler, float2((float)result.lightCount / 30.0f, 0), 0);
	return result;
}

[maxvertexcount(16)]
void GSMain(point GSInput input[1], inout TriangleStream<PSInput> outputStream)
{
    if(input[0].lightCount == 0)
    {
        return;
    }
	float4 center = input[0].center;
    float4 extents = input[0].extents;

    float4 positions[8] = {
        center + float4(-extents.x, -extents.y, -extents.z, 1.0f),
        center + float4(-extents.x, -extents.y, extents.z, 1.0f),
        center + float4(-extents.x, extents.y, -extents.z, 1.0f),
        center + float4(-extents.x, extents.y, extents.z, 1.0f),
        center + float4(extents.x, -extents.y, -extents.z, 1.0f),
        center + float4(extents.x, -extents.y, extents.z, 1.0f),
        center + float4(extents.x, extents.y, -extents.z, 1.0f),
        center + float4(extents.x, extents.y, extents.z, 1.0f)
    };

    uint indices[18] = {
        0, 1, 2,
        3, 6, 7,
        4, 5, -1,
        2, 6, 0,
        4, 1, 5,
        3, 7, -1
    };

    [unroll]
    for (uint i = 0; i < 18; ++i)
    {
        if (indices[i] == (uint)-1)
        {
            outputStream.RestartStrip();
        }
        else
        {
            PSInput output;
            output.position = mul(positions[indices[i]], cProjection);
            output.color = input[0].color;
            outputStream.Append(output);
        }
    }
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.color.xyz, 0.2f);
}