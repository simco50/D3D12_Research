#include "Common.hlsli"
#include "ShaderDebugRender.hlsli"

struct FrustumCullData
{
	bool IsVisible;
	float3 RectMin;
	float3 RectMax;
};

FrustumCullData FrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 worldToClip)
{
	FrustumCullData data = (FrustumCullData)0;
	data.IsVisible = true;

	// Clip space AABB
	float3x4 axis;
	axis[0] = mul(float4(aabbExtents.x * 2, 0, 0, 0), worldToClip);
	axis[1] = mul(float4(0, aabbExtents.y * 2, 0, 0), worldToClip);
	axis[2] = mul(float4(0, 0, aabbExtents.z * 2, 0), worldToClip);

	float4 pos000 = mul(float4(aabbCenter - aabbExtents, 1), worldToClip);
	float4 pos100 = pos000 + axis[0];
	float4 pos010 = pos000 + axis[1];
	float4 pos110 = pos010 + axis[0];
	float4 pos001 = pos000 + axis[2];
	float4 pos101 = pos100 + axis[2];
	float4 pos011 = pos010 + axis[2];
	float4 pos111 = pos110 + axis[2];

	float minW = min3(min3(pos000.w, pos100.w, pos010.w),
				min3(pos110.w, pos001.w, pos101.w),
				min(pos011.w, pos111.w));

	float maxW = max3(max3(pos000.w, pos100.w, pos010.w),
					max3(pos110.w, pos001.w, pos101.w),
					max(pos011.w, pos111.w));

	// Plane inequalities
	float4 planeMins = min3(
						min3(float4(pos000.xy, -pos000.xy) - pos000.w, float4(pos001.xy, -pos001.xy) - pos001.w, float4(pos010.xy, -pos010.xy) - pos010.w),
						min3(float4(pos100.xy, -pos100.xy) - pos100.w, float4(pos110.xy, -pos110.xy) - pos110.w, float4(pos011.xy, -pos011.xy) - pos011.w),
						min3(float4(pos101.xy, -pos101.xy) - pos101.w, float4(pos111.xy, -pos111.xy) - pos111.w, float4(1, 1, 1, 1))
						);

	// Screen space AABB
	float3 ssPos000 = pos000.xyz / pos000.w;
	float3 ssPos100 = pos100.xyz / pos100.w;
	float3 ssPos010 = pos010.xyz / pos010.w;
	float3 ssPos110 = pos110.xyz / pos110.w;
	float3 ssPos001 = pos001.xyz / pos001.w;
	float3 ssPos101 = pos101.xyz / pos101.w;
	float3 ssPos011 = pos011.xyz / pos011.w;
	float3 ssPos111 = pos111.xyz / pos111.w;

	data.RectMin = min3(
					min3(ssPos000, ssPos100, ssPos010),
					min3(ssPos110, ssPos001, ssPos101),
					min3(ssPos011, ssPos111, float3(1, 1, 1))
					);

	data.RectMax = max3(
					max3(ssPos000, ssPos100, ssPos010),
					max3(ssPos110, ssPos001, ssPos101),
					max3(ssPos011, ssPos111, float3(-1, -1, -1))
					);

	data.IsVisible &= data.RectMax.z > 0;

	if(minW <= 0 && maxW > 0)
	{
		data.RectMin = -1;
		data.RectMax = 1;
		data.IsVisible = true;
	}
	else
	{
		data.IsVisible &= maxW > 0.0f;
	}

	data.IsVisible &= !any(planeMins > 0.0f);

	return data;
}

FrustumCullData FrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 localToWorld, float4x4 worldToClip)
{
	// Transform bounds to world space
	aabbExtents = mul(aabbExtents, (float3x3)localToWorld);
	aabbCenter = mul(float4(aabbCenter, 1), localToWorld).xyz;

	return FrustumCull(aabbCenter, aabbExtents, worldToClip);
}

uint ComputeHZBMip(int4 rectPixels, int texelCoverage)
{
	int2 rectSize = rectPixels.zw - rectPixels.xy;
	int mipOffset = (int)log2((float)texelCoverage) - 1;
	int2 mipLevelXY = firstbithigh(rectSize);
	int mip = max(max(mipLevelXY.x, mipLevelXY.y) - mipOffset, 0);
	if(any((rectPixels.zw >> mip) - (rectPixels.xy >> mip) >= texelCoverage))
	{
		++mip;
	}
	return mip;
}

#define HZB_DEBUG_RENDER 0

bool HZBCull(FrustumCullData cullData, Texture2D<float> hzbTexture, bool debug = false)
{
	static const uint hzbTexelCoverage = 4;

	// Convert NDC to UV
	float4 rect = saturate(float4(cullData.RectMin.xy, cullData.RectMax.xy) * float2(0.5f, -0.5f).xyxy + 0.5f).xwzy;
	// Convert to texel indices. Contract bounds to only account for the area overlapping texel centres
	int4 rectPixels = int4(rect * cView.HZBDimensions.xyxy + float4(0.5f, 0.5f, -0.5f, -0.5f));
	rectPixels = int4(rectPixels.xy, max(rectPixels.xy, rectPixels.zw));
	int mip = ComputeHZBMip(rectPixels, hzbTexelCoverage);
	rectPixels >>= mip;
	float2 texelSize = 1.0f / cView.HZBDimensions * (1u << mip);

	float maxDepth = cullData.RectMax.z;
	float depth = 0;

	if(hzbTexelCoverage == 4)
	{
		float4 xCoords = (min(rectPixels.x + float4(0, 1, 2, 3), rectPixels.z) + 0.5f) * texelSize.x;
		float4 yCoords = (min(rectPixels.y + float4(0, 1, 2, 3), rectPixels.w) + 0.5f) * texelSize.y;

		float depth00 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.x, yCoords.x), mip);
		float depth10 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.y, yCoords.x), mip);
		float depth20 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.z, yCoords.x), mip);
		float depth30 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.w, yCoords.x), mip);

		float depth01 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.x, yCoords.y), mip);
		float depth11 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.y, yCoords.y), mip);
		float depth21 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.z, yCoords.y), mip);
		float depth31 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.w, yCoords.y), mip);

		float depth02 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.x, yCoords.z), mip);
		float depth12 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.y, yCoords.z), mip);
		float depth22 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.z, yCoords.z), mip);
		float depth32 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.w, yCoords.z), mip);

		float depth03 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.x, yCoords.w), mip);
		float depth13 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.y, yCoords.w), mip);
		float depth23 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.z, yCoords.w), mip);
		float depth33 = hzbTexture.SampleLevel(sPointClamp, float2(xCoords.w, yCoords.w), mip);

		depth =
			min4(
				min4(depth00, depth10, depth20, depth30),
				min4(depth01, depth11, depth21, depth31),
				min4(depth02, depth12, depth22, depth32),
				min4(depth03, depth13, depth23, depth33)
			);
	}
	else if(hzbTexelCoverage == 2)
	{
		float depth00 = hzbTexture.SampleLevel(sPointClamp, (rectPixels.xy + 0.5f) * texelSize, mip);
		float depth10 = hzbTexture.SampleLevel(sPointClamp, (rectPixels.zy + 0.5f) * texelSize, mip);
		float depth01 = hzbTexture.SampleLevel(sPointClamp, (rectPixels.xw + 0.5f) * texelSize, mip);
		float depth11 = hzbTexture.SampleLevel(sPointClamp, (rectPixels.zw + 0.5f) * texelSize, mip);

		depth = min4(depth00, depth10, depth01, depth11);
	}

	bool isOccluded = depth > maxDepth;

#if HZB_DEBUG_RENDER
	if(debug)
	{
		DrawRect(rect.xy, rect.zw, RectMode::MinMax, Colors::Red);
		TextWriter writer = CreateTextWriter(rect.xy * cView.ViewportDimensions);
		writer = writer + 'H' + 'Z' + 'B' + ' ' + 'm' + 'i' + 'p' + ':' + ' ';
		writer.Int(mip);
		writer.NewLine();
		writer = writer + 'V' + 'i' + 's' + 'i' + 'b' + 'l' + 'e' + ':' + ' ';
		if(isOccluded)
		{
			writer.SetColor(Colors::Red);
			writer = writer + 'N' + 'o';
		}
		else
		{
			writer.SetColor(Colors::Green);
			writer = writer + 'Y' + 'e' + 's';
		}
		uint gridColor = 0xFFFFFF33;
		for(float y = 0; y < 1; y += texelSize.y)
		{
			DrawScreenLine(float2(0, y), float2(1, y), gridColor);

			for(float x = 0; x < 1; x += texelSize.x)
			{
				DrawScreenLine(float2(x, 0), float2(x, 1), gridColor);
			}
		}

		float2 rectSize = cView.ViewportDimensionsInv * 3;
		if(hzbTexelCoverage == 4)
		{
			float4 xCoords = (min(rectPixels.x + float4(0, 1, 2, 3), rectPixels.z) + 0.5f) * texelSize.x;
			float4 yCoords = (min(rectPixels.y + float4(0, 1, 2, 3), rectPixels.w) + 0.5f) * texelSize.y;

			DrawRect(float2(xCoords.x, yCoords.x), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.y, yCoords.x), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.z, yCoords.x), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.w, yCoords.x), rectSize, RectMode::CenterExtents, Colors::Green);

			DrawRect(float2(xCoords.x, yCoords.y), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.y, yCoords.y), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.z, yCoords.y), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.w, yCoords.y), rectSize, RectMode::CenterExtents, Colors::Green);

			DrawRect(float2(xCoords.x, yCoords.z), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.y, yCoords.z), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.z, yCoords.z), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.w, yCoords.z), rectSize, RectMode::CenterExtents, Colors::Green);

			DrawRect(float2(xCoords.x, yCoords.w), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.y, yCoords.w), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.z, yCoords.w), rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect(float2(xCoords.w, yCoords.w), rectSize, RectMode::CenterExtents, Colors::Green);
		}
		else if(hzbTexelCoverage == 2)
		{
			DrawRect((rectPixels.xy + 0.5f) * texelSize, rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect((rectPixels.zy + 0.5f) * texelSize, rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect((rectPixels.xw + 0.5f) * texelSize, rectSize, RectMode::CenterExtents, Colors::Green);
			DrawRect((rectPixels.zw + 0.5f) * texelSize, rectSize, RectMode::CenterExtents, Colors::Green);
		}
	}
#endif

	return cullData.IsVisible && !isOccluded;
}
