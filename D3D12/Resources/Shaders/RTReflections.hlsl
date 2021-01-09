#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"

GlobalRootSignature GlobalRootSig =
{
	"CBV(b0, visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(SRV(t5, numDescriptors = 6), visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(SRV(t500, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(SRV(t1000, numDescriptors = 128, space = 2), visibility=SHADER_VISIBILITY_ALL),"
	"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL),"
};

RWTexture2D<float4> gOutput : register(u0);

struct Vertex
{
	float3 position;
	float2 texCoord;
	float3 normal;
	float3 tangent;
	float3 bitangent;
};

cbuffer ShaderParameters : register(b0)
{
	float4x4 cViewInverse;
	float4x4 cViewProjectionInverse;
	uint cNumLights;
}

cbuffer HitData : register(b1)
{
	int DiffuseIndex;
	int NormalIndex;
	int RoughnessIndex;
	int MetallicIndex;

	uint VertexBufferOffset;
	uint IndexBufferOffset;
}

struct RayPayload
{
	float3 output;
};

struct ShadowRayPayload
{
	uint hit;
};

[shader("closesthit")] 
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	float3 b = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
	uint3 indices = tGeometryData.Load3(IndexBufferOffset + PrimitiveIndex() * sizeof(uint3));
	Vertex v0 = tGeometryData.Load<Vertex>(VertexBufferOffset + indices.x * sizeof(Vertex));
	Vertex v1 = tGeometryData.Load<Vertex>(VertexBufferOffset + indices.y * sizeof(Vertex));
	Vertex v2 = tGeometryData.Load<Vertex>(VertexBufferOffset + indices.z * sizeof(Vertex));
	float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;
	float3 N = v0.normal * b.x + v1.normal * b.y + v2.normal * b.z;
	float3 T = v0.tangent * b.x + v1.tangent * b.y + v2.tangent * b.z;
	float3 B = v0.bitangent * b.x + v1.bitangent * b.y + v2.bitangent * b.z;
	float3x3 TBN = float3x3(T, B, N);
	float3 wPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	float rayDistanceScale = RayTCurrent() / (1000.0f - RayTMin());
	float cosAngle = 1.0f - dot(WorldRayDirection(), N);
	int mipLevel = (cosAngle / 2.0f + rayDistanceScale / 2.0f) * 6.0f;

	float3 V = normalize(wPos - cViewInverse[3].xyz);
	float3 diffuse = tMaterialTextures[DiffuseIndex].SampleLevel(sDiffuseSampler, texCoord, mipLevel).rgb;
	float3 sampledNormal = tMaterialTextures[NormalIndex].SampleLevel(sDiffuseSampler, texCoord, mipLevel).rgb;
	N = TangentSpaceNormalMapping(sampledNormal, TBN, false);
	float roughness = 0.5;
	float3 specularColor = ComputeF0(0.5f, diffuse, 0);

	LightResult totalResult = (LightResult)0;
	for(int i = 0; i < cNumLights; ++i)
	{
		Light light = tLights[i];
		float attenuation = GetAttenuation(light, wPos);
		if(attenuation <= 0.0f)
		{
			continue;
		}
		
		float3 L = normalize(light.Position - wPos);
		if(light.Type == LIGHT_DIRECTIONAL)
		{
			L = -light.Direction;
		}

#define SHADOW_RAY 1
#if SHADOW_RAY
		RayDesc ray;
		ray.Origin = wPos + L * 0.001f;
		ray.Direction = L;
		ray.TMin = 0.0f;
		ray.TMax = length(wPos - light.Position);

		ShadowRayPayload shadowRay = (ShadowRayPayload)0;
		// Trace the ray
		TraceRay(
			tAccelerationStructure, 										//AccelerationStructure
			RAY_FLAG_FORCE_OPAQUE |
			RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,					 	//RayFlags
			0xFF, 															//InstanceInclusionMask
			1, 																//RayContributionToHitGroupIndex
			2, 																//MultiplierForGeometryContributionToHitGroupIndex
			1, 																//MissShaderIndex
			ray, 															//Ray
			shadowRay 														//Payload
		);
		attenuation *= shadowRay.hit;
#endif // SHADOW_RAY

		LightResult result = DefaultLitBxDF(specularColor, roughness, diffuse, N, V, L, attenuation);
		float4 color = light.GetColor();
		totalResult.Diffuse += result.Diffuse * color.rgb * light.Intensity;
		totalResult.Specular *= result.Specular * color.rgb * light.Intensity;
	}
	payload.output = totalResult.Diffuse + totalResult.Specular + ApplyAmbientLight(diffuse, 1.0f, 0.1f);
}

[shader("closesthit")] 
void ShadowClosestHit(inout ShadowRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	payload.hit = 0;
}

[shader("miss")] 
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload) 
{
	payload.hit = 1;
}

[shader("miss")] 
void Miss(inout RayPayload payload : SV_RayPayload) 
{
	payload.output = 0;
}

[shader("raygeneration")] 
void RayGen() 
{
	RayPayload payload = (RayPayload)0;

	float2 dimInv = rcp(DispatchRaysDimensions().xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 texCoord = (float2)launchIndex * dimInv;

	float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r, cViewProjectionInverse);
    float4 reflectionSample = tSceneNormals.SampleLevel(sDiffuseSampler, texCoord, 0);
	float3 N = reflectionSample.rgb;
	float reflectivity = reflectionSample.a;
	if(reflectivity > 0)
	{
		float3 V = normalize(world - cViewInverse[3].xyz);
		float3 R = reflect(V, N);

		RayDesc ray;
		ray.Origin = world + 0.001f * R;
		ray.Direction = R;
		ray.TMin = 0.0f;
		ray.TMax = 10000;

		// Trace the ray
		TraceRay(
			tAccelerationStructure, 														//AccelerationStructure
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE, 	//RayFlags
			0xFF, 															//InstanceInclusionMask
			0,																//RayContributionToHitGroupIndex
			2, 																//MultiplierForGeometryContributionToHitGroupIndex
			0, 																//MissShaderIndex
			ray, 															//Ray
			payload 														//Payload
		);
	}
	float4 colorSample = tPreviousSceneColor.SampleLevel(sDiffuseSampler, texCoord, 0);
	gOutput[launchIndex] = colorSample + float4(reflectivity * payload.output, 0);
}
