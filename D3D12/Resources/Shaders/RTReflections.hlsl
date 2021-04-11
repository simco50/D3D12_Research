#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "SkyCommon.hlsli"
#include "RaytracingCommon.hlsli"

#define RAY_CONE_TEXTURE_LOD 1
#define SECONDARY_SHADOW_RAY 1

GlobalRootSignature GlobalRootSig =
{
	"CBV(b0, visibility=SHADER_VISIBILITY_ALL),"
	"CBV(b2, visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(SRV(t5, numDescriptors = 6), visibility=SHADER_VISIBILITY_ALL),"
	GLOBAL_BINDLESS_TABLE ", "
	"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL),"
	"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_ALL), " \
};


struct Vertex
{
	float3 position;
	float2 texCoord;
	float3 normal;
	float3 tangent;
	float3 bitangent;
};

struct ViewData
{
	float4x4 ViewInverse;
	float4x4 ProjectionInverse;
	uint NumLights;
	float ViewPixelSpreadAngle;
	uint TLASIndex;
};

struct HitData
{
	float4x4 WorldMatrix;
	int Diffuse;
	int Normal;
	int RoughnessMetalness;
	uint VertexBuffer;
	uint IndexBuffer;
};

RWTexture2D<float4> uOutput : register(u0);
ConstantBuffer<ViewData> cViewData : register(b0);
ConstantBuffer<HitData> cHitData : register(b1);

struct RayPayload
{
	float3 output;
	RayCone rayCone;
};

struct ShadowRayPayload
{
	uint hit;
};

ShadowRayPayload CastShadowRay(float3 origin, float3 direction)
{
	float len = length(direction);

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction / len;
	ray.TMin = 0.001f;
	ray.TMax = len;

	ShadowRayPayload shadowRay = (ShadowRayPayload)0;

	TraceRay(
		tTLASTable[cViewData.TLASIndex], 								//AccelerationStructure
		RAY_FLAG_FORCE_OPAQUE |
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,					 	//RayFlags
		0xFF, 															//InstanceInclusionMask
		0, 																//RayContributionToHitGroupIndex
		0, 																//MultiplierForGeometryContributionToHitGroupIndex
		1, 																//MissShaderIndex
		ray, 															//Ray
		shadowRay 														//Payload
	);
	return shadowRay;
}

RayPayload CastReflectionRay(float3 origin, float3 direction, float T)
{
	RayCone cone;
	cone.Width = 0;
	cone.SpreadAngle = cViewData.ViewPixelSpreadAngle;

	RayPayload payload;
	payload.rayCone = PropagateRayCone(cone, 0.0f, T);
	payload.output = 0.0f;

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = 0.001f;
	ray.TMax = 1000000.f;

	TraceRay(
		tTLASTable[cViewData.TLASIndex],		 						//AccelerationStructure
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE, 	//RayFlags
		0xFF, 															//InstanceInclusionMask
		0,																//RayContributionToHitGroupIndex
		0, 																//MultiplierForGeometryContributionToHitGroupIndex
		0, 																//MissShaderIndex
		ray, 															//Ray
		payload 														//Payload
	);

	return payload;
}

[shader("closesthit")] 
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	payload.rayCone = PropagateRayCone(payload.rayCone, 0, RayTCurrent());

	// Resolve geometry data
	uint3 indices = tBufferTable[cHitData.IndexBuffer].Load<uint3>(PrimitiveIndex() * sizeof(uint3));
	float3 b = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
	Vertex v0 = tBufferTable[cHitData.VertexBuffer].Load<Vertex>(indices.x * sizeof(Vertex));
	Vertex v1 = tBufferTable[cHitData.VertexBuffer].Load<Vertex>(indices.y * sizeof(Vertex));
	Vertex v2 = tBufferTable[cHitData.VertexBuffer].Load<Vertex>(indices.z * sizeof(Vertex));
	float2 texCoord = v0.texCoord * b.x + v1.texCoord * b.y + v2.texCoord * b.z;
	float3 N = mul(v0.normal * b.x + v1.normal * b.y + v2.normal * b.z, (float3x3)cHitData.WorldMatrix);
	float3 T = mul(v0.tangent * b.x + v1.tangent * b.y + v2.tangent * b.z, (float3x3)cHitData.WorldMatrix);
	float3 B = mul(v0.bitangent * b.x + v1.bitangent * b.y + v2.bitangent * b.z, (float3x3)cHitData.WorldMatrix);
	float3x3 TBN = float3x3(T, B, N);

#if RAY_CONE_TEXTURE_LOD
	float2 texcoords[3] = { v0.texCoord, v1.texCoord, v2.texCoord };
	float2 textureDimensions;
	tTexture2DTable[cHitData.Diffuse].GetDimensions(textureDimensions.x, textureDimensions.y);
	float mipLevel = ComputeRayConeMip(payload.rayCone, N, texcoords, textureDimensions);
#else
	float mipLevel = 2;
#endif //RAY_CONE_TEXTURE_LOD

	// Get material data
	float4 diffuseSample = tTexture2DTable[cHitData.Diffuse].SampleLevel(sDiffuseSampler, texCoord, mipLevel);
	float4 normalSample = tTexture2DTable[cHitData.Normal].SampleLevel(sDiffuseSampler, texCoord, mipLevel);
	float4 roughnessMetalnessSample = tTexture2DTable[cHitData.RoughnessMetalness].SampleLevel(sDiffuseSampler, texCoord, mipLevel);

	float4 baseColor = diffuseSample;
	float3 sampledNormal = normalSample.xyz;
	float metalness = roughnessMetalnessSample.b;
	float roughness = roughnessMetalnessSample.g;
	float3 specular = 0.5f;

	N = TangentSpaceNormalMapping(sampledNormal, TBN, false);

	float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
	float3 specularColor = ComputeF0(specular.r, diffuseColor, metalness);
	float3 wPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	float3 V = normalize(-WorldRayDirection());

	LightResult totalResult = (LightResult)0;
	for(int i = 0; i < cViewData.NumLights; ++i)
	{
		Light light = tLights[i];
		float attenuation = GetAttenuation(light, wPos);
		if(attenuation <= 0.0f)
		{
			continue;
		}
		
		float3 L = light.Position - wPos;
		if(light.Type == LIGHT_DIRECTIONAL)
		{
			L = 1000 * -light.Direction;
		}

		attenuation *= LightTextureMask(light, light.ShadowIndex, wPos);

#if SECONDARY_SHADOW_RAY
		ShadowRayPayload shadowRay = CastShadowRay(wPos, L);
		attenuation *= shadowRay.hit;
		if(attenuation <= 0.0f)
		{
			continue;
		}
#endif // SECONDARY_SHADOW_RAY

		LightResult result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);
		float4 color = light.GetColor();
		totalResult.Diffuse += result.Diffuse * color.rgb * light.Intensity;
		totalResult.Specular += result.Specular * color.rgb * light.Intensity;
	}
	payload.output += totalResult.Diffuse + totalResult.Specular + ApplyAmbientLight(diffuseColor, 1.0f, 0.1f);
}

[shader("miss")] 
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload) 
{
	payload.hit = 1;
}

[shader("miss")] 
void Miss(inout RayPayload payload : SV_RayPayload) 
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
	float3 view = ViewFromDepth(texCoord, depth, cViewData.ProjectionInverse);
	float3 world = mul(float4(view, 1), cViewData.ViewInverse).xyz;
	
	float4 colorSample = tPreviousSceneColor.SampleLevel(sDiffuseSampler, texCoord, 0);
	float4 reflectionSample = tSceneNormals.SampleLevel(sDiffuseSampler, texCoord, 0);
	float3 N = reflectionSample.rgb;
	float reflectivity = reflectionSample.a;

	if(reflectivity > 0.1f)
	{
		float3 V = normalize(world - cViewData.ViewInverse[3].xyz);
		float3 R = reflect(V, N);
		RayPayload payload = CastReflectionRay(world, R, depth);
		colorSample += float4(reflectivity * payload.output, 0);
	}
	uOutput[launchIndex] = colorSample;
}
