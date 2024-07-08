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
	float4 	Position 	: SV_Position;
	float2 	UV 			: TEXCOORD0;
#ifndef DEPTH_ONLY
	float3 	PositionWS 	: POSITION_WS;
	float3 	Normal 		: NORMAL;
	float4 	Tangent 	: TANGENT;
	uint 	Color 		: COLOR;
#endif
};

Texture2D<float> tAO :	register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D tPreviousSceneColor :	register(t2);
Texture3D<float4> tLightScattering : register(t3);
Buffer<uint> tLightGrid : register(t4);

#if CLUSTERED_FORWARD
uint GetSliceFromDepth(float depth)
{
	return floor(log(depth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
}
#endif

#if TILED_FORWARD

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint2 tileIndex = uint2(floor(pixel / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.ViewportDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
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

#elif CLUSTERED_FORWARD

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	uint3 clusterIndex3D = uint3(floor(pixel / cPass.ClusterSize), GetSliceFromDepth(linearDepth));
	uint tileIndex = Flatten3D(clusterIndex3D, cPass.ClusterDimensions.xy);
	uint lightGridOffset = tileIndex * CLUSTERED_LIGHTING_NUM_BUCKETS;

	LightResult totalResult = (LightResult)0;
	for(uint bucketIndex = 0; bucketIndex < CLUSTERED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
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

#else

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	LightResult totalResult = (LightResult)0;
	for(uint lightIndex = 0; lightIndex < cView.LightCount; ++lightIndex)
	{
		Light light = GetLight(lightIndex);
		totalResult = totalResult + DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);
	}
	return totalResult;
}

#endif


InterpolantsVSToPS LoadVertex(MeshData mesh, float4x4 world, uint vertexId)
{
	Vertex vertex = LoadVertex(mesh, vertexId);
	InterpolantsVSToPS result;
	float3 worldPos = mul(float4(vertex.Position, 1.0f), world).xyz;
	result.Position = mul(float4(worldPos, 1.0f), cView.ViewProjection);
	result.UV = vertex.UV;
#ifndef DEPTH_ONLY
	result.PositionWS = worldPos;
	result.Normal = normalize(mul(vertex.Normal, (float3x3)world));
	result.Tangent = float4(normalize(mul(vertex.Tangent.xyz, (float3x3)world)), vertex.Tangent.w);
	result.Color = vertex.Color;
#endif

	return result;
}

struct PayloadData
{
	uint Indices[32];
};

groupshared PayloadData gsPayload;

[numthreads(32, 1, 1)]
void ASMain(uint threadId : SV_DispatchThreadID)
{
	bool visible = false;

	InstanceData instance = GetInstance(cObject.ID);
	MeshData mesh = GetMesh(instance.MeshIndex);;
	if (threadId < mesh.MeshletCount)
	{
		Meshlet::Bounds bounds = BufferLoad<Meshlet::Bounds>(mesh.BufferIndex, threadId, mesh.MeshletBoundsOffset);
		FrustumCullData cullData = FrustumCull(bounds.LocalCenter, bounds.LocalExtents, instance.LocalToWorld, cView.ViewProjection);
		visible = cullData.IsVisible;
	}

	if (visible)
	{
		uint index = WavePrefixCountBits(visible);
		gsPayload.Indices[index] = threadId;
	}

	// Dispatch the required number of MS threadgroups to render the visible meshlets
	uint visibleCount = WaveActiveCountBits(visible);
	DispatchMesh(visibleCount, 1, 1, gsPayload);
}

#define NUM_MESHLET_THREADS 32

[outputtopology("triangle")]
[numthreads(NUM_MESHLET_THREADS, 1, 1)]
void MSMain(
	in uint groupThreadID : SV_GroupIndex,
	in payload PayloadData payload,
	in uint groupID : SV_GroupID,
	out vertices InterpolantsVSToPS verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES])
{
	InstanceData instance = GetInstance(cObject.ID);
	MeshData mesh = GetMesh(instance.MeshIndex);

	uint meshletIndex = payload.Indices[groupID];
	if(meshletIndex >= mesh.MeshletCount)
		return;

	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = BufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		InterpolantsVSToPS result = LoadVertex(mesh, instance.LocalToWorld, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		Meshlet::Triangle tri = BufferLoad<Meshlet::Triangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);
	}
}

#ifdef DEPTH_ONLY

void DepthOnlyPS(InterpolantsVSToPS input)
{
	InstanceData instance = GetInstance(cObject.ID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	if(Sample2D(material.Diffuse, sMaterialSampler, input.UV).a < material.AlphaCutoff)
	{
		discard;
	}
}

#else

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

void ShadePS(InterpolantsVSToPS input, out PSOut output)
{
	float2 screenUV = (float2)input.Position.xy * cView.ViewportDimensionsInv;
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

#endif
