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
	float4 NoiseWeights;

	float3 MinExtents;
	float padd0;
	float3 MaxExtents;
	float padd1;

	float3 CloudOffset;
	float CloudScale;
	float CloudTheshold;
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
	float3 uvw = position * 0.01;
	float4 shape = tCloudsTexture.SampleLevel(sLinearWrap, uvw, 0);
	float s = saturate(shape.r * cPass.NoiseWeights.x + shape.g * cPass.NoiseWeights.y + shape.b * cPass.NoiseWeights.z + shape.a * cPass.NoiseWeights.w);
	return max(0, s - cPass.CloudTheshold) * cPass.CloudDensity;
}

float LightMarch(float3 position, float3 lightDirection)
{
	float boxDistance = RayBoxDistance(cPass.MinExtents, cPass.MaxExtents, position, lightDirection).y;
	uint steps = 10;
	float stepSize = boxDistance / steps;
	float totalDensity = 0;
	for(int i = 0; i < steps; ++i)
	{
		position += lightDirection * stepSize;
		totalDensity += max(0, SampleDensity(position) * stepSize);
	}

	float transmittance = exp(-totalDensity * 8.0f);
	return 0.01 + transmittance * (1 - 0.01);
}

float4 PSMain(PSInput input) : SV_TARGET
{
	Light light = GetLight(0);
	float3 ro = cView.ViewLocation;
	float3 rd = normalize(input.ray.xyz);

	float4 color = tSceneTexture.Sample(sLinearClamp, input.texCoord);

	float2 boxResult = RayBoxDistance(cPass.MinExtents, cPass.MaxExtents, ro, rd);
	float depth = 10000000000;//LinearizeDepth01(tDepthTexture.Sample(sLinearClamp, input.texCoord).r);
	float maxDepth = depth * length(input.ray.xyz);

	float distanceTravelled = 0;
	float stepSize = boxResult.y / 40;
	float dstLimit = min(maxDepth - boxResult.x, boxResult.y);

	float totalDensity = 0;
	float3 totalLight = 0;
	float transmittance = 1;

	uint seed = SeedThread(input.position.xy, cView.ViewportDimensions, cView.FrameIndex);

	float offset = 0;// 0.2*Random01(seed);// InterleavedGradientNoise(input.position.xy + cView.FrameIndex);
	ro += offset;


	while (distanceTravelled < dstLimit)
	{
		float3 rayPos = ro + rd * (boxResult.x + distanceTravelled);
		float density = SampleDensity(rayPos);

		if(density > 0)
		{
			float lightTransmittance = LightMarch(rayPos, -light.Direction);
			totalLight += density * stepSize * transmittance * lightTransmittance ;
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
