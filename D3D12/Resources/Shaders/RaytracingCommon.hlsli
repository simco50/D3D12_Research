#ifndef __INCLUDE_RAYTRACING_COMMON__
#define __INCLUDE_RAYTRACING_COMMON__

#include "SkyCommon.hlsli"
#include "ShadingModels.hlsli"

#define RAY_BIAS 1.0e-2f
#define RAY_MAX_T 1.0e10f

struct VertexAttribute
{
    float2 UV;
    float3 Normal;
    float4 Tangent;
    float3 GeometryNormal;
    uint Material;
};

struct VertexInput
{
    uint2 Position;
    uint UV;
    float3 Normal;
    float4 Tangent;
};

VertexAttribute GetVertexAttributes(float2 attribBarycentrics, uint instanceID, uint primitiveIndex, float4x3 worldMatrix)
{
    float3 barycentrics = float3((1.0f - attribBarycentrics.x - attribBarycentrics.y), attribBarycentrics.x, attribBarycentrics.y);
    MeshData mesh = tMeshes[instanceID];
    uint3 indices = tBufferTable[mesh.IndexBuffer].Load<uint3>(primitiveIndex * sizeof(uint3));
    VertexAttribute outData;

    outData.UV = 0;
    outData.Normal = 0;
    outData.Material = mesh.Material;

    float3 positions[3];
    const uint vertexStride = sizeof(VertexInput);
    ByteAddressBuffer geometryBuffer = tBufferTable[mesh.VertexBuffer];

    for(int i = 0; i < 3; ++i)
    {
        uint dataOffset = 0;
        positions[i] += UnpackHalf3(geometryBuffer.Load<uint2>(indices[i] * vertexStride + dataOffset));
        dataOffset += sizeof(uint2);
        outData.UV += UnpackHalf2(geometryBuffer.Load<uint>(indices[i] * vertexStride + dataOffset)) * barycentrics[i];
        dataOffset += sizeof(uint);
        outData.Normal += geometryBuffer.Load<float3>(indices[i] * vertexStride + dataOffset) * barycentrics[i];
        dataOffset += sizeof(float3);
        outData.Tangent += geometryBuffer.Load<float4>(indices[i] * vertexStride + dataOffset) * barycentrics[i];
        dataOffset += sizeof(float4);
    }
    outData.Normal = normalize(mul(outData.Normal, (float3x3)worldMatrix));
    outData.Tangent.xyz = normalize(mul(outData.Tangent.xyz, (float3x3)worldMatrix));

    // Calculate geometry normal from triangle vertices positions
    float3 edge20 = positions[2] - positions[0];
    float3 edge21 = positions[2] - positions[1];
    float3 edge10 = positions[1] - positions[0];
    outData.GeometryNormal = mul(normalize(cross(edge20, edge10)), (float3x3)worldMatrix);

    return outData;
}

struct MaterialProperties
{
    float3 BaseColor;
    float3 NormalTS;
    float Metalness;
    float3 Emissive;
    float Roughness;
    float Opacity;
    float Specular;
};

MaterialProperties GetMaterialProperties(uint materialIndex, float2 UV, int mipLevel)
{
    MaterialData material = tMaterials[materialIndex];
    MaterialProperties properties;
    float4 baseColor = material.BaseColorFactor;
    if(material.Diffuse >= 0)
    {
        baseColor *= tTexture2DTable[material.Diffuse].SampleLevel(sDiffuseSampler, UV, mipLevel);
    }
    properties.BaseColor = baseColor.rgb;
    properties.Opacity = baseColor.a;

    properties.Metalness = material.MetalnessFactor;
    properties.Roughness = material.RoughnessFactor;
    if(material.RoughnessMetalness >= 0)
    {
        float4 roughnessMetalnessSample = tTexture2DTable[material.RoughnessMetalness].SampleLevel(sDiffuseSampler, UV, mipLevel);
        properties.Metalness *= roughnessMetalnessSample.b;
        properties.Roughness *= roughnessMetalnessSample.g;
    }
    properties.Emissive = material.EmissiveFactor.rgb;
    if(material.Emissive >= 0)
    {
        properties.Emissive *= tTexture2DTable[material.Emissive].SampleLevel(sDiffuseSampler, UV, mipLevel).rgb;
    }
    properties.Specular = 0.5f;

    properties.NormalTS = float3(0, 0, 1);
    if(material.Normal >= 0)
    {
        properties.NormalTS = tTexture2DTable[material.Normal].SampleLevel(sDiffuseSampler, UV, mipLevel).rgb;
    }
    return properties;
}

struct BrdfData
{
    float3 Diffuse;
    float3 Specular;
    float Roughness;
};

BrdfData GetBrdfData(MaterialProperties material)
{
    BrdfData data;
    data.Diffuse = ComputeDiffuseColor(material.BaseColor, material.Metalness);
    data.Specular = ComputeF0(material.Specular, material.BaseColor, material.Metalness);
    data.Roughness = material.Roughness;
    return data;
}

struct RayCone
{
    float Width;
    float SpreadAngle;
};

RayCone PropagateRayCone(RayCone cone, float surfaceSpreadAngle, float hitT)
{
    RayCone newCone;
    newCone.Width = cone.SpreadAngle * hitT + cone.Width;
    newCone.SpreadAngle = cone.SpreadAngle + surfaceSpreadAngle;
    return newCone;
}

// Texture Level of Detail Strategies for Real-Time Ray Tracing
// Ray Tracing Gems - Tomas Akenine-Möller
float ComputeRayConeMip(RayCone cone, float3 vertexNormal, float2 vertexUVs[3], float2 textureDimensions)
{   
    // Triangle surface area
    float3 normal = vertexNormal;
    float invWorldArea = rsqrt(dot(normal, normal));
    float3 triangleNormal = abs(normal * invWorldArea);

    // UV area
    float2 duv0 = vertexUVs[2] - vertexUVs[0];
    float2 duv1 = vertexUVs[1] - vertexUVs[0];
    float uvArea = 0.5f * length(cross(float3(duv0, 0), float3(duv1, 0)));

	float triangleLODConstant = 0.5f * log2(uvArea * invWorldArea);

	float lambda = triangleLODConstant;
	lambda += log2(abs(cone.Width));
	lambda += 0.5f * log2(textureDimensions.x * textureDimensions.y);
	lambda -= log2(abs(dot(WorldRayDirection(), triangleNormal)));
	return lambda;
}

Ray GeneratePinholeCameraRay(float2 pixel, float4x4 viewInverse, float4x4 projection)
{
    // Set up the ray.
    Ray ray;
    ray.Origin = viewInverse[3].xyz;
    // Extract the aspect ratio and fov from the projection matrix.
    float aspect = projection[1][1] / projection[0][0];
    float tanHalfFovY = 1.f / projection[1][1];

    // Compute the ray direction.
    ray.Direction = normalize(
        (pixel.x * viewInverse[0].xyz * tanHalfFovY * aspect) -
        (pixel.y * viewInverse[1].xyz * tanHalfFovY) +
        viewInverse[2].xyz);

    return ray;
}

// Ray Tracing Gems: A Fast and Robust Method for Avoiding Self-Intersection
// Wächter and Binder
// Offset ray so that it never self-intersects
float3 OffsetRay(float3 position, float3 geometryNormal)
{
    static const float origin = 1.0f / 32.0f;
    static const float float_scale = 1.0f / 65536.0f;
    static const float int_scale = 256.0f;

    int3 of_i = int3(int_scale * geometryNormal.x, int_scale * geometryNormal.y, int_scale * geometryNormal.z);

    float3 p_i = float3(
        asfloat(asint(position.x) + ((position.x < 0) ? -of_i.x : of_i.x)),
        asfloat(asint(position.y) + ((position.y < 0) ? -of_i.y : of_i.y)),
        asfloat(asint(position.z) + ((position.z < 0) ? -of_i.z : of_i.z)));

    return float3(abs(position.x) < origin ? position.x + float_scale * geometryNormal.x : p_i.x,
        abs(position.y) < origin ? position.y + float_scale * geometryNormal.y : p_i.y,
        abs(position.z) < origin ? position.z + float_scale * geometryNormal.z : p_i.z);
}

#endif