#include "Common.hlsli"
#include "Random.hlsli"
#include "Lighting.hlsli"
#include "Volumetrics.hlsli"
#include "Noise.hlsli"

struct PassParams
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
	Texture2DH<float4> SceneTexture;
	Texture2DH<float> DepthTexture;
	Texture2DH<float4> CloudTypeDensityLUT;
	Texture3DH<float4> ShapeNoise;
	Texture3DH<float4> DetailNoise;
	RWTexture2DH<float4> Output;
};
DEFINE_CONSTANTS(PassParams, 0);

bool RaySphereIntersect(float3 rayOrigin, float3 rayDirection, float3 sphereCenter, float sphereRadius, out float2 intersection)
{
    float3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDirection);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;
    if(h < 0.0)
	{
		intersection = -1.0f;
		return false;
	}
    h = sqrt(h);
    intersection = float2(-b - h, -b + h);
	return true;
}

float SampleDensity(float3 position, uint mipLevel)
{
	// Relative height based on spherical planet
	float height = length(position - float3(0, -cPassParams.PlanetRadius, 0));
	float heightGradient = saturate(InverseLerp(height - cPassParams.PlanetRadius, cPassParams.AtmosphereHeightStart, cPassParams.AtmosphereHeightEnd));

	// Wind
	position += heightGradient * cPassParams.WindDirection * cPassParams.TopSkew;
	position += (cPassParams.WindDirection + float3(0, 0.2f, 0)) * cView.FrameIndex * cPassParams.WindSpeed;
	position *= cPassParams.GlobalScale;

	// Shape
	float4 lowFrequencies = cPassParams.ShapeNoise.SampleLevel(sLinearWrap, position * cPassParams.ShapeNoiseScale, mipLevel);
	float lowFrequencyFBM = dot(lowFrequencies.yzw, float3(0.625f, 0.25f, 0.125f));
	float baseCloud = saturate(Remap(lowFrequencies.r, (1.0f - lowFrequencyFBM), 1.0f, 0.0f, 1.0f));

	// Coverage
	float coverage = cPassParams.Coverage;
	baseCloud = Remap(baseCloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f);
	baseCloud *= coverage;

	// Cloud type vertical gradient
	float verticalDensity = cPassParams.CloudTypeDensityLUT.SampleLevel(sLinearClamp, float2(cPassParams.CloudType, heightGradient), 0).x;
	baseCloud *= verticalDensity;

	// Detail noise
	float4 highFrequencies = cPassParams.DetailNoise.SampleLevel(sLinearWrap, position * cPassParams.DetailNoiseScale, mipLevel);
	float highFrequencyFBM = dot(highFrequencies.xyz, float3(0.625f, 0.25f, 0.125f));
	float highFrequencyNoise = lerp(highFrequencyFBM, 1 - highFrequencyFBM, saturate(heightGradient * 10));
	float finalCloud = Remap(baseCloud, highFrequencyNoise * cPassParams.DetailNoiseInfluence, 1.0f, 0.0f, 1.0f);

	return saturate(finalCloud * cPassParams.GlobalDensity);
}

float LightMarch(float3 rayOrigin, float3 rayDirection)
{
	const float coneSize = 200.0f;
	float stepSize = coneSize / cPassParams.LightMarchSteps;

	float totalDensity = 0;

	uint seed = SeedThread(0);
	for(uint i = 0; i < cPassParams.LightMarchSteps; ++i)
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
	if(RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPassParams.PlanetRadius, 0), cPassParams.PlanetRadius + cPassParams.AtmosphereHeightEnd, atmosphereHitTop))
	{
		float2 atmosphereHitBottom;
		if(RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPassParams.PlanetRadius, 0), cPassParams.PlanetRadius + cPassParams.AtmosphereHeightStart, atmosphereHitBottom))
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

	const float stepSize = cPassParams.RayStepSize;
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
	float3 sunColor = light.GetColor();

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
	if(any(threadId.xy >= cView.ViewportDimensions))
		return;

#define DEBUG_CLOUDS 0
#if DEBUG_CLOUDS
	float2 debugUV = InverseLerp((float2)threadId.xy, float2(20.0f, 20.0f), float2(300.0f, 300.0f));
	if(all(debugUV >= 0.0f) && all(debugUV <= 1.0f))
	{
		debugUV = (debugUV * 2.0f - 1.0f) * 1000.0f;
		float height = lerp(cPassParams.AtmosphereHeightStart, cPassParams.AtmosphereHeightEnd, 0.1f);
		float3 pos = float3(debugUV.x, height, debugUV.y);
		uOutput[threadId.xy] = float4(SampleDensity(pos, 0).xxx, 1);
		return;
	}
#endif

	float2 uv = TexelToUV(threadId.xy, cView.ViewportDimensionsInv);
	float4 color = cPassParams.SceneTexture.SampleLevel(sPointClamp, uv, 0);
	float sceneDepth = cPassParams.DepthTexture.SampleLevel(sPointClamp, uv, 0).r;
	float3 viewRay = normalize(ViewPositionFromDepth(uv, sceneDepth, cView.ClipToView));
	float linearDepth = sceneDepth == 0 ? 10000000 : length(viewRay);
	float3 rayOrigin = cView.ViewLocation;
	float3 rayDirection = mul(viewRay, (float3x3)cView.ViewToWorld);

	float2 planetHit;
	if(RaySphereIntersect(rayOrigin, rayDirection, float3(0, -cPassParams.PlanetRadius, 0), cPassParams.PlanetRadius, planetHit))
	{
		float hit = all(planetHit > 0) ? planetHit.x : planetHit.y;
		if(hit > 0 && hit < linearDepth)
		{
			linearDepth = 0;
		}
	}


	float4 scatteringTransmittance = RenderClouds(threadId.xy, rayOrigin, rayDirection, linearDepth);
	float3 col = color.xyz * scatteringTransmittance.w + scatteringTransmittance.xyz;
	cPassParams.Output.Store(threadId.xy, float4(col, 1));
}
