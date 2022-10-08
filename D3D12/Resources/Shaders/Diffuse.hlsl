#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Random.hlsli"
#include "RayTracing/DDGICommon.hlsli"

#define BLOCK_SIZE 16

struct PerViewData
{
	uint4 ClusterDimensions;
	uint2 ClusterSize;
	float2 LightGridParams;
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
	uint ID : ID;
};

Texture2D tAO :	register(t0);
Texture2D tDepth : register(t1);
Texture2D tPreviousSceneColor :	register(t2);
Texture3D<float4> tLightScattering : register(t3);

#if CLUSTERED_FORWARD
StructuredBuffer<uint> tLightGrid : register(t4);
uint GetSliceFromDepth(float depth)
{
	return floor(log(depth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
}
#elif TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t4);
#endif
StructuredBuffer<uint> tLightIndexList : register(t5);

void GetLightCount(float4 screenPos, out uint lightCount, out uint startOffset)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(screenPos.xy / BLOCK_SIZE));
	startOffset = tLightGrid[tileIndex].x;
	lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint3 clusterIndex3D = uint3(floor(screenPos.xy / cPass.ClusterSize), GetSliceFromDepth(screenPos.w));
	uint tileIndex = Flatten3D(clusterIndex3D, cPass.ClusterDimensions.xyz);
	startOffset = tLightGrid[tileIndex * 2];
	lightCount = tLightGrid[tileIndex * 2 + 1];
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

LightResult DoLight(float4 pos, float3 worldPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
	LightResult totalResult = (LightResult)0;

	uint lightCount, lightOffset;
	GetLightCount(pos, lightCount, lightOffset);

	for(uint i = 0; i < lightCount; ++i)
	{
		Light light = GetLight(i, lightOffset);
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, worldPos, N, V);

#define SCREEN_SPACE_SHADOWS 0
#if SCREEN_SPACE_SHADOWS
		float3 L = normalize(worldPos - light.Position);
		if(light.IsDirectional)
		{
			L = light.Direction;
		}

		float ditherValue = InterleavedGradientNoise(pos.xy, cView.FrameIndex);
		float length = 0.1f * pos.w * cView.ProjectionInverse[1][1];
		float occlusion = ScreenSpaceShadows(worldPos, L, tDepth, 8, length, ditherValue);

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
	float3 Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	result.PositionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(result.PositionWS, 1.0f), cView.ViewProjection);

	result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));

	NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
	result.Normal = normalize(mul(normalData.Normal, (float3x3)world));
	result.Tangent = float4(normalize(mul(normalData.Tangent.xyz, (float3x3)world)), normalData.Tangent.w);

	result.Color = 0xFFFFFFFF;
	if(mesh.ColorsOffset != ~0u)
		result.Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);
	result.ID = vertexId;

	return result;
}

struct PayloadData
{
	uint Indices[32];
};

groupshared PayloadData gsPayload;

bool IsVisible(MeshData mesh, float4x4 world, uint meshlet)
{
	MeshletBounds cullData = BufferLoad<MeshletBounds>(mesh.BufferIndex, meshlet, mesh.MeshletBoundsOffset);

	float4 center = mul(float4(cullData.Center, 1), world);
	float3 radius3 = abs(mul(cullData.Radius.xxx, (float3x3)world));
	float radius = Max3(radius3);
	float3 coneAxis = normalize(mul(cullData.ConeAxis, (float3x3)world));

	for(int i = 0; i < 6; ++i)
	{
		if(dot(center, cView.FrustumPlanes[i]) > radius)
		{
			return false;
		}
	}

	float3 viewLocation = cView.ViewLocation;
	if(dot(viewLocation - center.xyz, coneAxis) >= cullData.ConeCutoff * length(center.xyz - viewLocation) + radius)
	{
		return false;
	}
	return true;
}

[numthreads(32, 1, 1)]
void ASMain(uint threadID : SV_DispatchThreadID)
{
	bool visible = false;

	InstanceData instance = GetInstance(cObject.ID);
	MeshData mesh = GetMesh(instance.MeshIndex);;
	if (threadID < mesh.MeshletCount)
	{
		visible = IsVisible(mesh, instance.LocalToWorld, threadID);
	}

	if (visible)
	{
		uint index = WavePrefixCountBits(visible);
		gsPayload.Indices[index] = threadID;
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
		InterpolantsVSToPS result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
		result.ID = meshletIndex;
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);
	}
}

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InstanceData instance = GetInstance(cObject.ID);
	MeshData mesh = GetMesh(instance.MeshIndex);
	InterpolantsVSToPS result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
	return result;
}

MaterialProperties GetMaterialProperties(MaterialData material, float2 UV)
{
	MaterialProperties properties;
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse != INVALID_HANDLE)
	{
		baseColor *= Sample2D(material.Diffuse, sMaterialSampler, UV);
	}
	properties.BaseColor = baseColor.rgb;
	properties.Opacity = baseColor.a;

	properties.Metalness = material.MetalnessFactor;
	properties.Roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness != INVALID_HANDLE)
	{
		float4 roughnessMetalnessSample = Sample2D(material.RoughnessMetalness, sMaterialSampler, UV);
		properties.Metalness *= roughnessMetalnessSample.b;
		properties.Roughness *= roughnessMetalnessSample.g;
	}
	properties.Emissive = material.EmissiveFactor.rgb;
	if(material.Emissive != INVALID_HANDLE)
	{
		properties.Emissive *= Sample2D(material.Emissive, sMaterialSampler, UV).rgb;
	}
	properties.Specular = 0.5f;

	properties.NormalTS = float3(0.5f, 0.5f, 1.0f);
	if(material.Normal != INVALID_HANDLE)
	{
		properties.NormalTS = Sample2D(material.Normal, sMaterialSampler, UV).rgb;
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
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, screenUV, 0).r;

	float3 V = normalize(cView.ViewLocation - input.PositionWS);

	InstanceData instance = GetInstance(cObject.ID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	material.BaseColorFactor *= UIntToColor(input.Color);
	MaterialProperties surface = GetMaterialProperties(material, input.UV);
	float3 N = normalize(input.Normal);
	float3x3 TBN = CreateTangentToWorld(N, float4(normalize(input.Tangent.xyz), input.Tangent.w));
	N = TangentSpaceNormalMapping(surface.NormalTS, TBN);

	BrdfData brdf = GetBrdfData(surface);
	float3 positionVS = mul(float4(input.PositionWS, 1), cView.View).xyz;

	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(input.Position, positionVS, N, V, brdf.Roughness, tDepth, tPreviousSceneColor, ssrWeight);

	LightResult lighting = DoLight(input.Position, input.PositionWS, N, V, brdf.Diffuse, brdf.Specular, brdf.Roughness);

	float3 outRadiance = 0;
	outRadiance += ambientOcclusion * Diffuse_Lambert(brdf.Diffuse) * SampleDDGIIrradiance(input.PositionWS, N, -V);
	outRadiance += lighting.Diffuse + lighting.Specular;
	outRadiance += ssr * ambientOcclusion;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((positionVS.z - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tLightScattering.SampleLevel(sLinearClamp, float3(screenUV, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	float reflectivity = saturate(scatteringTransmittance.w * ambientOcclusion * Square(1 - brdf.Roughness));

#define DEBUG_MESHLETS 0
#if DEBUG_MESHLETS
	N = input.Normal;
	uint seed = SeedThread(input.ID);
	outRadiance = RandomColor(seed) * saturate(Wireframe(bary) + 0.6);
#endif

	output.Color = float4(outRadiance, surface.Opacity);
	output.Normal = EncodeNormalOctahedron(N);
	output.Roughness = saturate(reflectivity - ssrWeight);
}
