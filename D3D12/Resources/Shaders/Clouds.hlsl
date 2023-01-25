#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "Volumetrics.hlsli"

RWTexture2D<float4> uOutput : register(u0);
Texture2D tSceneTexture : register(t0);
Texture2D tDepthTexture : register(t1);
Texture2D tCloudTypeDensityLUT : register(t2);
Texture3D tShapeNoise : register(t3);
Texture3D tDetailNoise : register(t4);

struct PassParameters
{
	float GlobalScale;
	float ShapeNoiseScale;
	float DetailNoiseScale;
	float Coverage;
	float GlobalDensity;
	float RayStepSize;
	uint LightMarchSteps;
	float PlanetRadius;
	float AtmosphereHeightStart;
	float AtmosphereHeightEnd;
	float DetailNoiseInfluence;
	float CloudType;
	float3 WindDirection;
	float WindSpeed;
	float TopSkew;
};

ConstantBuffer<PassParameters> cPass : register(b0);

float SampleDensity(float3 position, uint mipLevel)
{
	// Relative height based on spherical planet
	float height = length(position - float3(0, -cPass.PlanetRadius, 0));
	float heightGradient = saturate(InverseLerp(height - cPass.PlanetRadius, cPass.AtmosphereHeightStart, cPass.AtmosphereHeightEnd));

	// Wind
	position += heightGradient * cPass.WindDirection * cPass.TopSkew;
	position += (cPass.WindDirection + float3(0, 0.2f, 0)) * cView.FrameIndex * cPass.WindSpeed;
	position *= cPass.GlobalScale;

	// Shape
	float4 lowFrequencies = tShapeNoise.SampleLevel(sLinearWrap, position * cPass.ShapeNoiseScale, mipLevel);
	float lowFrequencyFBM = dot(lowFrequencies.yzw, float3(0.625f, 0.25f, 0.125f));
	float baseCloud = saturate(Remap(lowFrequencies.r, (1.0f - lowFrequencyFBM), 1.0f, 0.0f, 1.0f));

	// Coverage
	float coverage = cPass.Coverage;
	baseCloud = Remap(baseCloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f);
	baseCloud *= coverage;

	// Cloud type vertical gradient
	float verticalDensity = tCloudTypeDensityLUT.SampleLevel(sLinearClamp, float2(cPass.CloudType, heightGradient), 0).x;
	baseCloud *= verticalDensity;

	// Detail noise
	float4 highFrequencies = tDetailNoise.SampleLevel(sLinearWrap, position * cPass.DetailNoiseScale, mipLevel);
	float highFrequencyFBM = dot(highFrequencies.xyz, float3(0.625f, 0.25f, 0.125f));
	float highFrequencyNoise = lerp(highFrequencyFBM, 1 - highFrequencyFBM, saturate(heightGradient * 10));
	float finalCloud = Remap(baseCloud, highFrequencyNoise * cPass.DetailNoiseInfluence, 1.0f, 0.0f, 1.0f);

	return saturate(finalCloud * cPass.GlobalDensity);
}

float LightMarch(float3 rayOrigin, float3 rayDirection)
{
	const float coneSize = 200.0f;
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

	return exp(-totalDensity);
}

float4 RenderClouds(uint2 pixel, float3 rayOrigin, float3 rayDirection, float maxDepth)
{
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
		return float4(0, 0, 0, 1);
	}

	minT = max(0.0f, minT);
	maxT = min(maxDepth, max(0.0f, maxT));
	if(minT >= maxT)
	{
		return float4(0, 0, 0, 1);
	}

	Light light = GetLight(0);

	const float stepSize = cPass.RayStepSize;
	float offset = InterleavedGradientNoise(pixel + cView.FrameIndex);
	rayOrigin += rayDirection * offset * stepSize;

	// Phase function makes clouds brighter around sun
    float cosAngle = dot(rayDirection, -light.Direction);
	const float forwardScattering = 0.8f;
    const float backScattering = -0.2f;
	float phaseVal = lerp(HenyeyGreenstreinPhase(cosAngle, forwardScattering), HenyeyGreenstreinPhase(cosAngle, backScattering), 0.5f);

	float3 totalScattering = 0.0f;
	float transmittance = 1.0f;

	float3 ambientColor = GetSky(normalize(float3(0, 1.0f, 0)));
	float3 sunColor = light.Intensity * light.GetColor().rgb;

	for(float t = minT; t <= maxT; t += stepSize)
	{
		float3 rayPos = rayOrigin + t * rayDirection;
		float density = SampleDensity(rayPos, 0);

		if(density > 0)
		{
			float3 sunScattering = LightMarch(rayPos, -light.Direction) * sunColor;
			totalScattering += density * stepSize * transmittance * (sunScattering * phaseVal + ambientColor);
			transmittance *= exp(-density * stepSize);
			if(transmittance < 0.01f)
			{
				break;
			}
		}
	}

	return float4(totalScattering, transmittance);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	if(any(threadId.xy >= cView.TargetDimensions))
		return;

#define DEBUG_CLOUDS 0
#if DEBUG_CLOUDS
	float2 debugUV = InverseLerp((float2)threadId.xy, float2(20.0f, 20.0f), float2(300.0f, 300.0f));
	if(all(debugUV >= 0.0f) && all(debugUV <= 1.0f))
	{
		debugUV = (debugUV * 2.0f - 1.0f) * 1000.0f;
		float height = lerp(cPass.AtmosphereHeightStart, cPass.AtmosphereHeightEnd, 0.1f);
		float3 pos = float3(debugUV.x, height, debugUV.y);
		uOutput[threadId.xy] = float4(SampleDensity(pos, 0).xxx, 1);
		return;
	}
#endif

	float2 texCoord = (threadId.xy + 0.5f) * cView.TargetDimensionsInv;
	float4 color = tSceneTexture.SampleLevel(sPointClamp, texCoord, 0);
	float sceneDepth = tDepthTexture.SampleLevel(sPointClamp, texCoord, 0).r;
	float3 viewRay = normalize(ViewFromDepth(texCoord, sceneDepth, cView.ProjectionInverse));
	float linearDepth = sceneDepth == 0 ? 10000000 : length(viewRay);
	float3 rayOrigin = cView.ViewLocation;
	float3 rayDirection = mul(viewRay, (float3x3)cView.ViewInverse);

	// Physically based pitch black earth :-)
	float2 planetHit;
	if(RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPass.PlanetRadius, 0), cPass.PlanetRadius, planetHit))
	{
		float hit = all(planetHit > 0) ? planetHit.x : planetHit.y;
		if(hit > 0 && hit < linearDepth)
		{
			color = 0;
			linearDepth = hit;
		}
	}

	float4 scatteringTransmittance = RenderClouds(threadId.xy, rayOrigin, rayDirection, linearDepth);
	float3 col = color.xyz * scatteringTransmittance.w + scatteringTransmittance.xyz;
	uOutput[threadId.xy] = float4(col, 1);
}
