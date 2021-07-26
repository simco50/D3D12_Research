#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

#define RAY_CONE_TEXTURE_LOD 1
#define SECONDARY_SHADOW_RAY 1

GlobalRootSignature GlobalRootSig =
{
	"CBV(b0),"
	"CBV(b2),"
	"DescriptorTable(UAV(u0, numDescriptors = 1)),"
	"DescriptorTable(SRV(t5, numDescriptors = 7)),"
	GLOBAL_BINDLESS_TABLE ", "
	"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT),"
	"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s2, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc=COMPARISON_GREATER), " \
};

struct ViewData
{
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 ProjectionInverse;
	uint NumLights;
	float ViewPixelSpreadAngle;
	uint TLASIndex;
	uint FrameIndex;
};

RWTexture2D<float4> uOutput : register(u0);
ConstantBuffer<ViewData> cViewData : register(b0);

struct RAYPAYLOAD ReflectionRayPayload
{
	float3 output 		RAYQUALIFIER(read(caller, closesthit) : write(caller, closesthit, miss));
	RayCone rayCone 	RAYQUALIFIER(read(caller, closesthit) : write(caller, closesthit));
};

struct RAYPAYLOAD ShadowRayPayload
{
	uint Hit RAYQUALIFIER(read(caller) : write(caller, miss));
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

	RaytracingAccelerationStructure TLAS = tTLASTable[cViewData.TLASIndex];

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
				VertexAttribute vertex = GetVertexAttributes(q.CandidateTriangleBarycentrics(), q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateObjectToWorld4x3());
				MaterialProperties surface = GetMaterialProperties(vertex.Material, vertex.UV, 0);
				if(surface.Opacity > 0.5f)
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

ReflectionRayPayload CastReflectionRay(float3 origin, float3 direction, float T)
{
	RayCone cone;
	cone.Width = 0;
	cone.SpreadAngle = cViewData.ViewPixelSpreadAngle;

	ReflectionRayPayload payload;
	payload.rayCone = PropagateRayCone(cone, 0.0f, T);
	payload.output = 0.0f;

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = RAY_BIAS;
	ray.TMax = RAY_MAX_T;

	TraceRay(
		tTLASTable[cViewData.TLASIndex],		 			//AccelerationStructure
		0,									 				//RayFlags
		0xFF, 												//InstanceInclusionMask
		0,													//RayContributionToHitGroupIndex
		0, 													//MultiplierForGeometryContributionToHitGroupIndex
		0, 													//MissShaderIndex
		ray, 												//Ray
		payload 											//Payload
	);

	return payload;
}

LightResult EvaluateLight(Light light, float3 worldPos, float3 V, float3 N, BrdfData brdfData)
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

	float3 viewPosition = mul(float4(worldPos, 1), cViewData.View).xyz;
	float4 pos = float4(0, 0, 0, viewPosition.z);
	int shadowIndex = GetShadowIndex(light, pos, worldPos);
	bool castShadowRay = true;
	if(shadowIndex >= 0)
	{
		float4x4 lightViewProjection = cShadowData.LightViewProjections[shadowIndex];
		float4 lightPos = mul(float4(worldPos, 1), lightViewProjection);
		lightPos.xyz /= lightPos.w;
		lightPos.x = lightPos.x / 2.0f + 0.5f;
		lightPos.y = lightPos.y / -2.0f + 0.5f;
		attenuation *= LightTextureMask(light, shadowIndex, worldPos);

		if(all(lightPos >= 0) && all(lightPos <= 1))
		{
			Texture2D shadowTexture = tTexture2DTable[cShadowData.ShadowMapOffset + shadowIndex];
			attenuation *= shadowTexture.SampleCmpLevelZero(sShadowMapSampler, lightPos.xy, lightPos.z);
			castShadowRay = false;
		}
	}
#if SECONDARY_SHADOW_RAY
	if(castShadowRay)
	{
		attenuation *= CastShadowRay(worldPos, L);
#else
		attenuation = 0.0f;
#endif // SECONDARY_SHADOW_RAY
	}
	
	if(attenuation <= 0.0f)
	{
		return result;
	}

	result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, normalize(L), attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	return result;
}

[shader("closesthit")] 
void ReflectionClosestHit(inout ReflectionRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	payload.rayCone = PropagateRayCone(payload.rayCone, 0, RayTCurrent());

	VertexAttribute v = GetVertexAttributes(attrib.barycentrics, InstanceID(), PrimitiveIndex(), ObjectToWorld4x3());
	float mipLevel = 2;
	MaterialProperties material = GetMaterialProperties(v.Material, v.UV, mipLevel);
	BrdfData brdfData = GetBrdfData(material);

	float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	float3 V = normalize(-WorldRayDirection());
	float3 N = v.Normal;

	LightResult totalResult = (LightResult)0;
	for(int i = 0; i < cViewData.NumLights; ++i)
	{
		Light light = tLights[i];
		LightResult result = EvaluateLight(light, hitLocation, V, N, brdfData);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}
	payload.output += material.Emissive + totalResult.Diffuse + totalResult.Specular + ApplyAmbientLight(brdfData.Diffuse, 1.0f, 0.1f);
}

[shader("anyhit")]
void ReflectionAnyHit(inout ReflectionRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	VertexAttribute vertex = GetVertexAttributes(attrib.barycentrics, InstanceID(), PrimitiveIndex(), ObjectToWorld4x3());
	MaterialProperties material = GetMaterialProperties(vertex.Material, vertex.UV, 2);
	if(material.Opacity < 0.5)
	{
		IgnoreHit();
	}
}

[shader("miss")] 
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload) 
{
	payload.Hit = 0;
}

[shader("miss")] 
void ReflectionMiss(inout ReflectionRayPayload payload : SV_RayPayload) 
{
	payload.output = CIESky(WorldRayDirection(), -tLights[0].Direction);
}

[shader("raygeneration")] 
void RayGen() 
{
	float2 dimInv = rcp(DispatchRaysDimensions().xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 texCoord = (float2)launchIndex * dimInv;

	float depth = tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r;
	float4 colorSample = tPreviousSceneColor.SampleLevel(sDiffuseSampler, texCoord, 0);
	float4 reflectionSample = tSceneNormals.SampleLevel(sDiffuseSampler, texCoord, 0);

	float3 view = ViewFromDepth(texCoord, depth, cViewData.ProjectionInverse);
	float3 world = mul(float4(view, 1), cViewData.ViewInverse).xyz;
	
	float3 N = reflectionSample.rgb;
	float reflectivity = reflectionSample.a;

	if(depth > 0 && reflectivity > 0.0f)
	{
		float3 V = normalize(world - cViewData.ViewInverse[3].xyz);
		float3 R = reflect(V, N);
		ReflectionRayPayload payload = CastReflectionRay(world, R, depth);
		colorSample += reflectivity * float4(payload.output, 0);
	}
	uOutput[launchIndex] = colorSample;
}
