#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "VisibilityBuffer.hlsli"
#include "Lighting.hlsli"

#define RootSig ROOT_SIG("CBV(b1), " \
				"DescriptorTable(SRV(t5, numDescriptors = 13)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1))")

Texture2D<uint> tVisibilityTexture : register(t13);
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

MaterialProperties GetMaterialProperties(uint materialIndex, float2 UV, float2 dx, float2 dy)
{
	MaterialData material = tMaterials[materialIndex];
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse >= 0)
	{
		baseColor *= tTexture2DTable[material.Diffuse].SampleGrad(sMaterialSampler, UV, dx, dy);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness >= 0)
	{
		float4 roughnessMetalnessSample = tTexture2DTable[material.RoughnessMetalness].SampleGrad(sMaterialSampler, UV, dx, dy);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive >= 0)
	{
		properties.Emissive *= tTexture2DTable[material.Emissive].SampleGrad(sMaterialSampler, UV, dx, dy).rgb;
	}
	properties.Specular = 0.5f;

	properties.NormalTS = float3(0, 0, 1);
	if(material.Normal >= 0)
	{
		properties.NormalTS = tTexture2DTable[material.Normal].SampleGrad(sMaterialSampler, UV, dx, dy).rgb;
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

	uint i = 0;

	uint visibilityMask = tVisibilityTexture.Load(uint3(dispatchThreadId.xy, 0));
	if(visibilityMask == 0)
	{
		uTarget[dispatchThreadId.xy] = 0;
		return;
	}
	uint meshIndex = visibilityMask >> 16;
	uint triangleIndex = visibilityMask & 0xFFFF;

    MeshInstance instance = tMeshInstances[meshIndex];
	MeshData mesh = tMeshes[instance.Mesh];
	uint3 indices = tBufferTable[mesh.IndexStream].Load<uint3>(triangleIndex * sizeof(uint3));

	VertexAttribute vertices[3];
	for(i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
        vertices[i].Position = UnpackHalf3(LoadByteAddressData<uint2>(mesh.PositionStream, vertexId));
        vertices[i].UV = UnpackHalf2(LoadByteAddressData<uint>(mesh.UVStream, vertexId));
        NormalData normalData = LoadByteAddressData<NormalData>(mesh.NormalStream, vertexId);
        vertices[i].Normal = normalData.Normal;
        vertices[i].Tangent = normalData.Tangent;
	}

	float4 clipPos0 = mul(mul(float4(vertices[0].Position, 1), instance.World), cViewData.ViewProjection);
	float4 clipPos1 = mul(mul(float4(vertices[1].Position, 1), instance.World), cViewData.ViewProjection);
	float4 clipPos2 = mul(mul(float4(vertices[2].Position, 1), instance.World), cViewData.ViewProjection);

	float3 oneOverW = 1.0f / float3(clipPos0.w, clipPos1.w, clipPos2.w);
	clipPos0 *= oneOverW[0];
	clipPos1 *= oneOverW[1];
	clipPos2 *= oneOverW[2];
	float2 screenPositions[3] = {clipPos0.xy, clipPos1.xy, clipPos2.xy};

	DerivativesOutput derivatives = ComputePartialDerivatives(screenPositions);

	float2 ndc = (float2)dispatchThreadId.xy * rcp(cViewData.ScreenDimensions) * 2 - 1;
	ndc.y *= -1;
	float2 delta = ndc + -screenPositions[0];

	float w = 1.0f / InterpolateAttribute(oneOverW, derivatives.dbDx, derivatives.dbDy, delta);

	float3x2 triTexCoords = float3x2(
		vertices[0].UV * oneOverW[0],
		vertices[1].UV * oneOverW[1],
		vertices[2].UV * oneOverW[2]
	);
	GradientInterpolationResults interpUV = InterpolateAttributeWithGradient(transpose(triTexCoords), derivatives.dbDx, derivatives.dbDy, delta, 2.0f * rcp(cViewData.ScreenDimensions));
	interpUV.Interp *= w;
	interpUV.Dx *= w;
	interpUV.Dy *= w;
	float2 UV = interpUV.Interp;

	float3x3 triNormals =
	{
		vertices[0].Normal * oneOverW[0],
		vertices[1].Normal * oneOverW[1],
		vertices[2].Normal * oneOverW[2]
	};
	float3 N = normalize(Interpolate3DAttributes(triNormals, derivatives.dbDx, derivatives.dbDy, delta) * w);

	float3x3 triTangents =
	{
		vertices[0].Tangent.xyz * oneOverW[0],
		vertices[1].Tangent.xyz * oneOverW[1],
		vertices[2].Tangent.xyz * oneOverW[2]
	};
	float3 T = normalize(Interpolate3DAttributes(triTangents, derivatives.dbDx, derivatives.dbDy, delta) * w);
	float3 B = cross(N, T) * -vertices[0].Tangent.w;

	float3x3 triPos =
	{
		vertices[0].Position * oneOverW[0],
		vertices[1].Position * oneOverW[1],
		vertices[2].Position * oneOverW[2]
	};
	float3 P = Interpolate3DAttributes(triPos, derivatives.dbDx, derivatives.dbDy, delta) * w;
	P = mul(float4(P, 1), instance.World).xyz;

	// Lighting
	MaterialProperties properties = GetMaterialProperties(instance.Material, UV, interpUV.Dx, interpUV.Dy);

	float3x3 TBN = float3x3(T, B, N);
	N = TangentSpaceNormalMapping(properties.NormalTS, TBN);

	BrdfData brdfData = GetBrdfData(properties);

	float3 V = normalize(P - cViewData.ViewInverse[3].xyz);
	Light light = tLights[0];
	float3 L = -light.Direction;
	float4 color = light.GetColor();
	LightResult result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, L, 1);
	float3 o = (result.Diffuse + result.Specular) * color.rgb * light.Intensity;

	uTarget[dispatchThreadId.xy] = float4(o, 1);
}
