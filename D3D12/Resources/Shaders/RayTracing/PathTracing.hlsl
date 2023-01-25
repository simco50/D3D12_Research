#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

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

LightResult EvaluateLight(Light light, float3 worldPos, float3 V, float3 N, float3 geometryNormal, BrdfData brdfData)
{
	LightResult result = (LightResult)0;
	float3 L;
	float attenuation = GetAttenuation(light, worldPos, L);
	if(attenuation <= 0.0f)
		return result;

	RayDesc rayDesc = CreateLightOcclusionRay(light, worldPos);
	RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
	attenuation *= TraceOcclusionRay(rayDesc, tlas);

	result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, L, attenuation);
	result.Diffuse *= light.GetColor() * light.Intensity;
	result.Specular *= light.GetColor() * light.Intensity;
	return result;
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

// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized!
float4 GetRotationToZAxis(float3 input)
{
	// Handle special case when input is exact or near opposite of (0, 0, 1)
	if (input.z < -0.99999f)
	{
		return float4(1.0f, 0.0f, 0.0f, 0.0f);
	}
	return normalize(float4(input.y, -input.x, 0.0f, 1.0f + input.z));
}

// Returns the quaternion with inverted rotation
float4 InvertRotation(float4 q)
{
	return float4(-q.x, -q.y, -q.z, q.w);
}

// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
float3 RotatePoint(float4 q, float3 v)
{
	float3 qAxis = float3(q.x, q.y, q.z);
	return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
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


void SampleSourceLight(inout uint seed, out uint lightIndex, out float sourcePdf)
{
	lightIndex = Random(seed, 0, cView.LightCount - 1);
	sourcePdf = 1.0f / cView.LightCount;
}

// From RT Gems 2
struct Reservoir
{
	uint LightSample;
	uint M;
	float TotalWeight;
	float SampleTargetPdf;
};

// Sample a random light by using Resampled Importance Sampling (RIS)
bool SampleLightRIS(inout uint seed, float3 position, float3 N, out int lightIndex, out float sampleWeight)
{
	// [Algorithm]
	// Pick N random lights from the full set of lights
	// Compute contribution of each light
	// If the light's weight is above a random threshold, pick it
	// Weight the selected light based on the total weight and light count

	if(cView.LightCount <= 0)
		return false;

	Reservoir reservoir;
	reservoir.TotalWeight = 0.0f;
	reservoir.M = RIS_CANDIDATES_LIGHTS;

	for(int i = 0; i < reservoir.M; ++i)
	{
		uint candidate = 0;
		float sourcePdf = 1.0f;
		SampleSourceLight(seed, candidate, sourcePdf);

		Light light = GetLight(candidate);
		float3 L;
		float targetPdf = GetLuminance(GetAttenuation(light, position, L) * light.GetColor());
		float risWeight = targetPdf / sourcePdf;
		reservoir.TotalWeight += risWeight;

		if(Random01(seed) < (risWeight / reservoir.TotalWeight))
		{
			reservoir.LightSample = candidate;
			reservoir.SampleTargetPdf = targetPdf;
		}
	}

	if(reservoir.TotalWeight == 0.0f)
		return false;

	lightIndex = reservoir.LightSample;
	sampleWeight = (reservoir.TotalWeight / reservoir.M) / reservoir.SampleTargetPdf;
	return true;
}

[shader("raygeneration")]
void RayGen()
{
	float2 pixel = float2(DispatchRaysIndex().xy);
	float2 resolution = float2(DispatchRaysDimensions().xy);
	uint seed = SeedThread(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, cView.FrameIndex);

	float3 previousColor = uAccumulation[DispatchRaysIndex().xy].rgb;

	// Jitter to achieve anti-aliasing
	float2 offset = float2(Random01(seed), Random01(seed));
	pixel += lerp(-0.5f.xx, 0.5f.xx, offset);

	pixel = (((pixel + 0.5f) / resolution) * 2.0f - 1.0f);
	RayDesc ray = GeneratePinholeCameraRay(pixel, cView.ViewInverse, cView.Projection);

	float3 radiance = 0;
	float3 throughput = 1;
	for(int i = 0; i < cPass.NumBounces; ++i)
	{
		RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
		MaterialRayPayload payload = TraceMaterialRay(ray, tlas);

		// If the ray didn't hit anything, accumulate the sky and break the loop
		if(!payload.IsHit())
		{
			const float3 SkyColor = GetSky(ray.Direction);
			radiance += throughput * SkyColor;
			break;
		}

		// Decode the hit payload to retrieve all the shading information
		InstanceData instance = GetInstance(payload.InstanceID);
		VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID);
		MaterialData material = GetMaterial(instance.MaterialIndex);
		MaterialProperties surface = EvaluateMaterial(material, vertex, 0);
		BrdfData brdfData = GetBrdfData(surface);

		float3 V = -ray.Direction;
		float3 hitLocation = ray.Origin + ray.Direction * payload.HitT;
		float3 geometryNormal = vertex.GeometryNormal;
		float3 N = surface.Normal;
		float4 T = vertex.Tangent;
		if(!payload.IsFrontFace())
			geometryNormal = -geometryNormal;

		if(dot(geometryNormal, N) < 0.0f)
		{
			N = -N;
			T.xyz = -T.xyz;
		}

		// The Emissive properties is like a light source and directly applied on top
		radiance += throughput * surface.Emissive;

		// Direct light evaluation
		int lightIndex = 0;
		float lightWeight = 0.0f;
		if(SampleLightRIS(seed, hitLocation, N, lightIndex, lightWeight))
		{
			LightResult result = EvaluateLight(GetLight(lightIndex), hitLocation, V, N, geometryNormal, brdfData);
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
		ray.Origin = hitLocation;
		OffsetRay(ray, geometryNormal);
	}

	// Accumulation and output
	if(cPass.AccumulatedFrames > 1)
	{
		radiance += previousColor;
	}
	uAccumulation[DispatchRaysIndex().xy] = float4(radiance, 1);
	uOutput[DispatchRaysIndex().xy] = float4(radiance, 1.0f) / cPass.AccumulatedFrames;
}
