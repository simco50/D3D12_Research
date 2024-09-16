#include "Common.hlsli"
#include "GBuffer.hlsli"
#include "Noise.hlsli"

Texture2D<uint4> tGBuffer : register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D<float4> tColor : register(t2);

RWTexture2D<float4> uOutput : register(u0);

struct Result
{
	bool IsHit;
	float HitT;
	float2 HitUV;

	float4 Debug;
};

Result DepthRayMarch(float3 rayOriginWS, float3 rayDirectionWS, uint numSteps, uint numBinarySearchSteps, Texture2D<float> depthTexture, float jitter)
{
	Result result = (Result)0;

	float4 rayOriginCS = mul(float4(rayOriginWS, 1), cView.WorldToClip);
	rayOriginCS /= rayOriginCS.w;
	float4 rayEndCS = mul(float4(rayOriginWS + rayDirectionWS*50, 1), cView.WorldToClip);
	rayEndCS /= rayEndCS.w;

	float3 rayOriginSS 		= float3(ClipToUV(rayOriginCS.xy), rayOriginCS.z);
	float3 rayEndSS 		= float3(ClipToUV(rayEndCS.xy), rayEndCS.z);
	float3 rayDirectionSS 	= rayEndSS - rayOriginSS;

	float minT = 0.0f;
	float maxT = 1.0f;

	float stepSize = (maxT - minT) / numSteps;
	bool intersect = false;

	// Linear search
	for(uint step = 0; step < numSteps; ++step)
	{
		float t = lerp(minT, maxT, step * stepSize);
		float3 p = rayOriginSS + t * rayDirectionSS;

		float depth = depthTexture.SampleLevel(sPointClamp, p.xy, 0);
		if(p.z + 0.000001f < depth)
		{
			maxT = t;
			intersect = true;
			break;
		}
		else
		{
			minT = t;
		}
	}

	result.IsHit = intersect;
	result.HitUV = rayOriginSS.xy + rayDirectionSS.xy * maxT;

	// Binary search refinement
	if(intersect)
	{
		for(uint step = 0; step < numBinarySearchSteps; ++step)
		{
			float mid = (minT + maxT) * 0.5f;
			float3 p = rayOriginSS + mid * rayDirectionSS;

			float depth = depthTexture.SampleLevel(sPointClamp, p.xy, 0);
			if(p.z + 0.00001f < depth)
			{
				maxT = mid;
			}
			else
			{
				minT = mid;
			}
		}
		
		result.IsHit = true;
		result.HitT = maxT;
		result.HitUV = rayOriginSS.xy + rayDirectionSS.xy * result.HitT;
	}

	return result;
}


[numthreads(8, 8, 1)]
void TraceCS(uint3 threadID : SV_DispatchThreadID)
{
	GBufferData gbuffer = LoadGBuffer(tGBuffer[threadID.xy]);

	float2 uv = TexelToUV(threadID.xy, cView.ViewportDimensionsInv);
	
	float depth = tDepth[threadID.xy];
	float3 worldPos = WorldPositionFromDepth(uv, depth, cView.ClipToWorld);
	float3 V = normalize(worldPos - cView.ViewLocation);
	float3 N = gbuffer.Normal;

	float jitter = GradientNoise(threadID.xy).x;

    Result result = DepthRayMarch(worldPos, reflect(V, gbuffer.Normal), 16, 4, tDepth, jitter);
	if(result.IsHit)
	{
		uOutput[threadID.xy] = tColor.SampleLevel(sLinearClamp, result.HitUV, 0);
	}
	else
	{
		uOutput[threadID.xy] = float4(0,0,0,1);
	}
}