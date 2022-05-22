#include "Common.hlsli"
#include "Random.hlsli"

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
Texture3D tCloudsTexture : register(t2);
Texture2D tVerticalDensity : register(t3);

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

float SampleDensity(float3 position)
{
	float3 uvw = 0.1f * (position + 0.01 * float3(cView.FrameIndex, 0, 0)) * cPass.CloudScale;
	float4 lowFrequencies = tCloudsTexture.SampleLevel(sLinearWrap, uvw, 0);

	float lowFrequencyFBM = 
		lowFrequencies.y * 0.625f +
		lowFrequencies.z * 0.25f +
		lowFrequencies.w * 0.125f;

	float baseCloud = Remap(lowFrequencies.r, (1.0f - lowFrequencyFBM), 1.0f, 0.0f, 1.0f);

	// Density is higher at higher altitude
	float heightGradient = saturate(InverseLerp(position.y, cPass.MinExtents.y, cPass.MaxExtents.y));

	// Vertical falloff
	float verticalDensity = tVerticalDensity.SampleLevel(sLinearClamp, float2(0, heightGradient), 0).x;
	
	return baseCloud * cPass.CloudDensity * heightGradient * verticalDensity;
}

float HenyeyGreenstreinPhase(float LoV, float G)
{
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}

float Phase(float a) 
{
    float forwardScattering = .8f;
    float backScattering = .3f;
    float baseBrightness = .1f;
    float phaseFactor = .5f;

	float blend = .5;
	float hgBlend = HenyeyGreenstreinPhase(a, forwardScattering) * (1-blend) + HenyeyGreenstreinPhase(a, -backScattering) * blend;
	return baseBrightness + hgBlend * phaseFactor;
}

float LightMarch(float3 position, float3 lightDirection)
{
	const float lightAbsorptionTowardsSun = 1.0f;
	const float darknessThreshold = 0.01f;

	float boxDistance = RayBoxDistance(cPass.MinExtents, cPass.MaxExtents, position, lightDirection).y;
	uint steps = 8;
	float stepSize = boxDistance / steps;
	float totalDensity = 0;
	for(int i = 0; i < steps; ++i)
	{
		position += lightDirection * stepSize;
		totalDensity += max(0, SampleDensity(position) * stepSize);
	}

	float transmittance = exp(-totalDensity * lightAbsorptionTowardsSun);
	return darknessThreshold + transmittance * (1 - darknessThreshold);
}

float4 PSMain(PSInput input) : SV_TARGET
{
	Light light = GetLight(0);
	float3 ro = cView.ViewLocation;
	float3 rd = normalize(input.ray.xyz);

	float4 color = tSceneTexture.Sample(sLinearClamp, input.texCoord);

	float2 boxResult = RayBoxDistance(cPass.MinExtents, cPass.MaxExtents, ro, rd);
	float maxDepth = LinearizeDepth(tDepthTexture.Sample(sLinearClamp, input.texCoord).r);

	float distanceTravelled = 0;
	float stepSize = boxResult.y / 40;
	float dstLimit = min(maxDepth - boxResult.x, boxResult.y);

	float totalDensity = 0;
	float3 totalLight = 0;
	float transmittance = 1;

	uint seed = SeedThread(input.position.xy, cView.ViewportDimensions, cView.FrameIndex);

	float offset = InterleavedGradientNoise(input.position.xy + cView.FrameIndex);
	ro += rd * offset * 10;

	 // Phase function makes clouds brighter around sun
    float cosAngle = dot(-rd, light.Direction);
    float phaseVal = Phase(cosAngle);

	while (distanceTravelled < dstLimit)
	{
		float3 rayPos = ro + rd * (boxResult.x + distanceTravelled);
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
	float3 col = color.xyz * transmittance + cloudColor;

	return float4(col, 1);
}
