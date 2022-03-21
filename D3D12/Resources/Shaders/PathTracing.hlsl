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

struct PassParameters
{
	uint NumBounces;
	uint AccumulatedFrames;
};

RWTexture2D<float4> uOutput : register(u0);
RWTexture2D<float4> uAccumulation : register(u1);
ConstantBuffer<PassParameters> cPass : register(b0);

struct RAYPAYLOAD ShadowRayPayload
{
	uint Hit RAYQUALIFIER(read(caller) : write(caller, miss));
};

struct RAYPAYLOAD PrimaryRayPayload
{
	float2 UV;
	float2 Normal;
	float3 Position;
	uint Color;
	int TangentSign;
	float2 Tangent;
	float2 GeometryNormal;
	uint Material;

	bool IsHit()
	{
		return Material != -1;
	}
};

float CastShadowRay(float3 origin, float3 direction)
{
	float len = length(direction);
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction / len;
	ray.TMin = RAY_BIAS;
	ray.TMax = len;

	const int rayFlags =
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

	RaytracingAccelerationStructure TLAS = ResourceDescriptorHeap[cView.TLASIndex];

// Inline RT for the shadow rays has better performance. Use it when available.
#if _INLINE_RT
	RayQuery<rayFlags> q;

	q.TraceRayInline(
		TLAS, 	// AccelerationStructure
		0,		// RayFlags
		0xFF, 	// InstanceMask
		ray		// Ray
	);

	while(q.Proceed())
	{
		switch(q.CandidateType())
		{
			case CANDIDATE_NON_OPAQUE_TRIANGLE:
			{
				MeshInstance instance = GetMeshInstance(q.CandidateInstanceID());
				VertexAttribute vertex = GetVertexAttributes(instance, q.CandidateTriangleBarycentrics(), q.CandidatePrimitiveIndex(), q.CandidateObjectToWorld4x3());
				MaterialData material = GetMaterial(instance.Material);
				MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);
				if(surface.Opacity > material.AlphaCutoff)
				{
					q.CommitNonOpaqueTriangleHit();
				}
			}
			break;
		}
	}
	return q.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
#else
	ShadowRayPayload payload;
	payload.Hit = 1;
	TraceRay(
		TLAS,		//AccelerationStructure
		rayFlags, 	//RayFlags
		0xFF, 		//InstanceInclusionMask
		0,			//RayContributionToHitGroupIndex
		0, 			//MultiplierForGeometryContributionToHitGroupIndex
		1, 			//MissShaderIndex
		ray, 		//Ray
		payload 	//Payload
	);
	return !payload.Hit;
#endif
}

LightResult EvaluateLight(Light light, float3 worldPos, float3 V, float3 N, float3 geometryNormal, BrdfData brdfData)
{
	LightResult result = (LightResult)0;
	float attenuation = GetAttenuation(light, worldPos);
	if(attenuation <= 0.0f)
	{
		return result;
	}

	float3 L = light.Position - worldPos;
	if(light.IsDirectional)
	{
		L = RAY_MAX_T * -light.Direction;
	}

	if(attenuation <= 0.0f)
	{
		return result;
	}

	float3 viewPosition = mul(float4(worldPos, 1), cView.View).xyz;
	float4 pos = float4(0, 0, 0, viewPosition.z);
	int shadowIndex = GetShadowIndex(light, pos, worldPos);
	if(shadowIndex >= 0)
	{
		attenuation *= LightTextureMask(light, shadowIndex, worldPos);
	}

	if(attenuation <= 0.0f)
	{
		return result;
	}

	float3 rayOrigin = worldPos;
	//float3 rayOrigin = OffsetRay(worldPos, geometryNormal);
	attenuation *= CastShadowRay(rayOrigin, L);

	L = normalize(L);
	result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, L, attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	return result;
}

[shader("closesthit")]
void PrimaryCHS(inout PrimaryRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	MeshInstance instance = GetMeshInstance(InstanceID());
	VertexAttribute vertex = GetVertexAttributes(instance, attrib.barycentrics, PrimitiveIndex(), ObjectToWorld4x3());
	payload.Material = instance.Material;
	payload.UV = vertex.UV;
	payload.Normal = EncodeNormalOctahedron(vertex.Normal);
	payload.Tangent = EncodeNormalOctahedron(vertex.Tangent.xyz);
	payload.TangentSign = vertex.Tangent.w;
	payload.GeometryNormal = EncodeNormalOctahedron(vertex.GeometryNormal);
	payload.Position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	payload.Color = vertex.Color;
}

[shader("anyhit")]
void PrimaryAHS(inout PrimaryRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	MeshInstance instance = GetMeshInstance(InstanceID());
	VertexAttribute vertex = GetVertexAttributes(instance, attrib.barycentrics, PrimitiveIndex(), ObjectToWorld4x3());
	MaterialData material = GetMaterial(instance.Material);
	material.BaseColorFactor *= UIntToColor(vertex.Color);
	MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);
	uint seed = SeedThread(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, cView.FrameIndex);
	float r = Random01(seed);
	if(surface.Opacity < r)
	{
		IgnoreHit();
	}
}

[shader("miss")]
void PrimaryMS(inout PrimaryRayPayload payload : SV_RayPayload)
{
	// Nothing to do here! :)
}

[shader("miss")]
void ShadowMS(inout ShadowRayPayload payload : SV_RayPayload)
{
	payload.Hit = 0;
}

// Compute the probability of a specular ray depending on Fresnel term
// The Fresnel term is approximated because it's calculated with the shading normal instead of the half vector
float BRDFProbability(BrdfData brdfData, float3 N, float3 V)
{
	float diffuseReflectance = GetLuminance(brdfData.Diffuse);
	float VdotN = max(0.05f, dot(V, N));
	float fresnel = saturate(GetLuminance(F_Schlick(brdfData.Specular, VdotN)));
	float specular = fresnel;
	float diffuse = diffuseReflectance * (1.0f - fresnel);
	float p = (specular / max(0.0001f, (specular + diffuse)));
	return clamp(p, 0.1f, 0.9f);
}

// Indirect light BRDF evaluation
bool EvaluateIndirectBRDF(int rayType, float2 u, BrdfData brdfData, float3 N, float3 V, float3 geometryNormal, out float3 direction, out float3 weight)
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
		weight = brdfData.Diffuse;

		// Weight the diffuse term based on the specular term of random microfacet normal
		// (Diffuse == 1.0 - Fresnel)
		float alpha = Square(brdfData.Roughness);
		float3 Hspecular = SampleGGXVNDF(Vlocal, alpha.xx, u);
		float VdotH = max(0.00001f, min(1.0f, dot(Vlocal, Hspecular)));
		weight *= (1.0f.xxx - F_Schlick(brdfData.Specular, VdotH));
	}
	else if(rayType == RAY_SPECULAR)
	{
		float alpha = Square(brdfData.Roughness);
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
			// For non-zero roughness, VNDF sampling for GGX distribution
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
		float3 F = F_Schlick(brdfData.Specular, HdotL);
		float G = Smith_G2_Over_G1_Height_Correlated(alpha, alphaSquared, NdotL, NdotV);

		// Calculate weight of the sample specific for selected sampling method
		// This is microfacet BRDF divided by PDF of sampling method
		// Due to the clever VNDF sampling method, many of the terms cancel out
		weight = F * G;

		// Kulla17 - Energy conervation due to multiple scattering
		float gloss = Pow4(1 - brdfData.Roughness);
		float3 DFG = EnvDFGPolynomial(brdfData.Specular, gloss, NdotV);
		float3 energyCompensation = 1.0f + brdfData.Specular * (1.0f / DFG.y - 1.0f);
		weight *= energyCompensation;

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

	if(cView.LightCount <= 0)
	{
		return false;
	}
	lightIndex = 0;
	float totalWeights = 0;
	float samplePdfG = 0;
	for(int i = 0; i < RIS_CANDIDATES_LIGHTS; ++i)
	{
		float candidateWeight = (float)cView.LightCount;
		int candidate = Random(seed, 0, cView.LightCount - 1);
		Light light = GetLight(candidate);
		float3 L = normalize(light.Position - position);
		if(light.IsDirectional)
		{
			L = -light.Direction;
		}
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
	sampleWeight = totalWeights / RIS_CANDIDATES_LIGHTS / samplePdfG;
	return true;
}

[shader("raygeneration")]
void RayGen()
{
	float2 pixel = float2(DispatchRaysIndex().xy);
	float2 resolution = float2(DispatchRaysDimensions().xy);
	uint seed = SeedThread(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, cView.FrameIndex);

	// Jitter to achieve anti-aliasing
	float2 offset = float2(Random01(seed), Random01(seed));
	pixel += lerp(-0.5f.xx, 0.5f.xx, offset);

	pixel = (((pixel + 0.5f) / resolution) * 2.0f - 1.0f);
	Ray ray = GeneratePinholeCameraRay(pixel, cView.ViewInverse, cView.Projection);

	float3 radiance = 0;
	float3 throughput = 1;
	for(int i = 0; i < cPass.NumBounces; ++i)
	{
		PrimaryRayPayload payload = (PrimaryRayPayload)0;
		payload.Material = -1;

		RayDesc desc;
		desc.Origin = ray.Origin;
		desc.Direction = ray.Direction;
		desc.TMin = RAY_BIAS;
		desc.TMax = RAY_MAX_T;

		RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];

		TraceRay(
			tlas,		//AccelerationStructure
			0, 									//RayFlags
			0xFF, 								//InstanceInclusionMask
			0,									//RayContributionToHitGroupIndex
			0, 									//MultiplierForGeometryContributionToHitGroupIndex
			0, 									//MissShaderIndex
			desc, 								//Ray
			payload 							//Payload
		);

		// If the ray didn't hit anything, accumulate the sky and break the loop
		if(!payload.IsHit())
		{
			const float3 SkyColor = GetSky(desc.Direction);
			radiance += throughput * SkyColor;
			break;
		}

		// Decode the hit payload to retrieve all the shading information
		MaterialData material = GetMaterial(payload.Material);
		material.BaseColorFactor *= UIntToColor(payload.Color);
		MaterialProperties surface = GetMaterialProperties(material, payload.UV, 0);
		BrdfData brdfData = GetBrdfData(surface);

		// Flip the normal towards the incoming ray
		float3 N = DecodeNormalOctahedron(payload.Normal);
		float3 T = DecodeNormalOctahedron(payload.Tangent);
		float3 V = -desc.Direction;
		float3 geometryNormal = DecodeNormalOctahedron(payload.GeometryNormal);
		if(dot(geometryNormal, V) < 0.0f)
		{
			geometryNormal = -geometryNormal;
		}
		if(dot(geometryNormal, N) < 0.0f)
		{
			N = -N;
			T = -T;
		}

		float3x3 TBN = { T, cross(N, T) * payload.TangentSign, N };
		N = TangentSpaceNormalMapping(surface.NormalTS, TBN);

		// The Emissive properties is like a light source and directly applied on top
		radiance += throughput * surface.Emissive;

		// Direct light evaluation
		int lightIndex = 0;
		float lightWeight = 0.0f;
		if(SampleLightRIS(seed, payload.Position, N, lightIndex, lightWeight))
		{
			LightResult result = EvaluateLight(GetLight(lightIndex), payload.Position, V, N, geometryNormal, brdfData);
			radiance += throughput * (result.Diffuse + result.Specular) * lightWeight;
		}

		// If we're at the last bounce, no point in computing the next ray
		if(i == cPass.NumBounces - 1)
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
		float brdfProbability = BRDFProbability(brdfData, N, V);
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
		if(!EvaluateIndirectBRDF(rayType, float2(Random01(seed), Random01(seed)), brdfData, N, V, geometryNormal, ray.Direction, weight))
		{
			break;
		}

		// Propagate the weight and define the new ray origin
		throughput *= weight;
		//ray.Origin = OffsetRay(payload.Position, geometryNormal);
		ray.Origin = payload.Position;
	}

	// Accumulation and output
	if(cPass.AccumulatedFrames > 1)
	{
		float3 previousColor = uAccumulation[DispatchRaysIndex().xy].rgb;
		radiance += previousColor;
	}
	uAccumulation[DispatchRaysIndex().xy] = float4(radiance, 1);
	uOutput[DispatchRaysIndex().xy] = float4(radiance, 1.0f) / cPass.AccumulatedFrames;
}
