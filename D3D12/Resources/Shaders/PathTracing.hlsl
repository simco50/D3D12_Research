#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"
#include "TonemappingCommon.hlsli"

#define MIN_BOUNCES 3
#define RIS_CANDIDATES_LIGHTS 8

#define RAY_INVALID -1
#define RAY_DIFFUSE 0
#define RAY_SPECULAR 1

GlobalRootSignature GlobalRootSig =
{
	"CBV(b0),"
	"CBV(b2),"
	"DescriptorTable(UAV(u0, numDescriptors = 2)),"
	"DescriptorTable(SRV(t5, numDescriptors = 7)),"
	GLOBAL_BINDLESS_TABLE ", "
	"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT),"
	"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s2, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc=COMPARISON_GREATER), " \
};

struct VertexAttribute
{
	float2 UV;
	float3 Normal;
	float3 GeometryNormal;
	int Material;
};

struct ViewData
{
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 ProjectionInverse;
	float4x4 Projection;
	uint NumLights;
	uint TLASIndex;
	uint FrameIndex;
	uint NumBounces;
	uint AccumulatedFrames;
};

RWTexture2D<float4> uOutput : register(u0);
RWTexture2D<float4> uAccumulation : register(u1);
ConstantBuffer<ViewData> cViewData : register(b0);

struct RAYPAYLOAD PrimaryRayPayload
{
	float2 UV;
	float3 Position;
	float3 Normal;
	float3 GeometryNormal;
	uint Material;
	uint Hit;
};

struct SurfaceData
{
	float Opacity;
	float3 Diffuse;
	float3 Specular;
	float Roughness;
	float3 Emissive;
};

float CastShadowRay(float3 origin, float3 direction)
{
	float len = length(direction);
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction / len;
	ray.TMin = RAY_BIAS;
	ray.TMax = len;

	RayQuery<
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

	rayQuery.TraceRayInline(
		tTLASTable[cViewData.TLASIndex], 	// AccelerationStructure
		0,									// RayFlags
		0xFF, 								// InstanceMask
		ray									// Ray
	);

	rayQuery.Proceed();

	return rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

SurfaceData GetShadingData(uint materialIndex, float2 uv, float mipLevel)
{
	MaterialData material = tMaterials[materialIndex];
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse >= 0)
	{
		baseColor *= tTexture2DTable[material.Diffuse].SampleLevel(sDiffuseSampler, uv, mipLevel);
	}
	float metalness = material.MetalnessFactor;
	float roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness >= 0)
	{
		float4 roughnessMetalnessSample = tTexture2DTable[material.RoughnessMetalness].SampleLevel(sDiffuseSampler, uv, mipLevel);
	 	metalness *= roughnessMetalnessSample.b;
	 	roughness *= roughnessMetalnessSample.g;
	}
	
	float3 emissive = material.EmissiveFactor.rgb;
	if(material.Emissive >= 0)
	{
		emissive *= tTexture2DTable[material.Emissive].SampleLevel(sDiffuseSampler, uv, mipLevel).rgb;
	}
	float specular = 0.5f;

	SurfaceData outData = (SurfaceData)0;
	outData.Diffuse = ComputeDiffuseColor(baseColor.rgb, metalness);
	outData.Specular = ComputeF0(specular, baseColor.rgb, metalness);
	outData.Roughness = roughness;
	outData.Emissive = emissive;
	outData.Opacity = baseColor.a;
	return outData;
}

LightResult EvaluateLight(Light light, float3 worldPos, float3 V, float3 N, float3 geometryNormal, SurfaceData surface)
{
	LightResult result = (LightResult)0;
	float attenuation = GetAttenuation(light, worldPos);
	if(attenuation <= 0.0f)
	{
		return result;
	}
	
	float3 L = light.Position - worldPos;
	if(light.IsDirectional())
	{
		L = RAY_MAX_T * -light.Direction;
	}

	if(attenuation <= 0.0f)
	{
		return result;
	}

	float3 viewPosition = mul(float4(worldPos, 1), cViewData.View).xyz;
	float4 pos = float4(0, 0, 0, viewPosition.z);
	int shadowIndex = GetShadowIndex(light, pos, worldPos);
	float4x4 lightViewProjection = cShadowData.LightViewProjections[shadowIndex];
	float4 lightPos = mul(float4(worldPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;
	if(shadowIndex >= 0)
	{
		attenuation *= LightTextureMask(light, shadowIndex, worldPos);
		float3 rayOrigin = worldPos;
		//float3 rayOrigin = OffsetRay(worldPos, geometryNormal);
		attenuation *= CastShadowRay(rayOrigin, L); 
	}

	L = normalize(L);
	result = DefaultLitBxDF(surface.Specular, surface.Roughness, surface.Diffuse, N, V, L, attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	return result;
}

struct VertexInput
{
	uint2 Position;
	uint UV;
	float3 Normal;
	float4 Tangent;
};

VertexAttribute GetVertexAttributes(float3 barycentrics)
{
	MeshData mesh = tMeshes[InstanceID()];
	uint3 indices = tBufferTable[mesh.IndexBuffer].Load<uint3>(PrimitiveIndex() * sizeof(uint3));
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
		dataOffset += sizeof(float4);
	}
	float4x3 worldMatrix = ObjectToWorld4x3();
	outData.Normal = normalize(mul(outData.Normal, (float3x3)worldMatrix));

	// Calculate geometry normal from triangle vertices positions
	float3 edge20 = positions[2] - positions[0];
	float3 edge21 = positions[2] - positions[1];
	float3 edge10 = positions[1] - positions[0];
	outData.GeometryNormal = mul(normalize(cross(edge20, edge10)), (float3x3)worldMatrix);

	return outData;
}

[shader("closesthit")] 
void PrimaryCHS(inout PrimaryRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	float3 barycentrics = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
	VertexAttribute vertex = GetVertexAttributes(barycentrics);

	payload.Hit = 1;
	payload.Material = vertex.Material;
	payload.UV = vertex.UV;
	payload.Normal = vertex.Normal;
	payload.GeometryNormal = vertex.GeometryNormal;
	payload.Position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
}

[shader("anyhit")]
void PrimaryAHS(inout PrimaryRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	float3 barycentrics = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
	VertexAttribute vertex = GetVertexAttributes(barycentrics);

	SurfaceData surface = GetShadingData(vertex.Material, vertex.UV, 0);
	if(surface.Opacity < 0.5)
	{
		IgnoreHit();
	}
}

[shader("miss")] 
void PrimaryMS(inout PrimaryRayPayload payload : SV_RayPayload) 
{
	payload.Hit = 0;
}

// Compute the probability of a specular ray depending on Fresnel term
// The Fresnel term is approximated because it's calculated with the shading normal instead of the half vector
float BRDFProbability(SurfaceData surface, float3 N, float3 V)
{
	float diffuseReflectance = GetLuminance(surface.Diffuse);
	float VdotN = max(0.05f, dot(V, N));
	float fresnel = saturate(GetLuminance(F_Schlick(surface.Specular, VdotN)));
	float specular = fresnel;
	float diffuse = diffuseReflectance * (1.0f - fresnel);
	float p = (specular / max(0.0001f, (specular + diffuse)));
	return clamp(p, 0.1f, 0.9f);
}

// Indirect light BRDF evaluation
bool EvaluateIndirectBRDF(int rayType, float2 u, SurfaceData surface, float3 N, float3 V, float3 geometryNormal, out float3 direction, out float3 weight)
{
	if(rayType == RAY_INVALID)
	{
		weight = float3(1, 0, 1);
		return false;
	}

	// Skip if the view ray is under the hemisphere
	if(dot(N, V) <= 0)
	{
		return false;
	}

	// Transform the view direction into the space of the shading normal
	// This will simplify evaluation as the local normal with become (0, 0, 1)
	float4 qRotationToZ = GetRotationToZAxis(N);
	float3 Vlocal = RotatePoint(qRotationToZ, V);
	const float3 Nlocal = float3(0.0f, 0.0f, 1.0f);

	float3 directionLocal = float3(0.0f, 0.0f, 0.0f);

	if(rayType == RAY_DIFFUSE)
	{
		// PDF of cosine weighted sample cancels out the diffuse term
		float pdf;
		directionLocal = HemisphereSampleCosineWeight(u, pdf);
		weight = surface.Diffuse;

		// Weight the diffuse term based on the specular term of random microfacet normal
		// (Diffuse == 1.0 - Fresnel)
		float alpha = Square(surface.Roughness);
		float3 Hspecular = SampleGGXVNDF(Vlocal, alpha.xx, u);
		float VdotH = max(0.00001f, min(1.0f, dot(Vlocal, Hspecular)));
		weight *= (1.0f.xxx - F_Schlick(surface.Specular, VdotH));
	}
	else if(rayType == RAY_SPECULAR)
	{
		float alpha = Square(surface.Roughness);
		float alphaSquared = Square(alpha);

		// Sample a microfacet normal (H) in local space
		float3 Hlocal;
		if (alpha == 0.0f) 
		{
			// Fast path for zero roughness (perfect reflection)
			// also prevents NaNs appearing due to divisions by zeroes
			Hlocal = float3(0.0f, 0.0f, 1.0f);
		} 
		else 
		{
			// For non-zero roughness, VNDF sampling for GG-X distribution
			Hlocal = SampleGGXVNDF(Vlocal, float2(alpha, alpha), u);
		}

		// Reflect view direction to obtain light vector
		float3 Llocal = reflect(-Vlocal, Hlocal);

		// Clamp dot products here to small value to prevent numerical instability.
		// Assume that rays incident from below the hemisphere have been filtered
		float HdotL = max(0.00001f, min(1.0f, dot(Hlocal, Llocal)));
		const float3 Nlocal = float3(0.0f, 0.0f, 1.0f);
		float NdotL = max(0.00001f, min(1.0f, dot(Nlocal, Llocal)));
		float NdotV = max(0.00001f, min(1.0f, dot(Nlocal, Vlocal)));
		float NdotH = max(0.00001f, min(1.0f, dot(Nlocal, Hlocal)));
		float3 F = F_Schlick(surface.Specular, HdotL);

		// Calculate weight of the sample specific for selected sampling method 
		// (this is microfacet BRDF divided by PDF of sampling method - notice how most terms cancel out)
		weight = F * Smith_G2_Over_G1_Height_Correlated(alpha, alphaSquared, NdotL, NdotV);

		directionLocal = Llocal;
	}

	// Don't trace direction if there is no contribution
	if(GetLuminance(weight) <= 0.0f)
	{
		return false;
	}
	
	// Rotate the direction back into vector space
	direction = normalize(RotatePoint(InvertRotation(qRotationToZ), directionLocal));

	// Don't trace under the hemisphere
	if(dot(geometryNormal, direction) <= 0.0f)
	{
		return false;
	}
	return true;
}

// Sample a random light by using Resampled Importance Sampling (RIS)
bool SampleLightRIS(inout uint seed, float3 position, float3 N, out int lightIndex, out float sampleWeight)
{
	// [Algorithm]
	// Pick N random lights from the full set of lights
	// Compute contribution of each light
	// If the light's weight is above a random threshold, pick it
	// Weight the selected light based on the total weight and light count

	if(cViewData.NumLights <= 0)
	{
		return false;
	}
	lightIndex = 0;
	float totalWeights = 0;
	float samplePdfG = 0;
	for(int i = 0; i < RIS_CANDIDATES_LIGHTS; ++i)
	{
		float candidateWeight = (float)cViewData.NumLights;
		int candidate = Random(seed, 0, cViewData.NumLights);
		Light light = tLights[candidate];
		float3 L = normalize(light.Position - position);
		if(dot(N, L) < 0.0f) 
		{
			continue;
		}
		float candidatePdfG = GetLuminance(GetAttenuation(light, position) * light.GetColor().xyz);
		float candidateRISWeight = candidatePdfG * candidateWeight;
		totalWeights += candidateRISWeight;
		if(Random01(seed) < (candidateRISWeight / totalWeights))
		{
			lightIndex = candidate;
			samplePdfG = candidatePdfG;
		}
	}

	if(totalWeights == 0.0f)
	{
		return false;
	}
	sampleWeight = (totalWeights / (float)cViewData.NumLights) / samplePdfG;
	return true;
}

[shader("raygeneration")] 
void RayGen() 
{
	float2 pixel = float2(DispatchRaysIndex().xy);
	float2 resolution = float2(DispatchRaysDimensions().xy);
	uint seed = SeedThread(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, cViewData.FrameIndex);

	// Jitter to achieve anti-aliasing
	float2 offset = float2(Random01(seed), Random01(seed));
	pixel += lerp(-0.5f.xx, 0.5f.xx, offset);

	pixel = (((pixel + 0.5f) / resolution) * 2.0f - 1.0f);
	Ray ray = GeneratePinholeCameraRay(pixel, cViewData.ViewInverse, cViewData.Projection);

	float3 radiance = 0;
	float3 throughput = 1;
	for(int i = 0; i < cViewData.NumBounces; ++i)
	{
		PrimaryRayPayload payload = (PrimaryRayPayload)0;

		RayDesc desc;
		desc.Origin = ray.Origin;
		desc.Direction = ray.Direction;
		desc.TMin = RAY_BIAS;
		desc.TMax = RAY_MAX_T;

		TraceRay(
			tTLASTable[cViewData.TLASIndex],	//AccelerationStructure
			0, 									//RayFlags
			0xFF, 								//InstanceInclusionMask
			0,									//RayContributionToHitGroupIndex
			0, 									//MultiplierForGeometryContributionToHitGroupIndex
			0, 									//MissShaderIndex
			desc, 								//Ray
			payload 							//Payload
		);

		// If the ray didn't hit anything, accumulate the sky and break the loop
		if(!payload.Hit)
		{
			const float3 SkyColor = 3; //CIESky(desc.Direction, -tLights[0].Direction);
			radiance += throughput * SkyColor;
			break;
		}

		// Decode the hit payload to retrieve all the shading information
		SurfaceData surface = GetShadingData(payload.Material, payload.UV, 0);

		// Flip the normal towards the incoming ray
		float3 N = payload.Normal;
		float3 V = -desc.Direction;
		float3 geometryNormal = payload.GeometryNormal;
		if(dot(geometryNormal, V) < 0.0f)
		{ 
			geometryNormal = -geometryNormal;
		}
		if(dot(geometryNormal, N) < 0.0f)
		{
			N = -N;
		}

		// The Emissive properties is like a light source and directly applied on top
		radiance += throughput * surface.Emissive;

		// Direct light evaluation
		int lightIndex = 0;
		float lightWeight = 0.0f;
		if(SampleLightRIS(seed, payload.Position, N, lightIndex, lightWeight))
		{
			LightResult result = EvaluateLight(tLights[lightIndex], payload.Position, V, N, payload.GeometryNormal, surface);
			radiance += throughput * (result.Diffuse + result.Specular) * lightWeight;
		}

		// If we're at the last bounce, no point in computing the next ray
		if(i == cViewData.NumBounces - 1)
		{
			break;
		}

		// Russian Roulette ray elimination
		// Kill the ray based on the current throughput
		// We must correctly weigh the sample based on this elimination to account for bias
		if(i > MIN_BOUNCES)
		{
			float rrProbability = min(0.95f, GetLuminance(throughput));
			if(rrProbability < Random01(seed))
			{
				break;
			}
			else
			{
				throughput /= rrProbability;
			}
		}

		// Produce new ray based on BRDF probability
		int rayType = RAY_INVALID;
		float brdfProbability = BRDFProbability(surface, N, V);
		if(Random01(seed) < brdfProbability)
		{
			rayType = RAY_SPECULAR;
			throughput /= brdfProbability;
		}
		else
		{
			rayType = RAY_DIFFUSE;
			throughput /= (1.0f - brdfProbability);
		}

		// Evaluate the BRDF based on the selected ray type (ie. Diffuse vs. Specular)
		float3 weight;
		if(!EvaluateIndirectBRDF(rayType, float2(Random01(seed), Random01(seed)), surface, N, V, geometryNormal, ray.Direction, weight))
		{
			break;
		}

		// Propagate the weight and define the new ray origin
		throughput *= weight;
		//ray.Origin = OffsetRay(payload.Position, geometryNormal);
		ray.Origin = payload.Position;
	}

	// Accumulation and output
	if(cViewData.AccumulatedFrames > 1)
	{
		float3 previousColor = uAccumulation[DispatchRaysIndex().xy].rgb;
		radiance += previousColor;
	}
	uAccumulation[DispatchRaysIndex().xy] = float4(radiance, 1);
	uOutput[DispatchRaysIndex().xy] = float4(radiance, 1.0f) / cViewData.AccumulatedFrames;
}
