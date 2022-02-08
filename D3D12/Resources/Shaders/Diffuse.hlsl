#include "CommonBindings.hlsli"
#include "Lighting.hlsli"
#include "Random.hlsli"

#define BLOCK_SIZE 16

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=3, b0), " \
	"CBV(b1), " \
	"CBV(b100), " \
	"DescriptorTable(SRV(t0, numDescriptors = 6))")

struct PerViewData
{
	uint4 ClusterDimensions;
	uint2 ClusterSize;
	float2 LightGridParams;
};

ConstantBuffer<InstanceData> cObject : register(b0);
ConstantBuffer<PerViewData> cPass : register(b1);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 PositionWS : POSITION_WS;
	float3 PositionVS : POSITION_VS;
	float2 UV : TEXCOORD;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	uint Color : TEXCOORD1;
	uint Seed : SEED;
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

float ScreenSpaceShadows(float3 worldPos, float3 lightDirection, int stepCount, float rayLength, float ditherOffset)
{
	float4 rayStartPS = mul(float4(worldPos, 1), cView.ViewProjection);
	float4 rayDirPS = mul(float4(-lightDirection * rayLength, 0), cView.ViewProjection);
	float4 rayEndPS = rayStartPS + rayDirPS;
	rayStartPS.xyz /= rayStartPS.w;
	rayEndPS.xyz /= rayEndPS.w;
	float3 rayStep = rayEndPS.xyz - rayStartPS.xyz;
	float stepSize = 1.0f / stepCount;

	float4 rayDepthClip = rayStartPS + mul(float4(0, 0, rayLength, 0), cView.Projection);
	rayDepthClip.xyz /= rayDepthClip.w;
	float tolerance = abs(rayDepthClip.z - rayStartPS.z) * stepSize * 2;

	float occlusion = 0.0f;
	float hitStep = -1.0f;

	float n = stepSize * ditherOffset + stepSize;

	[unroll]
	for(uint i = 0; i < stepCount; ++i)
	{
		float3 rayPos = rayStartPS.xyz + n * rayStep;
		float depth = tDepth.SampleLevel(sLinearClamp, rayPos.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f), 0).r;
		float diff = rayPos.z - depth;

		bool hit = abs(diff + tolerance) < tolerance;
		hitStep = hit && hitStep < 0.0f ? n : hitStep;
		n += stepSize;
	}
	if(hitStep > 0.0f)
	{
		float2 hitUV = rayStartPS.xy + n * rayStep.xy;
		hitUV = hitUV * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
		occlusion = ScreenFade(hitUV);
	}
	return 1.0f - occlusion;
}

LightResult DoLight(float4 pos, float3 worldPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
#elif CLUSTERED_FORWARD
	uint3 clusterIndex3D = uint3(floor(pos.xy / cPass.ClusterSize), GetSliceFromDepth(pos.w));
	uint tileIndex = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
#endif

#if TILED_FORWARD
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint startOffset = tLightGrid[tileIndex * 2];
	uint lightCount = tLightGrid[tileIndex * 2 + 1];
#else
	uint lightCount = cView.LightCount;
#endif

	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
#if TILED_FORWARD || CLUSTERED_FORWARD
		uint lightIndex = tLightIndexList[startOffset + i];
#else
		uint lightIndex = i;
#endif
		Light light = GetLight(lightIndex);
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
		float occlusion = ScreenSpaceShadows(worldPos, L, 8, length, ditherValue);

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
	result.PositionVS = mul(float4(result.PositionWS, 1.0f), cView.View).xyz;
	result.Position = mul(float4(result.PositionWS, 1.0f), cView.ViewProjection);

	result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));

	NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
	result.Normal = normalize(mul(normalData.Normal, (float3x3)world));
	result.Tangent = float4(normalize(mul(normalData.Tangent.xyz, (float3x3)world)), normalData.Tangent.w);

	result.Color = 0xFFFFFFFF;
	if(mesh.ColorsOffset != ~0u)
		result.Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);

	result.Seed = cObject.Mesh;

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

	for(int i = 0; i < 6; ++i)
	{
		if(dot(center, cView.FrustumPlanes[i]) > cullData.Radius)
		{
			return false;
		}
	}

	float3 viewLocation = cView.ViewPosition.xyz;
	float3 coneApex = mul(float4(cullData.ConeApex, 1), world).xyz;
	float3 coneAxis = mul(cullData.ConeAxis, (float3x3)world);
	float3 view = normalize(viewLocation - coneApex);
	if (dot(view, coneAxis) >= cullData.ConeCutoff)
	{
		return false;
	}
	return true;
}

[numthreads(32, 1, 1)]
void ASMain(uint threadID : SV_DispatchThreadID)
{
	bool visible = false;

	MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	if (threadID < mesh.MeshletCount)
	{
		visible = IsVisible(mesh, world, threadID);
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

[RootSignature(RootSig)]
[outputtopology("triangle")]
[numthreads(NUM_MESHLET_THREADS, 1, 1)]
void MSMain(
	in uint groupThreadID : SV_GroupIndex,
	in payload PayloadData payload,
	in uint groupID : SV_GroupID,
	out vertices InterpolantsVSToPS verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES])
{
	MeshData mesh = GetMesh(cObject.Mesh);

	uint meshletIndex = payload.Indices[groupID];
	if(meshletIndex >= mesh.MeshletCount)
	{
		return;
	}

	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	float4x4 world = GetTransform(cObject.World);
	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = BufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		InterpolantsVSToPS result = FetchVertexAttributes(mesh, world, vertexId);
		result.Seed = meshletIndex;
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);
	}
}

[RootSignature(RootSig)]
InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	InterpolantsVSToPS result = FetchVertexAttributes(mesh, world, vertexId);
	return result;
}

float3 ScreenSpaceReflections(float4 Position, float3 PositionVS, float3 N, float3 V, float R, inout float ssrWeight)
{
	float3 ssr = 0;
	const float roughnessThreshold = 0.7f;
	bool ssrEnabled = R < roughnessThreshold;
	if(ssrEnabled)
	{
		float reflectionThreshold = 0.0f;
		float3 reflectionWs = normalize(reflect(-V, N));
		if (dot(V, reflectionWs) <= reflectionThreshold)
		{
			float jitter = InterleavedGradientNoise(Position.xy, cView.FrameIndex) - 1.0f;
			uint maxSteps = cView.SsrSamples;

			float3 rayStartVS = PositionVS;
			float linearDepth = rayStartVS.z;
			float3 reflectionVs = mul(reflectionWs, (float3x3)cView.View);
			float3 rayEndVS = rayStartVS + (reflectionVs * linearDepth);

			float3 rayStart = ViewToWindow(rayStartVS, cView.Projection);
			float3 rayEnd = ViewToWindow(rayEndVS, cView.Projection);

			float3 rayStep = ((rayEnd - rayStart) / float(maxSteps));
			rayStep = rayStep / length(rayEnd.xy - rayStart.xy);
			float3 rayPos = rayStart + (rayStep * jitter);
			float zThickness = abs(rayStep.z);

			uint hitIndex = 0;
			float3 bestHit = rayPos;
			float prevSceneZ = rayStart.z;
			for (uint currStep = 0; currStep < maxSteps; currStep += 4)
			{
				uint4 step = float4(1, 2, 3, 4) + currStep;
				float4 sceneZ = float4(
					tDepth.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.x, 0).x,
					tDepth.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.y, 0).x,
					tDepth.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.z, 0).x,
					tDepth.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.w, 0).x
				);
				float4 currentPosition = rayPos.z + rayStep.z * step;
				uint4 zTest = abs(sceneZ - currentPosition - zThickness) < zThickness;
				uint zMask = (((zTest.x << 0) | (zTest.y << 1)) | (zTest.z << 2)) | (zTest.w << 3);
				if(zMask > 0)
				{
					uint firstHit = firstbitlow(zMask);
					if(firstHit > 0)
					{
						prevSceneZ = sceneZ[firstHit - 1];
					}
					bestHit = rayPos + (rayStep * float(currStep + firstHit + 1));
					float zAfter = sceneZ[firstHit] - bestHit.z;
					float zBefore = (prevSceneZ - bestHit.z) + rayStep.z;
					float weight = saturate(zAfter / (zAfter - zBefore));
					float3 prevRayPos = bestHit - rayStep;
					bestHit = (prevRayPos * weight) + (bestHit * (1.0f - weight));
					hitIndex = currStep + firstHit;
					break;
				}
				prevSceneZ = sceneZ.w;
			}

			float4 hitColor = 0;
			if (hitIndex > 0)
			{
				float4 UV = float4(bestHit.xy, 0, 1);
				UV = mul(UV, cView.ReprojectionMatrix);
				float2 distanceFromCenter = (float2(UV.x, UV.y) * 2.0f) - float2(1.0f, 1.0f);
				float edgeAttenuation = saturate((1.0 - ((float)hitIndex / maxSteps)) * 4.0f);
				edgeAttenuation *= smoothstep(0.0f, 0.5f, saturate(1.0 - dot(distanceFromCenter, distanceFromCenter)));
				float3 reflectionResult = tPreviousSceneColor.SampleLevel(sLinearClamp, UV.xy, 0).xyz;
				hitColor = float4(reflectionResult, edgeAttenuation);
			}
			float roughnessMask = saturate(1.0f - (R / roughnessThreshold));
			ssrWeight = (hitColor.w * roughnessMask);
			ssr = saturate(hitColor.xyz * ssrWeight);
		}
	}
	return ssr;
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

void PSMain(InterpolantsVSToPS input,
			float3 bary : SV_Barycentrics,
			out float4 outColor : SV_Target0,
			out float4 outNormalRoughness : SV_Target1)
{
	float2 screenUV = (float2)input.Position.xy * cView.ScreenDimensionsInv;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, screenUV, 0).r;
	float3 V = normalize(cView.ViewPosition.xyz - input.PositionWS);

	MaterialData material = GetMaterial(cObject.Material);
	material.BaseColorFactor *= UIntToColor(input.Color);
	MaterialProperties surface = GetMaterialProperties(material, input.UV);
	float3x3 TBN = CreateTangentToWorld(normalize(input.Normal), float4(normalize(input.Tangent.xyz), input.Tangent.w));
	float3 N = TangentSpaceNormalMapping(surface.NormalTS, TBN);

	BrdfData brdf = GetBrdfData(surface);

	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(input.Position, input.PositionVS, N, V, brdf.Roughness, ssrWeight);

	LightResult lighting = DoLight(input.Position, input.PositionWS, N, V, brdf.Diffuse, brdf.Specular, brdf.Roughness);

	float3 outRadiance = 0;
	outRadiance += lighting.Diffuse + lighting.Specular;
	outRadiance += ApplyAmbientLight(brdf.Diffuse, ambientOcclusion, GetLight(0).GetColor().rgb);
	outRadiance += ssr * ambientOcclusion;
	outRadiance += surface.Emissive;

	float fogSlice = sqrt((input.PositionVS.z - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 scatteringTransmittance = tLightScattering.SampleLevel(sLinearClamp, float3(screenUV, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;

	outColor = float4(outRadiance, surface.Opacity);
	float reflectivity = saturate(scatteringTransmittance.w * ambientOcclusion * Square(1 - brdf.Roughness));
	outNormalRoughness = float4(N, saturate(reflectivity - ssrWeight));

#define DEBUG_MESHLETS 0
#if DEBUG_MESHLETS
	outNormalRoughness = float4(input.Normal, 0);

	uint Seed = SeedThread(input.Seed);
	outColor = float4(Random01(Seed), Random01(Seed), Random01(Seed), 1);

	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	outColor = float4(outColor.xyz * saturate(minBary + 0.6), 1);
#endif
}
