#include "Common.hlsli"
#include "Random.hlsli"
#include "Volumetrics.hlsli"
#include "SkyCommon.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float4 ray : RAY;
};

Texture2D tSceneTexture : register(t0);
Texture2D tDepthTexture : register(t1);
Texture2D tVerticalDensity : register(t2);
Texture3D tShapeNoise : register(t3);
Texture3D tDetailNoise : register(t4);

struct PassParameters
{
	float3 MinExtents;
	float padd0;
	float3 MaxExtents;
	float padd1;

	float CloudScale;
	float CloudDensity;
};

ConstantBuffer<PassParameters> cPass : register(b0);

PSInput VSMain(VSInput input)
{
	PSInput output;
	output.position = float4(input.position.xy, 0, 1);
	output.texCoord = input.texCoord;

	float4 ray = output.position;
	ray = mul(ray, cView.ProjectionInverse);
	ray.xyz = mul(ray.xyz, (float3x3)cView.ViewInverse);
	output.ray = ray;
	return output;
}

float2 RaySphereIntersect(float3 rayOrigin, float3 rayDirection, float3 sphereCenter, float sphereRadius)
{
    float3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDirection);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;
    if(h < 0.0)
	{
		return -1.0f;
	}
    h = sqrt(h);
    return float2(-b - h, -b + h);
}

float2 RayBoxDistance(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDirection)
{
	//http://jcgt.org/published/0007/03/04/
	float3 t0 = (boundsMin - rayOrigin) / rayDirection;
	float3 t1 = (boundsMax - rayOrigin) / rayDirection;
	float3 tMin = min(t0, t1);
	float3 tMax = max(t0, t1);

	float distanceA = max(max(tMin.x, tMin.y), tMin.z);
	float distanceB = min(min(tMax.x, tMax.y), tMax.z);

	float distanceToBox = max(0, distanceA);
	float distanceInsideBox = max(0, distanceB - distanceToBox);
	return float2(distanceToBox, distanceInsideBox);
}

static const float PlanetOffset = -1000.0f;
static const float InnerAtmosphereRadius = 1050.0f;
static const float OuterAtmosphereRadius = 1080.0f;

float SampleDensity(float3 position)
{
	float3 uvw = 0.1f * (position + 0.01 * float3(cView.FrameIndex, 0, 0)) * cPass.CloudScale;
	float4 lowFrequencies = tShapeNoise.SampleLevel(sLinearWrap, uvw, 0);

	float lowFrequencyFBM =
		lowFrequencies.y * 0.625f +
		lowFrequencies.z * 0.25f +
		lowFrequencies.w * 0.125f;

	float4 highFrequencies = 0.3 * tShapeNoise.SampleLevel(sLinearWrap, uvw * 6, 0);
	float highFrequencyFBM =
		highFrequencies.y * 0.625f +
		highFrequencies.z * 0.25f +
		highFrequencies.w * 0.125f;

	float baseCloud = saturate(Remap(lowFrequencies.r, (1.0f - lowFrequencyFBM), 1.0f, 0.0f, 1.0f));

	baseCloud = Remap(baseCloud, highFrequencyFBM, 1.0f, 0.0f, 1.0f);

	float height = length(position - float3(0, PlanetOffset, 0));

	// Density is higher at higher altitude
	float heightGradient = saturate(InverseLerp(height, InnerAtmosphereRadius, OuterAtmosphereRadius));

	// Vertical falloff
	float verticalDensity = tVerticalDensity.SampleLevel(sLinearClamp, float2(0, heightGradient), 0).x;

	return saturate(baseCloud * cPass.CloudDensity * heightGradient * verticalDensity);
}

float LightMarch(float3 rayOrigin, float3 rayDirection)
{
	const float lightAbsorptionTowardsSun = 1.0f;
	const float darknessThreshold = 0.01f;

	float outerAtmosphereHit = RaySphereIntersect(rayOrigin, rayDirection, float3(0, PlanetOffset, 0), OuterAtmosphereRadius).y;
	float rayDistance = length(rayDirection * outerAtmosphereHit);

	uint steps = 8;
	float stepSize = rayDistance / steps;
	float totalDensity = 0;
	for(int i = 0; i < steps; ++i)
	{
		rayOrigin += rayDirection * stepSize;
		totalDensity += max(0, SampleDensity(rayOrigin) * stepSize);
	}

	float transmittance = exp(-totalDensity * lightAbsorptionTowardsSun);
	return darknessThreshold + transmittance * (1 - darknessThreshold);
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = tSceneTexture.Sample(sLinearClamp, input.texCoord);
	float maxDepth = LinearizeDepth(tDepthTexture.Sample(sLinearClamp, input.texCoord).r);
	if(maxDepth < cView.NearZ)
		return color;

	Light light = GetLight(0);

	float3 rayOrigin = cView.ViewLocation;
	float3 rayDirection = normalize(input.ray.xyz);
	float offset = InterleavedGradientNoise(input.position.xy + cView.FrameIndex);
	rayOrigin += rayDirection * offset * 1;

	float2 planetHit = RaySphereIntersect(rayOrigin, rayDirection, float3(0, PlanetOffset, 0), 1000);
	if(any(planetHit > -1))
		return 0;

	float atmosphereHit = RaySphereIntersect(rayOrigin, rayDirection, float3(0, PlanetOffset, 0), InnerAtmosphereRadius).y;
	float outerAtmosphereHit = RaySphereIntersect(rayOrigin, rayDirection, float3(0, PlanetOffset, 0), OuterAtmosphereRadius).y;

	float raymarchDistance = outerAtmosphereHit - atmosphereHit;

	const float stepSize = 1.0f;

	float distanceTravelled = 0;

	 // Phase function makes clouds brighter around sun
    float cosAngle = dot(-rayDirection, light.Direction);
	const float forwardScattering = .8f;
    const float backScattering = .3f;
	float phaseVal = lerp(HenyeyGreenstreinPhase(cosAngle, forwardScattering), HenyeyGreenstreinPhase(cosAngle, backScattering), 0.5f);

	float3 totalLight = 0;
	float transmittance = 1;

	while (distanceTravelled < raymarchDistance)
	{
		float3 rayPos = rayOrigin + rayDirection * (atmosphereHit + distanceTravelled);
		float density = SampleDensity(rayPos);

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
	float3 col = color.xyz * transmittance + cloudColor + GetSky(float3(0, 1, 0)) * (1 - transmittance);

	return float4(col, 1);
}
