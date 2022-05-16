#include "Common.hlsli"

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
	float4 FrustumCorners[4];

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
	output.ray = mul(cPass.FrustumCorners[int(input.position.z)], cView.ViewInverse);
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
	float3 uvw = position * cPass.CloudScale + cPass.CloudOffset;
	float4 shape = tCloudsTexture.SampleLevel(sLinearWrap, uvw, 0);
	float s = shape.r * cPass.NoiseWeights.x + shape.g * cPass.NoiseWeights.y + shape.b * cPass.NoiseWeights.z + shape.a * cPass.NoiseWeights.w;
	return max(0, cPass.CloudTheshold - s) * cPass.CloudDensity;
}

float3 LightMarch(float3 position, float3 lightDirection)
{
	float boxDistance = RayBoxDistance(cPass.MinExtents, cPass.MaxExtents, position, lightDirection).y;
	float stepSize = boxDistance / 6;
	float totalDensity = 0;
	float offset = InterleavedGradientNoise(position.xy);
	position -= lightDirection * offset;
	for(int i = 0; i < 6; ++i)
	{
		position += lightDirection * stepSize;
		totalDensity += max(0, SampleDensity(position) * stepSize);
	}

	float transmittance = exp(-totalDensity * 8.0f);
	return 0.01 + transmittance * (1 - 0.01);
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 ro = cView.ViewInverse[3].xyz;
	float3 rd = normalize(input.ray.xyz);
	float4 color = tSceneTexture.Sample(sLinearClamp, input.texCoord);

	float2 boxResult = RayBoxDistance(cPass.MinExtents, cPass.MaxExtents, ro, rd);
	float depth = 100;//LinearizeDepth01(tDepthTexture.Sample(sLinearClamp, input.texCoord).r);
	float maxDepth = depth * length(input.ray.xyz);

	float distanceTravelled = 0;
	float stepSize = boxResult.y / 150;
	float dstLimit = min(maxDepth - boxResult.x, boxResult.y);

	float totalDensity = 0;
	float3 totalLight = 0;
	float transmittance = 1;

	float offset = InterleavedGradientNoise(input.position.xy);
	ro += offset - 1;

	Light light = GetLight(0);

	while (distanceTravelled < dstLimit)
	{
		float3 rayPos = ro + rd * (boxResult.x + distanceTravelled);
		float height = (cPass.MaxExtents.y - rayPos.y) / (cPass.MaxExtents.y - cPass.MinExtents.y);
		float densityMultiplier = tVerticalDensity.Sample(sLinearClamp, float2(0, height)).r;
		float density = SampleDensity(rayPos) * stepSize * densityMultiplier;
		if(density > 0)
		{
			totalLight += LightMarch(rayPos, -light.Direction) * stepSize * densityMultiplier * density;
			transmittance *= exp(-density * stepSize);
			if(transmittance < 0.01f)
			{
				break;
			}
		}
		distanceTravelled += stepSize;
	}
	return float4(color.xyz * transmittance + totalLight * light.Intensity * light.GetColor().rgb, 1);
}
