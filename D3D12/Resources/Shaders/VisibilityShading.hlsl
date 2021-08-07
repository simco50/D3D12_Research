#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "ShadingModels.hlsli"

#define RootSig \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t5, numDescriptors = 13)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				GLOBAL_BINDLESS_TABLE ", " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR), "

Texture2D<uint> tVisibilityTexture : register(t13);
Texture2D<float2> tBarycentricsTexture : register(t14);
RWTexture2D<float4> uTarget : register(u0);

struct PerViewData
{
	float4x4 ViewProjection;
	float4x4 ViewInverse;
	uint2 ScreenDimensions;
};

ConstantBuffer<PerViewData> cViewData : register(b1);

struct VertexInput
{
	uint2 Position;
	uint UV;
	float3 Normal;
	float4 Tangent;
};

struct VertexAttribute
{
	float3 Position;
	float2 UV;
	float3 Normal;
	float4 Tangent;
};

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

[numthreads(16, 16, 1)]
[RootSignature(RootSig)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if(dispatchThreadId.x >= cViewData.ScreenDimensions.x || dispatchThreadId.y >= cViewData.ScreenDimensions.y)
	{
		return;
	}

	uint visibilityMask = tVisibilityTexture.Load(uint3(dispatchThreadId.xy, 0));
	float2 bary = tBarycentricsTexture.Load(uint3(dispatchThreadId.xy, 0));
	float3 barycentrics = float3(bary.xy, 1 - bary.x - bary.y);
	uint meshIndex = visibilityMask >> 16;
	uint triangleIndex = visibilityMask & 0xFFFF;

    MeshInstance instance = tMeshInstances[meshIndex];
	MeshData mesh = tMeshes[instance.Mesh];
	uint3 indices = tBufferTable[mesh.IndexStream].Load<uint3>(triangleIndex * sizeof(uint3));

	VertexAttribute vertex;
	vertex.Position = 0;
	vertex.UV = 0;
	vertex.Normal = 0;
	for(uint i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
        vertex.Position += UnpackHalf3(GetVertexData<uint2>(mesh.PositionStream, vertexId)) * barycentrics[i];
        vertex.UV += UnpackHalf2(GetVertexData<uint>(mesh.UVStream, vertexId)) * barycentrics[i];
        NormalData normalData = GetVertexData<NormalData>(mesh.NormalStream, vertexId);
        vertex.Normal += normalData.Normal * barycentrics[i];
        vertex.Tangent += normalData.Tangent * barycentrics[i];
	}

	MaterialProperties properties = GetMaterialProperties(instance.Material, vertex.UV, 0);
	BrdfData brdfData = GetBrdfData(properties);

	float3 V = normalize(vertex.Position - cViewData.ViewInverse[3].xyz);
	
	Light light = tLights[0];
	float3 L = -light.Direction;

	float4 color = light.GetColor();

	LightResult result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, vertex.Normal, V, L, 1);
	float3 o = (result.Diffuse + result.Specular) * color.rgb * light.Intensity;

	uTarget[dispatchThreadId.xy] = float4(o, 1);
}