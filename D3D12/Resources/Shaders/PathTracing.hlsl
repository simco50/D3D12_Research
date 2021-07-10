#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"

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

struct VertexAttribute
{
	float2 UV;
	float3 Normal;
	int Material;
};

struct VertexInput
{
	uint2 Position;
	uint UV;
	float3 Normal;
	float4 Tangent;
};

struct ViewData
{
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 ProjectionInverse;
	float4x4 Projection;
	uint NumLights;
	float ViewPixelSpreadAngle;
	uint TLASIndex;
};

RWTexture2D<float4> uOutput : register(u0);
ConstantBuffer<ViewData> cViewData : register(b0);

struct RAYPAYLOAD PrimaryRayPayload
{
	float2 UV;
	float3 Position;
	float3 Normal;
	uint Material;
	uint Hit;
};

struct RAYPAYLOAD ShadowRayPayload
{
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

	ShadowRayPayload shadowRay;
	shadowRay.Hit = 0;

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
	return shadowRay.Hit;
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

LightResult EvaluateLight(Light light, float3 worldPos, float3 V, float3 N, SurfaceData shadingData)
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
		attenuation *= CastShadowRay(worldPos, L); 
	}

	L = normalize(L);
	result = DefaultLitBxDF(shadingData.Specular, shadingData.Roughness, shadingData.Diffuse, N, V, L, attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	return result;
}

VertexAttribute GetVertexAttributes(float3 barycentrics)
{
	MeshData mesh = tMeshes[InstanceID()];
	uint3 indices = tBufferTable[mesh.IndexBuffer].Load<uint3>(PrimitiveIndex() * sizeof(uint3));
	VertexAttribute outData;

	outData.UV = 0;
	outData.Normal = 0;
	outData.Material = mesh.Material;
	for(int i = 0; i < 3; ++i)
	{
		VertexInput vertex = tBufferTable[mesh.VertexBuffer].Load<VertexInput>(indices[i] * sizeof(VertexInput));
		outData.UV += UnpackHalf2(vertex.UV) * barycentrics[i];
		outData.Normal += vertex.Normal * barycentrics[i];
	}
	float4x3 worldMatrix = ObjectToWorld4x3();
	outData.Normal = normalize(mul(outData.Normal, (float3x3)worldMatrix));
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

[shader("miss")] 
void ShadowMS(inout ShadowRayPayload payload : SV_RayPayload) 
{
	payload.Hit = 1;
}

[shader("raygeneration")] 
void RayGen() 
{
	float2 pixel = float2(DispatchRaysIndex().xy);
	float2 resolution = float2(DispatchRaysDimensions().xy);
	pixel = (((pixel + 0.5f) / resolution) * 2.f - 1.f);
	Ray ray = GeneratePinholeCameraRay(pixel, cViewData.ViewInverse, cViewData.Projection);

	float3 output = 0;
	uint numBounces = 3;
	for(int i = 0; i < numBounces; ++i)
	{
		PrimaryRayPayload payload = (PrimaryRayPayload)0;

		RayDesc desc;
		desc.Origin = ray.Origin;
		desc.Direction = ray.Direction;
		desc.TMin = RAY_BIAS;
		desc.TMax = RAY_MAX_T;

		TraceRay(
			tTLASTable[cViewData.TLASIndex],		 			//AccelerationStructure
			0, 													//RayFlags
			0xFF, 												//InstanceInclusionMask
			0,													//RayContributionToHitGroupIndex
			0, 													//MultiplierForGeometryContributionToHitGroupIndex
			0, 													//MissShaderIndex
			desc, 												//Ray
			payload 											//Payload
		);

		if(!payload.Hit)
		{
			output += CIESky(ray.Direction, -tLights[0].Direction);
			break;
		}

		SurfaceData surface = GetShadingData(payload.Material, payload.UV, 0);
		for(int j = 0; j < cViewData.NumLights; ++j)
		{
			LightResult result = EvaluateLight(tLights[j], payload.Position, desc.Direction, payload.Normal, surface);
			output += result.Diffuse + result.Specular + surface.Emissive;
		}

		ray.Origin = payload.Position;
		ray.Direction = reflect(ray.Direction, payload.Normal);
	}

	uOutput[DispatchRaysIndex().xy] = float4(output, 1);
}
