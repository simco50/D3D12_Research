#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

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
	float4 tangent;
};

struct VertexInput
{
	uint2 position;
	uint texCoord;
	float3 normal;
	float4 tangent;
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
	float3 output 		RAYQUALIFIER(read(caller, closesthit) : write(caller, closesthit, miss));
};

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
	for(int i = 0; i < 3; ++i)
	{
		VertexInput v = tBufferTable[mesh.VertexBuffer].Load<VertexInput>(indices[i] * sizeof(VertexInput));
		vertexOut.position += UnpackHalf3(v.position) * barycentrics[i];
		vertexOut.texCoord += UnpackHalf2(v.texCoord) * barycentrics[i];
		vertexOut.normal += v.normal * barycentrics[i];
		vertexOut.tangent += v.tangent * barycentrics[i];
	}
	float4x3 worldMatrix = ObjectToWorld4x3();
	vertexOut.position = mul(float4(vertexOut.position, 1), worldMatrix).xyz;
	vertexOut.normal = normalize(mul(vertexOut.normal, (float3x3)worldMatrix));
	vertexOut.tangent.xyz = normalize(mul(vertexOut.tangent.xyz, (float3x3)worldMatrix));
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
		float3 B = cross(v.normal, v.tangent.xyz) * v.tangent.w;
		float3x3 TBN = float3x3(v.tangent.xyz, B, v.normal);
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
void PrimaryCHS(inout PrimaryRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
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

[shader("miss")] 
void PrimaryMS(inout PrimaryRayPayload payload : SV_RayPayload) 
{
	payload.output = CIESky(WorldRayDirection(), -tLights[0].Direction);
}

[shader("raygeneration")] 
void RayGen() 
{
	float2 pixel = float2(DispatchRaysIndex().xy);
	float2 resolution = float2(DispatchRaysDimensions().xy);
	pixel = (((pixel + 0.5f) / resolution) * 2.f - 1.f);
	RayDesc ray = GeneratePinholeCameraRay(pixel, cViewData.ViewInverse, cViewData.Projection);

	PrimaryRayPayload payload;
	payload.output = 0.0f;
	
	TraceRay(
		tTLASTable[cViewData.TLASIndex],		 			//AccelerationStructure
		0, 													//RayFlags
		0xFF, 												//InstanceInclusionMask
		0,													//RayContributionToHitGroupIndex
		0, 													//MultiplierForGeometryContributionToHitGroupIndex
		0, 													//MissShaderIndex
		ray, 												//Ray
		payload 											//Payload
	);

	uOutput[DispatchRaysIndex().xy] = float4(payload.output, 1);
}
