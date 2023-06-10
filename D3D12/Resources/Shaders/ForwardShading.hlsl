#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Random.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "HZB.hlsli"
#include "Noise.hlsli"

struct PerViewData
{
	uint4 ClusterDimensions;
	uint2 ClusterSize;
	float2 LightGridParams;
};

struct InstanceIndex
{
	uint ID;
};

ConstantBuffer<InstanceIndex> cObject : register(b0);
ConstantBuffer<PerViewData> cPass : register(b1);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 PositionWS : POSITION_WS;
	float2 UV : TEXCOORD;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	uint Color : TEXCOORD1;
};

Texture2D<float> tAO :	register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D tPreviousSceneColor :	register(t2);
Texture3D<float4> tLightScattering : register(t3);

#if CLUSTERED_FORWARD
Buffer<uint> tLightGrid : register(t4);
uint GetSliceFromDepth(float depth)
{
	return floor(log(depth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
}
#elif TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t4);
#endif
Buffer<uint> tLightIndexList : register(t5);

void GetLightCount(float2 pixel, float linearDepth, out uint lightCount, out uint startOffset)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	startOffset = tLightGrid[tileIndex].x;
	lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint3 clusterIndex3D = uint3(floor(pixel / cPass.ClusterSize), GetSliceFromDepth(linearDepth));
	uint tileIndex = Flatten3D(clusterIndex3D, cPass.ClusterDimensions.xyz);
	startOffset = tileIndex * MAX_LIGHTS_PER_CLUSTER;
	lightCount = tLightGrid[tileIndex];
#else
	startOffset = 0;
	lightCount = cView.LightCount;
#endif
}

Light GetLight(uint lightIndex, uint lightOffset)
{
#if TILED_FORWARD || CLUSTERED_FORWARD
	lightIndex = tLightIndexList[lightOffset + lightIndex];
#endif
	return GetLight(lightIndex);
}

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	LightResult totalResult = (LightResult)0;

	uint lightCount, lightOffset;
	GetLightCount(pixel, linearDepth, lightCount, lightOffset);

	for(uint i = 0; i < lightCount; ++i)
	{
		Light light = GetLight(i, lightOffset);
		LightResult result = DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);

#define SCREEN_SPACE_SHADOWS 0
#if SCREEN_SPACE_SHADOWS
		float3 L = normalize(worldPos - light.Position);
		if(light.IsDirectional)
		{
			L = light.Direction;
		}

		float length = 0.1f * pos.w * cView.ProjectionInverse[1][1];
		float occlusion = ScreenSpaceShadows(worldPos, L, tDepth, 8, length, dither);

		result.Diffuse *= occlusion;
		result.Specular *= occlusion;
#endif

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

InterpolantsVSToPS FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	InterpolantsVSToPS result;
	float3 Position = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.PositionsOffset)).xyz;
	result.PositionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(result.PositionWS, 1.0f), cView.ViewProjection);

	result.UV = Unpack_RG16_FLOAT(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));

	uint2 normalData = BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
	result.Normal = normalize(mul(Unpack_RGB10A2_SNORM(normalData.x).xyz, (float3x3)world));
	float4 tangent = Unpack_RGB10A2_SNORM(normalData.y);
	result.Tangent = float4(normalize(mul(tangent.xyz, (float3x3)world)), tangent.w);

	result.Color = 0xFFFFFFFF;
	if(mesh.ColorsOffset != ~0u)
		result.Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);

	return result;
}

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InstanceData instance = GetInstance(cObject.ID);
	MeshData mesh = GetMesh(instance.MeshIndex);
	InterpolantsVSToPS result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
	return result;
}

MaterialProperties EvaluateMaterial(MaterialData material, InterpolantsVSToPS attributes)
{
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor * Unpack_RGBA8_UNORM(attributes.Color);
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= Sample2D(material.Diffuse, sMaterialSampler, attributes.UV);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = Sample2D(material.RoughnessMetalness, sMaterialSampler, attributes.UV);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= Sample2D(material.Emissive, sMaterialSampler, attributes.UV).rgb;
	}
	properties.Specular = 0.5f;

	properties.Normal = normalize(attributes.Normal);
	if(material.Normal != INVALID_HANDLE)
	{
		float3 normalTS = Sample2D(material.Normal, sMaterialSampler, attributes.UV).rgb;
		float3x3 TBN = CreateTangentToWorld(properties.Normal, float4(normalize(attributes.Tangent.xyz), attributes.Tangent.w));
		properties.Normal = TangentSpaceNormalMapping(normalTS, TBN);
	}
	return properties;
}

struct PSOut
{
 	float4 Color : SV_Target0;
	float2 Normal : SV_Target1;
	float Roughness : SV_Target2;
};

void PSMain(InterpolantsVSToPS input,
			float3 bary : SV_Barycentrics,
			out PSOut output)
{
	float2 screenUV = (float2)input.Position.xy * cView.TargetDimensionsInv;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, screenUV, 0);
	float linearDepth = input.Position.w;
	float dither = InterleavedGradientNoise(input.Position.xy);

	InstanceData instance = GetInstance(cObject.ID);

	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, input);
	BrdfData brdfData = GetBrdfData(surface);

	float3 V = normalize(cView.ViewLocation - input.PositionWS);
	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(input.PositionWS, surface.Normal, V, brdfData.Roughness, tDepth, tPreviousSceneColor, dither, ssrWeight);

	LightResult lighting = DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, input.PositionWS, input.Position.xy, linearDepth, dither);

	float3 outRadiance = 0;
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(input.PositionWS, surface.Normal, -V);
	outRadiance += lighting.Diffuse + lighting.Specular;
	outRadiance += ssr * ambientOcclusion;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tLightScattering.SampleLevel(sLinearClamp, float3(screenUV, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	float reflectivity = saturate(scatteringTransmittance.w * ambientOcclusion * Square(1 - brdfData.Roughness));

	output.Color = float4(outRadiance, surface.Opacity);
	output.Normal = EncodeNormalOctahedron(surface.Normal);
	output.Roughness = saturate(reflectivity - ssrWeight);
}
