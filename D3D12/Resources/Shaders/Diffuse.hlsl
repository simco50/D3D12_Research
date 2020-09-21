#include "Common.hlsli"
#include "Lighting.hlsli"

#define BLOCK_SIZE 16

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t3, numDescriptors = 7), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t10, numDescriptors = 32, space = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
				"SRV(t500, visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s2, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc=COMPARISON_GREATER), " \

struct PerObjectData
{
	float4x4 World;
	float4x4 WorldViewProj;
};

struct PerViewData
{
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 Projection;
	float4x4 ProjectionInverse;
	float2 InvScreenDimensions;
	float NearZ;
	float FarZ;
	int FrameIndex;
#if CLUSTERED_FORWARD
    int3 ClusterDimensions;
    int2 ClusterSize;
	float2 LightGridParams;
#endif
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 positionWS : POSITION_WS;
	float3 positionVS : POSITION_VS;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t3);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t3);
#endif
StructuredBuffer<uint> tLightIndexList : register(t4);

#if CLUSTERED_FORWARD
uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cViewData.LightGridParams.x - cViewData.LightGridParams.y);
}
#endif

LightResult DoLight(float4 pos, float3 worldPos, float3 vPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint3 clusterIndex3D = uint3(floor(pos.xy / cViewData.ClusterSize), GetSliceFromDepth(vPos.z));
    uint clusterIndex1D = clusterIndex3D.x + (cViewData.ClusterDimensions.x * (clusterIndex3D.y + cViewData.ClusterDimensions.y * clusterIndex3D.z));
	uint startOffset = tLightGrid[clusterIndex1D].x;
	uint lightCount = tLightGrid[clusterIndex1D].y;
#endif
	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
		uint lightIndex = tLightIndexList[startOffset + i];
		Light light = tLights[lightIndex];
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, worldPos, vPos, N, V);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}
	return totalResult;
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
	PSInput result;
	result.position = mul(float4(input.position, 1.0f), cObjectData.WorldViewProj);
	result.positionWS = mul(float4(input.position, 1.0f), cObjectData.World).xyz;
	result.positionVS = mul(float4(result.positionWS, 1.0f), cViewData.View).xyz;
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cObjectData.World));
	result.tangent = normalize(mul(input.tangent, (float3x3)cObjectData.World));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cObjectData.World));
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 baseColor = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);
	float3 specular = 0.5f;
	float metalness = 0;
	float r = 0.5f;

	float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
	float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

	float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
	float3 N = TangentSpaceNormalMapping(tNormalTexture, sDiffuseSampler, TBN, input.texCoord, true);
	float3 V = normalize(cViewData.ViewInverse[3].xyz - input.positionWS);	
	float3 spec = 0;
#if 0

	float3 reflectionWs = normalize(reflect(-V, N));
	{
		RayDesc ray;
		ray.Origin = input.positionWS;
		ray.Direction = reflectionWs;
		ray.TMin = 0.001;
		ray.TMax = input.positionVS.z;

		RayQuery<RAY_FLAG_NONE> q;

		q.TraceRayInline(
			tAccelerationStructure,
			RAY_FLAG_NONE,
			~0,
			ray);
		q.Proceed();

		if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			float distance = q.CommittedRayT();
			float gradient = saturate((40 - distance) / 40);
			float3 worldHit = ray.Origin + ray.Direction * q.CommittedRayT();
			float4 proj = mul(float4(mul(float4(worldHit, 1), cViewData.View).xyz, 1), cViewData.Projection);
			proj.xyz /= proj.w;
			proj.x = (proj.x + 1) / 2.0f;
			proj.y = (1 - proj.y) / 2.0f;
			if(proj.z > 0 && proj.x > 0 && proj.x < 1 && proj.y > 0 && proj.y < 1)
			{
				spec = gradient * saturate(0.5f * tPrevColor.SampleLevel(sClampSampler, proj.xy, 0).xyz);
			}
		}
	}

#else
	float ssrMode = 1.0f;
	float roughnessThreshold = 0.1f;
	bool ssr = false;
	if (ssrMode > 0.0)
	{
		ssr = r > roughnessThreshold;
	}
	else
	{
		ssr = ssrMode > 0.0;
	}
	if (ssr)
	{
		//N = input.normal; //TEMP

		float VdovR_threshold = 0.2;
		float3 reflectionWs = normalize(reflect(-V, N));
		if (dot(V, reflectionWs) <= VdovR_threshold)
		{
			uint frameIndex = cViewData.FrameIndex;
			float jitter = InterleavedGradientNoise(input.position.xy, frameIndex);
			jitter -= 1.0;
			
			//jitter = 0; //TEMP

			uint max_steps = 16;


			float4 vsPosition = input.position;
			vsPosition.w = 1;
			vsPosition.xy *= cViewData.InvScreenDimensions;
			vsPosition.x = vsPosition.x * 2 - 1;
			vsPosition.y = (1 - vsPosition.y) * 2 - 1;
			vsPosition = mul(vsPosition, cViewData.ProjectionInverse);
			float3 ray_start_vs = vsPosition.xyz / vsPosition.w;

			float linearDepth = input.positionVS.z;
			float3 reflectionVs = mul(reflectionWs, (float3x3)cViewData.View);
			float3 ray_end_vs = ray_start_vs + (reflectionVs * linearDepth);

			float4 ray_start = mul(float4(ray_start_vs, 1), cViewData.Projection);
			ray_start.xyz /= ray_start.w;
			ray_start.xy = (ray_start.xy + 1) / 2;
			ray_start.y = 1 - ray_start.y;
			float4 ray_end = mul(float4(ray_end_vs, 1), cViewData.Projection);
			ray_end.xyz /= ray_end.w;
			ray_end.xy = (ray_end.xy + 1) / 2;
			ray_end.y = 1 - ray_end.y;

			float3 ray_step = ((ray_end - ray_start).xyz / float(max_steps));
			ray_step = ray_step / length(ray_end.xy - ray_start.xy);
			float3 ray_pos = ray_start.xyz + (ray_step * jitter);
			float z_thickness = abs(ray_step.z);

			uint hitIndex = 0;
			float3 bestHit = ray_pos;
			float prevSceneZ = ray_start.z;
			for (uint curr_step = 1; curr_step <= max_steps; curr_step++)
			{
				float2 texCoord = ray_pos.xy + ray_step.xy * curr_step;
				float scene_z = tDepth.SampleLevel(sClampSampler, texCoord, 0).x;
				float currentPosition = ray_pos.z + ray_step.z * curr_step;
				if (abs(currentPosition - z_thickness) < scene_z + z_thickness)
				{
					bestHit = ray_pos + (ray_step * float(curr_step));
					float z_after = scene_z - bestHit.z;
					float z_before = (prevSceneZ - bestHit.z) + ray_step.z;
					float weight = saturate(z_after / (z_after - z_before));
					float3 prev_ray_pos = bestHit - ray_step;
					bestHit = (prev_ray_pos * weight) + (bestHit * (1.0 - weight));
					hitIndex = curr_step;
					break;
				}
				prevSceneZ = scene_z;
			}

			float4 hit_color = 0;
			if (hitIndex > 0)
			{
				float4 tcReproj = float4(bestHit, 1);
				float2 dist = (tcReproj.xy * 2.0) - float2(1.0, 1.0);
				float edge_atten = (1.0 - (float(hitIndex) / float(max_steps))) * 4.0;
				edge_atten = saturate(edge_atten);
				edge_atten *= smoothstep(0.0, 0.5, saturate(1.0 - dot(dist, dist)));
				//edge_atten = 1;
				float3 reflectionResult = tPrevColor.SampleLevel(sClampSampler, tcReproj.xy, 0).xyz;
				hit_color = float4(reflectionResult, edge_atten);
			}
			float smoothness_mask = 1.0 - (r / (1 - roughnessThreshold));
			smoothness_mask = saturate(smoothness_mask);
			smoothness_mask = 1;
			float ssrWeight = (hit_color.w * smoothness_mask);
			spec = hit_color.xyz * ssrWeight;
		}
	}

#endif

	
	LightResult lighting = DoLight(input.position, input.positionWS, input.positionVS, N, V, diffuseColor, specularColor, r);
	float3 color = lighting.Diffuse + lighting.Specular + spec; 

	float ao = tAO.SampleLevel(sDiffuseSampler, (float2)input.position.xy * cViewData.InvScreenDimensions, 0).r;
	color += ApplyAmbientLight(diffuseColor, ao, tLights[0].GetColor().rgb * 0.1f);
	color += ApplyVolumetricLighting(cViewData.ViewInverse[3].xyz, input.positionWS.xyz, input.position.xyz, cViewData.View, tLights[0], 10);

	return float4(color, baseColor.a);
}