#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "VisibilityBuffer.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "Noise.hlsli"

Texture2D<uint> tVisibilityTexture : register(t0);
Texture2D<float> tAO :	register(t1);
Texture2D<float> tDepth : register(t2);
Texture2D tPreviousSceneColor :	register(t3);
Texture3D<float4> tFog : register(t4);
StructuredBuffer<MeshletCandidate> tVisibleMeshlets : register(t5);
StructuredBuffer<uint> tLightGrid : register(t6);

MaterialProperties EvaluateMaterial(MaterialData material, VisBufferVertexAttribute attributes)
{
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor * Unpack_RGBA8_UNORM(attributes.Color);
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= SampleGrad2D(NonUniformResourceIndex(material.Diffuse), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = SampleGrad2D(NonUniformResourceIndex(material.RoughnessMetalness), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= SampleGrad2D(NonUniformResourceIndex(material.Emissive), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY).rgb;
	}
	properties.Specular = 0.5f;

	properties.Normal = attributes.Normal;
	if(material.Normal != INVALID_HANDLE)
	{
		float3 normalTS = SampleGrad2D(NonUniformResourceIndex(material.Normal), sMaterialSampler, attributes.UV, attributes.DX, attributes.DY).rgb;
		float3x3 TBN = CreateTangentToWorld(properties.Normal, float4(normalize(attributes.Tangent.xyz), attributes.Tangent.w));
		properties.Normal = TangentSpaceNormalMapping(normalTS, TBN);
	}
	return properties;
}

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.TargetDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
	uint lightGridOffset = tileIndex1D * TILED_LIGHTING_NUM_BUCKETS;

	LightResult totalResult = (LightResult)0;
	for(uint bucketIndex = 0; bucketIndex < TILED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
	{
		uint bucket = tLightGrid[lightGridOffset + bucketIndex];
		while(bucket)
		{
			uint bitIndex = firstbitlow(bucket);
			bucket ^= 1u << bitIndex;

			uint lightIndex = bitIndex + bucketIndex * 32;
			Light light = GetLight(lightIndex);
			totalResult = totalResult + DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);
		}
	}
	return totalResult;
}

struct PSOut
{
 	float4 Color : SV_Target0;
	float2 Normal : SV_Target1;
	float Roughness : SV_Target2;
};

bool VisibilityShadingCommon(uint2 texel, out PSOut output)
{
	uint candidateIndex, primitiveID;
	if(!UnpackVisBuffer(tVisibilityTexture[texel], candidateIndex, primitiveID))
		return false;

	float2 uv = (0.5f + texel) * cView.ViewportDimensionsInv;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, uv, 0);
	float dither = InterleavedGradientNoise(texel);

	MeshletCandidate candidate = tVisibleMeshlets[candidateIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);

	// Vertex Shader
	VisBufferVertexAttribute vertex = GetVertexAttributes(uv, instance, candidate.MeshletIndex, primitiveID);
	float linearDepth = vertex.LinearDepth;

	// Surface Shader
	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, vertex);
	BrdfData brdfData = GetBrdfData(surface);

	float3 V = normalize(cView.ViewLocation - vertex.Position);
	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(vertex.Position, surface.Normal, V, brdfData.Roughness, tDepth, tPreviousSceneColor, dither, ssrWeight);

	LightResult result = DoLight(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, surface.Normal, V, vertex.Position, texel, linearDepth, dither);

	float3 outRadiance = 0;
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(vertex.Position, surface.Normal, -V);
	outRadiance += result.Diffuse + result.Specular;
	outRadiance += ssr;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((linearDepth - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tFog.SampleLevel(sLinearClamp, float3(uv, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	float reflectivity = saturate(Square(1 - brdfData.Roughness));

	output.Color = float4(outRadiance, surface.Opacity);
	output.Normal = EncodeNormalOctahedron(surface.Normal);
	output.Roughness = reflectivity;
	return true;
}

void ShadePS(
	float4 position : SV_Position,
	float2 uv : TEXCOORD,
	out PSOut output)
{
	VisibilityShadingCommon((uint2)position.xy, output);
}

RWTexture2D<float4> uColorTarget : register(u0);
RWTexture2D<float2> uNormalsTarget : register(u1);
RWTexture2D<float> uRoughnessTarget : register(u2);

[numthreads(8, 8, 1)]
void ShadeCS(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;

	PSOut output;
	if(VisibilityShadingCommon(texel, output))
	{
		uColorTarget[texel] = output.Color;
		uNormalsTarget[texel] = output.Normal;
		uRoughnessTarget[texel] = output.Roughness;
	}
}
