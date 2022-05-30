#include "Common.hlsli"
#include "Random.hlsli"
#include "Volumetrics.hlsli"
#include "SkyCommon.hlsli"

RWTexture2D<float4> uOutput : register(u0);
Texture2D tSceneTexture : register(t0);
Texture2D tDepthTexture : register(t1);
Texture2D tCloudTypeDensityLUT : register(t2);
Texture3D tShapeNoise : register(t3);
Texture3D tDetailNoise : register(t4);

struct PassParameters
{
	float ShapeNoiseScale;
	float DetailNoiseScale;
	float CloudDensity;
	float RayStepSize;
	uint LightMarchSteps;
	float PlanetRadius;
	float AtmosphereHeightStart;
	float AtmosphereHeightEnd;
	float DetailNoiseInfluence;
	float CloudType;
};

ConstantBuffer<PassParameters> cPass : register(b0);

float4 GetHeightGradient(float cloudType)
{
	const float4 CloudGradient1 = float4(0.0, 0.07, 0.08, 0.15);
	const float4 CloudGradient2 = float4(0.0, 0.2, 0.42, 0.6);
	const float4 CloudGradient3 = float4(0.0, 0.08, 0.75, 0.98);

	float a = 1.0 - saturate(cloudType * 2.0);
	float b = 1.0 - abs(cloudType - 0.5) * 2.0;
	float c = saturate(cloudType - 0.5) * 2.0;

	return CloudGradient1 * a + CloudGradient2 * b + CloudGradient3 * c;
}

float SampleDensity(float3 position, uint mipLevel)
{
	float height = length(position - float3(0, -cPass.PlanetRadius, 0));
	float heightGradient = saturate(InverseLerp(height - cPass.PlanetRadius, cPass.AtmosphereHeightStart, cPass.AtmosphereHeightEnd));

	const float globalScale = 0.001f;

	const float3 windDirection = float3(0, 0, -1);
	const float cloudSpeed = 0.03f;
	const float cloudTopOffset = 10.0f;

	position += heightGradient * windDirection * cloudTopOffset;
	position += (windDirection + float3(0, 0.1f, 0)) * cView.FrameIndex * cloudSpeed;

	position *= globalScale;

	float4 lowFrequencies = tShapeNoise.SampleLevel(sLinearWrap, position * cPass.ShapeNoiseScale, mipLevel);
	float lowFrequencyFBM = dot(lowFrequencies.yzw, float3(0.625f, 0.25f, 0.125f));

	float baseCloud = saturate(Remap(lowFrequencies.r, (1.0f - lowFrequencyFBM), 1.0f, 0.0f, 1.0f));

	float coverage = cPass.CloudDensity;
	baseCloud = Remap(baseCloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f);
	baseCloud *= coverage;

	float verticalDensity = tCloudTypeDensityLUT.SampleLevel(sLinearClamp, float2(cPass.CloudType, heightGradient), 0).x;
	baseCloud *= verticalDensity;

	float4 highFrequencies = tDetailNoise.SampleLevel(sLinearWrap, position * cPass.DetailNoiseScale, mipLevel);
	float highFrequencyFBM = dot(highFrequencies.xyz, float3(0.625f, 0.25f, 0.125f));

	float highFrequencyNoise = lerp(highFrequencyFBM, 1 - highFrequencyFBM, saturate(heightGradient * 10));

	float finalCloud = Remap(baseCloud, highFrequencyNoise * cPass.DetailNoiseInfluence, 1.0f, 0.0f, 1.0f);

	return saturate(finalCloud * 0.1f);
}

float LightMarch(float3 rayOrigin, float3 rayDirection)
{
	const float coneSize = 300.0f;
	float stepSize = coneSize / cPass.LightMarchSteps;

	float totalDensity = 0;

	uint seed = SeedThread(0);
	for(uint i = 0; i < cPass.LightMarchSteps; ++i)
	{
		float3 rnd = float3(Random01(seed), Random01(seed), Random01(seed)) / 5.0f;
		rayOrigin += rayDirection * stepSize + (stepSize * rnd * (i + 1));
		totalDensity += max(0, SampleDensity(rayOrigin, 0) * stepSize);
	}

	rayOrigin += rayDirection * coneSize * 2;
	totalDensity += max(0, SampleDensity(rayOrigin, 0) * stepSize);

	float transmittance = exp(-totalDensity);
	return transmittance;
}

float4 RenderClouds(float2 UV, float3 rayOrigin, float3 rayDirection, float sceneDepth)
{
	float2 pixel = UV * cView.ViewportDimensions;

#define DEBUG_CLOUDS 1
#if DEBUG_CLOUDS
	float2 debugUV = InverseLerp(pixel, float2(20.0f, 20.0f), float2(500.0f, 500.0f));
	if(all(debugUV >= 0.0f) && all(debugUV <= 1.0f))
	{
		debugUV = (debugUV * 2.0f - 1.0f) * 1000.0f;
		float height = lerp(cPass.AtmosphereHeightStart, cPass.AtmosphereHeightEnd, 0.1f);
		float3 pos = float3(debugUV.x, height, debugUV.y);
		return SampleDensity(pos, 0);
	}
#endif

	float4 color = tSceneTexture.Sample(sLinearClamp, UV);
	float maxDepth = 10000000; //sceneDepth;
	if(maxDepth < cView.NearZ)
		return color;

	Light light = GetLight(0);
	
	float2 planetHit;
	RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPass.PlanetRadius, 0), cPass.PlanetRadius, planetHit);
	if(any(planetHit > -1))
	{
		color = 0;
		maxDepth = length(rayDirection * planetHit.x);
	}

	float minT = -1000000.0f;
	float maxT = -1000000.0f;

	float2 atmosphereHitTop;
	if(RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPass.PlanetRadius, 0), cPass.PlanetRadius + cPass.AtmosphereHeightEnd, atmosphereHitTop))
	{
		float2 atmosphereHitBottom;
		if(RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPass.PlanetRadius, 0), cPass.PlanetRadius + cPass.AtmosphereHeightStart, atmosphereHitBottom))
		{
			// If we see both intersection in front of us, keep the min/closest, otherwise the max/furthest
			float TempTop = all(atmosphereHitTop > 0.0f) ? min(atmosphereHitTop.x, atmosphereHitTop.y) : max(atmosphereHitTop.x, atmosphereHitTop.y);
			float TempBottom = all(atmosphereHitBottom > 0.0f) ? min(atmosphereHitBottom.x, atmosphereHitBottom.y) : max(atmosphereHitBottom.x, atmosphereHitBottom.y);

			if (all(atmosphereHitBottom > 0.0f))
			{
				// But if we can see the bottom of the layer, make sure we use the camera or the highest top layer intersection
				TempTop = max(0.0f, min(atmosphereHitTop.x, atmosphereHitTop.y));
			}

			minT = min(TempBottom, TempTop);
			maxT = max(TempBottom, TempTop);
		}
		else
		{
			minT = atmosphereHitTop.x;
			maxT = atmosphereHitTop.y;
		}
	}
	else
	{
		return color;
	}

	minT = max(0.0f, minT);
	maxT = min(maxDepth, max(0.0f, maxT));
	if(minT >= maxT)
	{
		return color;
	}
	const float stepSize = cPass.RayStepSize;

	float raymarchDistance = maxT - minT;

	float offset = InterleavedGradientNoise(pixel + cView.FrameIndex);
	rayOrigin += rayDirection * offset * stepSize;

	float distanceTravelled = 0;

	 // Phase function makes clouds brighter around sun
    float cosAngle = dot(rayDirection, -light.Direction);
	const float forwardScattering = 0.3f;
    const float backScattering = -0.7f;
	float phaseVal = lerp(HenyeyGreenstreinPhase(cosAngle, forwardScattering), HenyeyGreenstreinPhase(cosAngle, backScattering), 0.5f);

	float3 totalLight = 0;
	float transmittance = 1;

	while (distanceTravelled < raymarchDistance)
	{
		float3 rayPos = rayOrigin + rayDirection * (minT + distanceTravelled);
		float density = SampleDensity(rayPos, 0);

		if(density > 0)
		{
			float lightTransmittance = LightMarch(rayPos, -light.Direction) * phaseVal;

			totalLight += density * stepSize * transmittance * lightTransmittance;
			transmittance *= exp(-density * stepSize);
			if(transmittance < 0.01f)
			{
				break;
			}
		}
		distanceTravelled += stepSize;
	}

	float3 cloudColor = totalLight * light.Intensity * light.GetColor().rgb;
	float3 col = color.xyz * transmittance + cloudColor + GetSky(normalize(float3(0, 1.0f, 0))) * (1 - transmittance);

	return float4(col, 1);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	if(any(threadId.xy >= cView.TargetDimensions))
		return;

	float2 texCoord = threadId.xy * cView.TargetDimensionsInv;
	float sceneDepth = tDepthTexture.Sample(sLinearClamp, texCoord).r;
	float3 viewRay = normalize(ViewFromDepth(texCoord, sceneDepth, cView.ProjectionInverse));
	float linearDepth = length(viewRay);
	float3 rayOrigin = cView.ViewLocation;
	float3 rayDirection = mul(viewRay, (float3x3)cView.ViewInverse);
	uOutput[threadId.xy] = RenderClouds(texCoord, rayOrigin, rayDirection, linearDepth);
}