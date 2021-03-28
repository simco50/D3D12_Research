#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Lighting.hlsli"

#define RootSig \
		"CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
		"CBV(b2, visibility=SHADER_VISIBILITY_ALL), " \
		"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
		"DescriptorTable(SRV(t4, numDescriptors = 10), visibility=SHADER_VISIBILITY_ALL), " \
		GLOBAL_BINDLESS_TABLE ", " \
		"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP), " \
		"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
		"StaticSampler(s2, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc=COMPARISON_GREATER), " \

struct ShaderData
{
	int3 ClusterDimensions;
	int NoiseTexture;
	float3 InvClusterDimensions;
	int NumLights;
	float4x4 ViewProjectionInv;
	float4x4 ProjectionInv;
	float4x4 ViewInv;
	float NearZ;
	float FarZ;
	int FrameIndex;
	float Jitter;
	float4x4 ReprojectionMatrix;
};

ConstantBuffer<ShaderData> cData : register(b0);

Texture3D<float4> tLightScattering : register(t4);
RWTexture3D<float4> uOutLightScattering : register(u0);

float HenyeyGreenstreinPhase(float LoV, float G)
{
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}

float3 LineFromOriginZIntersection(float3 lineFromOrigin, float depth)
{
    float3 normal = float3(0.0f, 0.0f, 1.0f);
    float t = depth / dot(normal, lineFromOrigin);
    return t * lineFromOrigin;
}

float sdBox( float3 p, float3 b )
{
  float3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

[RootSignature(RootSig)]
[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	// Compute the sample point inside the froxel. Jittered
	float2 texelUV = (threadId.xy + 0.5f) * cData.InvClusterDimensions.xy;
	float minZ = (float)threadId.z * cData.InvClusterDimensions.z;
	float maxZ = (float)(threadId.z + 1) * cData.InvClusterDimensions.z;
	float minLinearZ = cData.FarZ + minZ * (cData.NearZ - cData.FarZ);
	float maxLinearZ = cData.FarZ + maxZ * (cData.NearZ - cData.FarZ);

	float z = lerp(minLinearZ, maxLinearZ, cData.Jitter);

	float3 viewRay = ViewFromDepth(texelUV, 1, cData.ProjectionInv);
	float3 vPos = LineFromOriginZIntersection(viewRay, z);
	float3 worldPosition = mul(float4(vPos, 1), cData.ViewInv).xyz;

	// Calculate Density based on the fog volumes
	float cellThickness = maxLinearZ - minLinearZ;
	float3 cellAbsorption = 0.0f;

	float volumeAttenuation = saturate(1 - sdBox(worldPosition, float3(200, 100, 200)));

	float3 lightScattering = 0.5f * volumeAttenuation;
	float cellDensity = 0.05f * volumeAttenuation;

	float3 V = normalize(cData.ViewInv[3].xyz - worldPosition);
	float4 pos = float4(texelUV, 0, z);

	float3 totalScattering = 0;

	// Iterate over all the lights and light the froxel
	for(int i = 0; i < cData.NumLights; ++i)
	{
		Light light = tLights[i];
		if(light.VolumetricLighting > 0)
		{
			float attenuation = GetAttenuation(light, worldPosition);
			if(attenuation <= 0.0f)
			{
				continue;
			}

			if(light.ShadowIndex >= 0)
			{
				int shadowIndex = GetShadowIndex(light, pos, worldPosition);
				attenuation *= ShadowNoPCF(worldPosition, shadowIndex, light.InvShadowSize);
				if(light.LightTexture >= 0)
				{
					float4 lightPos = mul(float4(worldPosition, 1), cShadowData.LightViewProjections[shadowIndex]);
					lightPos.xyz /= lightPos.w;
					lightPos.xy = (lightPos.xy + 1) / 2;
					float mask = tTexture2DTable[light.LightTexture].SampleLevel(sClampSampler, lightPos.xy, 0).r;
					attenuation *= mask;
				}
			}

			float3 L = normalize(light.Position - worldPosition);
			if(light.Type == LIGHT_DIRECTIONAL)
			{
				L = -normalize(light.Direction);
			}
			float VdotL = dot(V, L);
			float4 lightColor = light.GetColor() * light.Intensity;

			totalScattering += attenuation * lightColor.xyz * HenyeyGreenstreinPhase(-VdotL, 0.5);
		}
	}

	//totalScattering += ApplyAmbientLight(1, 1, tLights[0].GetColor().rgb * 0.001f);

	float4 prevScattering = tLightScattering[threadId];
	float4 newScattering = float4(lightScattering * totalScattering, cellDensity);

	uOutLightScattering[threadId] = lerp(prevScattering, newScattering, 0.05);
}

groupshared uint gsMaxDepth;

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void AccumulateFogCS(uint3 threadId : SV_DISPATCHTHREADID, uint groupIndex : SV_GROUPINDEX)
{
	float2 texCoord = threadId.xy * cData.InvClusterDimensions.xy;
	float depth = tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r;

	if(groupIndex == 0)
	{
		gsMaxDepth = 0xffffffff;
	}

    GroupMemoryBarrierWithGroupSync();

	InterlockedMin(gsMaxDepth, asuint(depth));

    GroupMemoryBarrierWithGroupSync();

	float maxDepth = asfloat(gsMaxDepth);

	uint lastSlice = (1 - maxDepth) * cData.ClusterDimensions.z;

	float3 lightScattering = 0;
	float cellDensity = 0;
	float transmittance = 1;

	uOutLightScattering[int3(threadId.xy, 0)] = float4(lightScattering, transmittance);

	for(int sliceIndex = 1; sliceIndex <= lastSlice; ++sliceIndex)
	{
		float4 scatteringDensity = tLightScattering[int3(threadId.xy, sliceIndex - 1)];
		lightScattering += scatteringDensity.xyz * transmittance;
		cellDensity += scatteringDensity.w;
		transmittance = saturate(exp(-cellDensity));
		uOutLightScattering[int3(threadId.xy, sliceIndex)] = float4(lightScattering, transmittance);
	}
}
