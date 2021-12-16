#include "CommonBindings.hlsli"
#include "Lighting.hlsli"
#include "Random.hlsli"

#define BLOCK_SIZE 16

#define RootSig ROOT_SIG("RootConstants(num32BitConstants=2, b0), " \
	"CBV(b1), " \
	"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
	"DescriptorTable(SRV(t2, numDescriptors = 11))")

struct PerViewData
{
	float4x4 View;
	float4x4 Projection;
	float4x4 ProjectionInverse;
	float4x4 ViewProjection;
	float4x4 ReprojectionMatrix;
	float4 ViewPosition;
	float4 FrustumPlanes[6];
	float2 InvScreenDimensions;
	float NearZ;
	float FarZ;
	int FrameIndex;
	int SsrSamples;
	int LightCount;
	int padd;
#if CLUSTERED_FORWARD
	int4 ClusterDimensions;
	int2 ClusterSize;
	float2 LightGridParams;
#endif
	int3 VolumeFogDimensions;
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct PSInput
{
	float4 position : SV_POSITION;
	float3 positionWS : POSITION_WS;
	float3 positionVS : POSITION_VS;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
	uint seed : SEED;
};

Texture3D<float4> tLightScattering : register(t2);
StructuredBuffer<uint> tLightIndexList : register(t4);

#if CLUSTERED_FORWARD
StructuredBuffer<uint> tLightGrid : register(t3);
uint GetSliceFromDepth(float depth)
{
	return floor(log(depth) * cViewData.LightGridParams.x - cViewData.LightGridParams.y);
}
#elif TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t3);
#endif

float ScreenSpaceShadows(float3 worldPos, float3 lightDirection, int stepCount, float rayLength, float ditherOffset)
{
	float4 rayStartPS = mul(float4(worldPos, 1), cViewData.ViewProjection);
	float4 rayDirPS = mul(float4(-lightDirection * rayLength, 0), cViewData.ViewProjection);
	float4 rayEndPS = rayStartPS + rayDirPS;
	rayStartPS.xyz /= rayStartPS.w;
	rayEndPS.xyz /= rayEndPS.w;
	float3 rayStep = rayEndPS.xyz - rayStartPS.xyz;
	float stepSize = 1.0f / stepCount;

	float4 rayDepthClip = rayStartPS + mul(float4(0, 0, rayLength, 0), cViewData.Projection);
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
	uint3 clusterIndex3D = uint3(floor(pos.xy / cViewData.ClusterSize), GetSliceFromDepth(pos.w));
	uint tileIndex = clusterIndex3D.x + (cViewData.ClusterDimensions.x * (clusterIndex3D.y + cViewData.ClusterDimensions.y * clusterIndex3D.z));
#endif

#if TILED_FORWARD
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint startOffset = tLightGrid[tileIndex * 2];
	uint lightCount = tLightGrid[tileIndex * 2 + 1];
#else
	uint lightCount = cViewData.LightCount;
#endif

	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
#if TILED_FORWARD || CLUSTERED_FORWARD
		uint lightIndex = tLightIndexList[startOffset + i];
#else
		uint lightIndex = i;
#endif
		Light light = tLights[lightIndex];
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, worldPos, N, V);

#define SCREEN_SPACE_SHADOWS 0
#if SCREEN_SPACE_SHADOWS
		float3 L = normalize(worldPos - light.Position);
		if(light.IsDirectional())
		{
			L = light.Direction;
		}

		float ditherValue = InterleavedGradientNoise(pos.xy, cViewData.FrameIndex);
		float length = 0.1f * pos.w * cViewData.ProjectionInverse[1][1];
		float occlusion = ScreenSpaceShadows(worldPos, L, 8, length, ditherValue);

		result.Diffuse *= occlusion;
		result.Specular *= occlusion;
#endif

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

template<typename T>
T BufferLoad(uint bufferIndex, uint elementIndex, uint byteOffset = 0)
{
	ByteAddressBuffer buffer = tBufferTable[bufferIndex];
	return buffer.Load<T>(elementIndex * sizeof(T) + byteOffset);
}

PSInput FetchVertexAttributes(MeshInstance instance, MeshData mesh, uint vertexId)
{
	PSInput result;
	float3 position = UnpackHalf3(BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.PositionsOffset));
	result.positionWS = mul(float4(position, 1.0f), instance.World).xyz;
	result.positionVS = mul(float4(result.positionWS, 1.0f), cViewData.View).xyz;
	result.position = mul(float4(result.positionWS, 1.0f), cViewData.ViewProjection);

	result.texCoord = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));

	NormalData normalData = BufferLoad<NormalData>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
	result.normal = normalize(mul(normalData.Normal, (float3x3)instance.World));
	result.tangent = float4(normalize(mul(normalData.Tangent.xyz, (float3x3)instance.World)), normalData.Tangent.w);

	result.seed = vertexId;

	return result;
}

struct PayloadData
{
	uint Indices[32];
};

groupshared PayloadData gsPayload;

bool IsVisible(MeshInstance instance, MeshData mesh, uint meshlet)
{
	MeshletBounds cullData = BufferLoad<MeshletBounds>(mesh.BufferIndex, meshlet, mesh.MeshletBoundsOffset);

	float4 center = mul(float4(cullData.Center, 1), instance.World);

#define FRUSTUM_CULL 1
#define CONE_CULL 0

#if FRUSTUM_CULL
	for(int i = 0; i < 6; ++i)
	{
		if(dot(center, cViewData.FrustumPlanes[i]) > cullData.Radius)
		{
			return false;
		}
	}
#endif

#if CONE_CULL
	float3 viewLocation = cViewData.ViewPosition.xyz;
	float3 coneApex = mul(float4(cullData.ConeApex, 1), instance.World).xyz;
	float3 coneAxis = mul(cullData.ConeAxis, (float3x3)instance.World);
	float3 view = normalize(viewLocation - coneApex);
	if (dot(view, coneAxis) >= cullData.ConeCutoff)
	{
		return false;
	}
#endif

	return true;
}

[numthreads(32, 1, 1)]
void ASMain(uint threadID : SV_DispatchThreadID)
{
    bool visible = false;

	MeshInstance instance = tMeshInstances[cObjectData.Index];
	MeshData mesh = tMeshes[instance.Mesh];
    if (threadID < mesh.MeshletCount)
    {
        visible = IsVisible(instance, mesh, threadID);
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
	out vertices PSInput verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES])
{
	MeshInstance instance = tMeshInstances[cObjectData.Index];
	MeshData mesh = tMeshes[instance.Mesh];

	uint meshletIndex = payload.Indices[groupID];
	if(meshletIndex >= mesh.MeshletCount)
	{
		return;
	}

	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = BufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		PSInput result = FetchVertexAttributes(instance, mesh, vertexId);
		result.seed = meshletIndex;
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);
	}
}

[RootSignature(RootSig)]
PSInput VSMain(uint vertexId : SV_VertexID)
{
	MeshInstance instance = tMeshInstances[cObjectData.Index];
	MeshData mesh = tMeshes[instance.Mesh];
	PSInput result = FetchVertexAttributes(instance, mesh, vertexId);
	return result;
}

float3 ScreenSpaceReflections(float4 position, float3 positionVS, float3 N, float3 V, float R, inout float ssrWeight)
{
	float3 ssr = 0;
	const float roughnessThreshold = 0.6f;
	bool ssrEnabled = R < roughnessThreshold;
	if(ssrEnabled)
	{
		float reflectionThreshold = 0.0f;
		float3 reflectionWs = normalize(reflect(-V, N));
		if (dot(V, reflectionWs) <= reflectionThreshold)
		{
			uint frameIndex = cViewData.FrameIndex;
			float jitter = InterleavedGradientNoise(position.xy, frameIndex) - 1.0f;
			uint maxSteps = cViewData.SsrSamples.x;

			float3 rayStartVS = positionVS;
			float linearDepth = rayStartVS.z;
			float3 reflectionVs = mul(reflectionWs, (float3x3)cViewData.View);
			float3 rayEndVS = rayStartVS + (reflectionVs * linearDepth);

			float3 rayStart = ViewToWindow(rayStartVS, cViewData.Projection);
			float3 rayEnd = ViewToWindow(rayEndVS, cViewData.Projection);

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
				float4 texCoord = float4(bestHit.xy, 0, 1);
				texCoord = mul(texCoord, cViewData.ReprojectionMatrix);
				float2 distanceFromCenter = (float2(texCoord.x, texCoord.y) * 2.0f) - float2(1.0f, 1.0f);
				float edgeAttenuation = saturate((1.0 - ((float)hitIndex / maxSteps)) * 4.0f);
				edgeAttenuation *= smoothstep(0.0f, 0.5f, saturate(1.0 - dot(distanceFromCenter, distanceFromCenter)));
				float3 reflectionResult = tPreviousSceneColor.SampleLevel(sLinearClamp, texCoord.xy, 0).xyz;
				hitColor = float4(reflectionResult, edgeAttenuation);
			}
			float roughnessMask = saturate(1.0f - (R / roughnessThreshold));
			ssrWeight = (hitColor.w * roughnessMask);
			ssr = saturate(hitColor.xyz * ssrWeight);
		}
	}
	return ssr;
}

void PSMain(PSInput input,
			float3 bary : SV_Barycentrics,
			out float4 outColor : SV_TARGET0,
			out float4 outNormalRoughness : SV_TARGET1)
{
	float2 screenUV = (float2)input.position.xy * cViewData.InvScreenDimensions;
	float ambientOcclusion = tAO.SampleLevel(sLinearClamp, screenUV, 0).r;

// Surface Shader BEGIN
	MeshInstance instance = tMeshInstances[cObjectData.Index];
	MaterialData material = tMaterials[instance.Material];

	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse >= 0)
	{
		baseColor *= Sample2D(material.Diffuse, sMaterialSampler, input.texCoord);
	}
	float roughness = material.RoughnessFactor;
	float metalness = material.MetalnessFactor;
	if(material.RoughnessMetalness >= 0)
	{
		float4 roughnessMetalness = Sample2D(material.RoughnessMetalness, sMaterialSampler, input.texCoord);
		metalness *= roughnessMetalness.b;
		roughness *= roughnessMetalness.g;
	}
	float4 emissive = material.EmissiveFactor;
	if(material.Emissive >= 0)
	{
		emissive *= Sample2D(material.Emissive, sMaterialSampler, input.texCoord);
	}
	float3 specular = 0.5f;

	float3 N = normalize(input.normal);
	if(material.Normal >= 0)
	{
		float3 T = normalize(input.tangent.xyz);
		float3 B = cross(N, T) * input.tangent.w;
		float3x3 TBN = float3x3(T, B, N);
		float3 tangentNormal = Sample2D(material.Normal, sMaterialSampler, input.texCoord).xyz;
		N = TangentSpaceNormalMapping(tangentNormal, TBN);
	}
// Surface Shader END

	float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
	float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);
	float3 V = normalize(cViewData.ViewPosition.xyz - input.positionWS);

	float ssrWeight = 0;
	float3 ssr = ScreenSpaceReflections(input.position, input.positionVS, N, V, roughness, ssrWeight);

	LightResult lighting = DoLight(input.position, input.positionWS, N, V, diffuseColor, specularColor, roughness);

	float3 outRadiance = 0;
	outRadiance += lighting.Diffuse + lighting.Specular;
	outRadiance += ApplyAmbientLight(diffuseColor, ambientOcclusion, tLights[0].GetColor().rgb * 0.1f);
	outRadiance += ssr * ambientOcclusion;
	outRadiance += emissive.rgb;

// Hack: volfog only working in clustered path right now...
#if CLUSTERED_FORWARD
	float fogSlice = sqrt((input.positionVS.z - cViewData.FarZ) / (cViewData.NearZ - cViewData.FarZ));
	float4 scatteringTransmittance = tLightScattering.SampleLevel(sLinearClamp, float3(screenUV, fogSlice), 0);
	outRadiance = outRadiance * scatteringTransmittance.w + scatteringTransmittance.rgb;
#else
	float4 scatteringTransmittance = 1;
#endif

	outColor = float4(outRadiance, baseColor.a);
	float reflectivity = saturate(scatteringTransmittance.w * ambientOcclusion * Square(1 - roughness));
	outNormalRoughness = float4(N, saturate(reflectivity - ssrWeight));
	//outNormalRoughness = float4(input.normal, 1);

	return;

	outNormalRoughness = float4(input.normal, 0);

	uint seed = SeedThread(input.seed);
	outColor = float4(Random01(seed), Random01(seed), Random01(seed), 1);

	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	outColor = float4(outColor.xyz * saturate(minBary + 0.6), 1);
}
