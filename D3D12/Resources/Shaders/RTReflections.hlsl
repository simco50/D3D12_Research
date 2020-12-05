#include "Common.hlsli"
#include "ShadingModels.hlsli"

RWTexture2D<float4> gOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tDepth : register(t1);

StructuredBuffer<Light> tLights : register(t2);

SamplerState sSceneSampler : register(s0);

struct Vertex
{
	float3 position;
	float2 texCoord;
	float3 normal;
	float3 tangent;
	float3 bitangent;
};

ByteAddressBuffer tVertexData : register(t100);
ByteAddressBuffer tIndexData : register(t101);
cbuffer HitData : register(b1)
{
	int DiffuseIndex;
	int NormalIndex;
	int RoughnessIndex;
	int MetallicIndex;
}

Texture2D tMaterialTextures[] : register(t200);

cbuffer ShaderParameters : register(b0)
{
	float4x4 cViewInverse;
	float4x4 cViewProjectionInverse;
}

struct RayPayload
{
	float3 output;
};

float3 TangentSpaceNormalMapping(float3 sampledNormal, float3x3 TBN, float2 tex, bool invertY)
{
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;

//#define NORMAL_BC5
#ifdef NORMAL_BC5
	sampledNormal.z = sqrt(saturate(1.0f - dot(sampledNormal.xy, sampledNormal.xy)));
#endif
	if(invertY)
	{
		sampledNormal.x = -sampledNormal.x;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, TBN);
}

[shader("closesthit")] 
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	float3 b = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
	uint3 indices = tIndexData.Load3(PrimitiveIndex() * sizeof(uint3));
	Vertex v0 = tVertexData.Load<Vertex>(indices.x * sizeof(Vertex));
	Vertex v1 = tVertexData.Load<Vertex>(indices.y * sizeof(Vertex));
	Vertex v2 = tVertexData.Load<Vertex>(indices.z * sizeof(Vertex));
	float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;
	float3 N = v0.normal * b.x + v1.normal * b.y + v2.normal * b.z;
	float3 T = v0.tangent * b.x + v1.tangent * b.y + v2.tangent * b.z;
	float3 B = v0.bitangent * b.x + v1.bitangent * b.y + v2.bitangent * b.z;
	float3x3 TBN = float3x3(T, B, N);
	float3 wPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	float3 V = normalize(wPos - cViewInverse[3].xyz);
	float attenuation = 1;

	Light light = tLights[0];

	float3 L = light.Direction;

	float3 diffuse = tMaterialTextures[DiffuseIndex].SampleLevel(sSceneSampler, texCoord, 0).rgb;
	float3 sampledNormal = tMaterialTextures[NormalIndex].SampleLevel(sSceneSampler, texCoord, 0).rgb;
	N = TangentSpaceNormalMapping(sampledNormal, TBN, texCoord, false);

	float roughness = 0.5;
	float3 specularColor = ComputeF0(0.5f, diffuse, 0);

	LightResult result = DefaultLitBxDF(specularColor, roughness, diffuse, N, V, L, attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	payload.output = result.Diffuse + result.Specular;
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
	uint launchIndex1d = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
	float2 texCoord = (float2)launchIndex * dimInv;

	float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sSceneSampler, texCoord, 0).r, cViewProjectionInverse);
    float3 N = NormalFromDepth(tDepth, sSceneSampler, texCoord, dimInv, cViewProjectionInverse);

	float3 V = normalize(world - cViewInverse[3].xyz);
	float3 R = reflect(V, N);

	RayDesc ray;
	ray.Origin = world + 0.001f * R;
	ray.Direction = R;
	ray.TMin = 0.0f;
	ray.TMax = 10000;

	// Trace the ray
	TraceRay(
		//AccelerationStructure
		SceneBVH,

		//RayFlags
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
		RAY_FLAG_FORCE_OPAQUE,

		//InstanceInclusionMask
		// Instance inclusion mask, which can be used to mask out some geometry to this ray by
		// and-ing the mask with a geometry mask. The 0xFF flag then indicates no geometry will be
		// masked
		0xFF,

		//RayContributionToHitGroupIndex
		// Depending on the type of ray, a given object can have several hit groups attached
		// (ie. what to do when hitting to compute regular shading, and what to do when hitting
		// to compute shadows). Those hit groups are specified sequentially in the SBT, so the value
		// below indicates which offset (on 4 bits) to apply to the hit groups for this ray.
		0,

		//MultiplierForGeometryContributionToHitGroupIndex
		// The offsets in the SBT can be computed from the object ID, its instance ID, but also simply
		// by the order the objects have been pushed in the acceleration structure. This allows the
		// application to group shaders in the SBT in the same order as they are added in the AS, in
		// which case the value below represents the stride (4 bits representing the number of hit
		// groups) between two consecutive objects.
		1,

		//MissShaderIndex
		// Index of the miss shader to use in case several consecutive miss shaders are present in the
		// SBT. This allows to change the behavior of the program when no geometry have been hit, for
		// example one to return a sky color for regular rendering, and another returning a full
		// visibility value for shadow rays.
		0,

		//Ray
		ray,

		//Payload
		payload);

	gOutput[launchIndex] = float4(payload.output, 1);
}
