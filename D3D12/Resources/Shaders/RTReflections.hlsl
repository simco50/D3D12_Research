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
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 ProjectionInverse;
	uint NumLights;
	float ViewPixelSpreadAngle;
	uint TLASIndex;
};

RWTexture2D<float4> uOutput : register(u0);
ConstantBuffer<ViewData> cViewData : register(b0);

struct RAYPAYLOAD ReflectionRayPayload
{
	float3 output RAYQUALIFIER(read(caller) : write(caller, closesthit, miss));
	RayCone rayCone RAYQUALIFIER(read(caller, closesthit) : write(caller, closesthit));
};

struct RAYPAYLOAD ShadowRayPayload
{
	float hit RAYQUALIFIER(read(caller) : write(caller, miss));
};

float CastShadowRay(float3 origin, float3 direction)
{
	float len = length(direction);
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction / len;
	ray.TMin = RAY_BIAS;
	ray.TMax = len;

	ShadowRayPayload shadowRay;
	shadowRay.hit = 0.0f;

	TraceRay(
		tTLASTable[cViewData.TLASIndex], 						//AccelerationStructure
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |						//RayFlags
			RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
		0xFF, 													//InstanceInclusionMask
		0, 														//RayContributionToHitGroupIndex
		0, 														//MultiplierForGeometryContributionToHitGroupIndex
		1, 														//MissShaderIndex
		ray, 													//Ray
		shadowRay 												//Payload
	);
	return shadowRay.hit;
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
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 				//RayFlags
		0xFF, 												//InstanceInclusionMask
		0,													//RayContributionToHitGroupIndex
		0, 													//MultiplierForGeometryContributionToHitGroupIndex
		0, 													//MissShaderIndex
		ray, 												//Ray
		payload 											//Payload
	);

	return payload;
}

struct ShadingData
{
	float3 WorldPos;
	float3 V;
	float3 N;
	float2 UV;

	float Opacity;
	float3 Diffuse;
	float3 Specular;
	float Roughness;
	float3 Emissive;
};

Vertex GetVertexAttributes(float3 barycentrics)
{
	MeshData mesh = tMeshes[InstanceID()];
	uint3 indices = tBufferTable[mesh.IndexBuffer].Load<uint3>(PrimitiveIndex() * sizeof(uint3));
	Vertex vertexOut;
	vertexOut.position = 0;
	vertexOut.texCoord = 0;
	vertexOut.normal = 0;
	vertexOut.tangent = 0;
	vertexOut.bitangent = 0;
	for(int i = 0; i < 3; ++i)
	{
		Vertex v = tBufferTable[mesh.VertexBuffer].Load<Vertex>(indices[i] * sizeof(Vertex));
		vertexOut.position += v.position * barycentrics[i];
		vertexOut.texCoord += v.texCoord * barycentrics[i];
		vertexOut.normal += v.normal * barycentrics[i];
		vertexOut.tangent += v.tangent * barycentrics[i];
		vertexOut.bitangent += v.bitangent * barycentrics[i];
	}
	float4x3 worldMatrix = ObjectToWorld4x3();
	vertexOut.position = mul(float4(vertexOut.position, 1), worldMatrix).xyz;
	vertexOut.normal = normalize(mul(vertexOut.normal, (float3x3)worldMatrix));
	vertexOut.tangent = normalize(mul(vertexOut.tangent, (float3x3)worldMatrix));
	vertexOut.bitangent = normalize(mul(vertexOut.bitangent, (float3x3)worldMatrix));
	return vertexOut;
}

ShadingData GetShadingData(BuiltInTriangleIntersectionAttributes attrib, float3 cameraLocation, float mipLevel)
{
	MeshData mesh = tMeshes[InstanceID()];
	float3 barycentrics = float3((1.0f - attrib.barycentrics.x - attrib.barycentrics.y), attrib.barycentrics.x, attrib.barycentrics.y);
	Vertex v = GetVertexAttributes(barycentrics);

// Surface Shader BEGIN
	MaterialData material = tMaterials[mesh.Material];
	float4 baseColor = material.BaseColorFactor;
	if(material.Diffuse >= 0)
	{
		baseColor *= tTexture2DTable[material.Diffuse].SampleLevel(sDiffuseSampler, v.texCoord, mipLevel);
	}
	float metalness = material.MetalnessFactor;
	float roughness = material.RoughnessFactor;
	if(material.RoughnessMetalness >= 0)
	{
		float4 roughnessMetalnessSample = tTexture2DTable[material.RoughnessMetalness].SampleLevel(sDiffuseSampler, v.texCoord, mipLevel);
	 	metalness *= roughnessMetalnessSample.b;
	 	roughness *= roughnessMetalnessSample.g;
	}
	
	float3 emissive = material.EmissiveFactor.rgb;
	if(material.Emissive >= 0)
	{
		emissive *= tTexture2DTable[material.Emissive].SampleLevel(sDiffuseSampler, v.texCoord, mipLevel).rgb;
	}
	float specular = 0.5f;

	float3 N = v.normal;
	if(material.Normal >= 0)
	{
		float4 normalSample = tTexture2DTable[material.Normal].SampleLevel(sDiffuseSampler, v.texCoord, mipLevel);
		float3x3 TBN = float3x3(v.tangent, v.bitangent, v.normal);
		N = TangentSpaceNormalMapping(normalSample.xyz, TBN, false);
	}
// Surface Shader END

	ShadingData outData = (ShadingData)0;
	outData.WorldPos = v.position;
	outData.V = -WorldRayDirection();
	outData.N = N;
	outData.UV = v.texCoord;
	outData.Diffuse = baseColor.rgb;
	outData.Specular = ComputeF0(specular, baseColor.rgb, metalness);
	outData.Roughness = roughness;
	outData.Emissive = emissive;
	outData.Opacity = baseColor.a;
	return outData;
}

LightResult EvaluateLight(Light light, ShadingData shadingData)
{
	LightResult result = (LightResult)0;
	float attenuation = GetAttenuation(light, shadingData.WorldPos);
	if(attenuation <= 0.0f)
	{
		return result;
	}
	
	float3 L = light.Position - shadingData.WorldPos;
	if(light.IsDirectional())
	{
		L = RAY_MAX_T * -light.Direction;
	}

	float3 viewPosition = mul(float4(shadingData.WorldPos, 1), cViewData.View).xyz;
	float4 pos = float4(0, 0, 0, viewPosition.z);
	int shadowIndex = GetShadowIndex(light, pos, shadingData.WorldPos);
	float4x4 lightViewProjection = cShadowData.LightViewProjections[shadowIndex];
	float4 lightPos = mul(float4(shadingData.WorldPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;
	attenuation *= LightTextureMask(light, shadowIndex, shadingData.WorldPos);

	if(all(lightPos >= 0) && all(lightPos <= 1))
	{
		Texture2D shadowTexture = tTexture2DTable[cShadowData.ShadowMapOffset + shadowIndex];
		attenuation *= shadowTexture.SampleCmpLevelZero(sShadowMapSampler, lightPos.xy, lightPos.z);
	}
	else
	{
#if SECONDARY_SHADOW_RAY
		attenuation *= CastShadowRay(shadingData.WorldPos, L);
#else
		attenuation = 0.0f;
#endif // SECONDARY_SHADOW_RAY
	}
	if(attenuation <= 0.0f)
	{
		return result;
	}

	result = DefaultLitBxDF(shadingData.Specular, shadingData.Roughness, shadingData.Diffuse, shadingData.N, shadingData.V, L, attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	return result;
}

[shader("closesthit")] 
void ReflectionClosestHit(inout ReflectionRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	payload.rayCone = PropagateRayCone(payload.rayCone, 0, RayTCurrent());

	float mipLevel = 2;
	ShadingData shadingData = GetShadingData(attrib, WorldRayOrigin(), mipLevel);

	LightResult totalResult = (LightResult)0;
	for(int i = 0; i < cViewData.NumLights; ++i)
	{
		Light light = tLights[i];
		LightResult result = EvaluateLight(light, shadingData);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}
	payload.output += shadingData.Emissive + totalResult.Diffuse + totalResult.Specular + ApplyAmbientLight(shadingData.Diffuse, 1.0f, 0.1f);
}

[shader("anyhit")]
void ReflectionAnyHit(inout ReflectionRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	ShadingData shadingData = GetShadingData(attrib, WorldRayOrigin(), 2);
	if(shadingData.Opacity < 0.5)
	{
		IgnoreHit();
	}
}

[shader("miss")] 
void ShadowMiss(inout ShadowRayPayload payload : SV_RayPayload) 
{
	payload.hit = 1;
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
