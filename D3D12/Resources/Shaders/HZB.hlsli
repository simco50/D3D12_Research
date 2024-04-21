#include "Common.hlsli"
#include "ShaderDebugRender.hlsli"

struct FrustumCullData
{
	bool IsVisible;
	float3 RectMin;
	float3 RectMax;
};

FrustumCullData FrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 localToWorld, float4x4 worldToClip)
{
	FrustumCullData data = (FrustumCullData)0;
	data.IsVisible = true;

	float3 ext = 2.0f * aabbExtents;
	float4x4 extentsBasis = float4x4(
		ext.x,		0,		0,	 	0,
			0,	ext.y,		0, 		0,
			0,		0,	ext.z, 		0,
			0, 		0, 		0, 		0
		);

	float4x4 axis = mul(mul(extentsBasis, localToWorld), worldToClip);

	float4 pos000 = mul(mul(float4(aabbCenter - aabbExtents, 1), localToWorld), worldToClip);
	float4 pos100 = pos000 + axis[0];
	float4 pos010 = pos000 + axis[1];
	float4 pos110 = pos010 + axis[0];
	float4 pos001 = pos000 + axis[2];
	float4 pos101 = pos100 + axis[2];
	float4 pos011 = pos010 + axis[2];
	float4 pos111 = pos110 + axis[2];

	float minW = Min(Min(pos000.w, pos100.w, pos010.w),
				Min(pos110.w, pos001.w, pos101.w),
				min(pos011.w, pos111.w));

	float maxW = Max(Max(pos000.w, pos100.w, pos010.w),
					Max(pos110.w, pos001.w, pos101.w),
					max(pos011.w, pos111.w));

	// Plane inequalities
	float4 planeMins = Min(
						Min(float4(pos000.xy, -pos000.xy) - pos000.w, float4(pos001.xy, -pos001.xy) - pos001.w, float4(pos010.xy, -pos010.xy) - pos010.w),
						Min(float4(pos100.xy, -pos100.xy) - pos100.w, float4(pos110.xy, -pos110.xy) - pos110.w, float4(pos011.xy, -pos011.xy) - pos011.w),
						Min(float4(pos101.xy, -pos101.xy) - pos101.w, float4(pos111.xy, -pos111.xy) - pos111.w, float4(1, 1, 1, 1))
						);

	// Clip space AABB
	float3 csPos000 = pos000.xyz / pos000.w;
	float3 csPos100 = pos100.xyz / pos100.w;
	float3 csPos010 = pos010.xyz / pos010.w;
	float3 csPos110 = pos110.xyz / pos110.w;
	float3 csPos001 = pos001.xyz / pos001.w;
	float3 csPos101 = pos101.xyz / pos101.w;
	float3 csPos011 = pos011.xyz / pos011.w;
	float3 csPos111 = pos111.xyz / pos111.w;

	data.RectMin = Min(
					Min(csPos000, csPos100, csPos010),
					Min(csPos110, csPos001, csPos101),
					Min(csPos011, csPos111, float3(1, 1, 1))
					);

	data.RectMax = Max(
					Max(csPos000, csPos100, csPos010),
					Max(csPos110, csPos001, csPos101),
					Max(csPos011, csPos111, float3(-1, -1, -1))
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

#define HZB_DEBUG_RENDER 0

bool HZBCull(FrustumCullData cullData, Texture2D<float> hzbTexture, float2 hzbDimensions, bool debug = false)
{
	// Convert NDC to UV. Y is flipped in DX, so flip Y and swap Min and Max Y component.
	float4 rect = saturate(float4(cullData.RectMin.xy, cullData.RectMax.xy) * float2(0.5f, -0.5f).xyxy + 0.5f).xwzy;
	int4 rectPixels = rect * hzbDimensions.xyxy;

	// Clamp bounds
	rectPixels.xy = max(rectPixels.xy, 0);
	rectPixels.zw = min(rectPixels.zw, hzbDimensions.xy);

	// Compute the mip level. * 0.5 as we have a 4x4 pixel sample kernel
	float2 rectSize = (rectPixels.zw - rectPixels.xy) * 0.5f;
	int mip = max(ceil(log2(max(rectSize.x, rectSize.y))), 0);

	// Determine whether a higher res mip can be used
	int levelLower = max(mip - 1, 0);
	float4 lowerRect = rectPixels * exp2(-levelLower);
	float2 lowerRectSize = ceil(lowerRect.zw) - floor(lowerRect.xy);
	if(lowerRectSize.x <= 4 && lowerRectSize.y <= 4)
		mip = levelLower;

	// Transform the texel coordinates for the selected mip
	rectPixels >>= mip;
	float2 texelSize = rcp(hzbDimensions) * (1u << mip);

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

	float depth =
		Min(
			Min(depth00, depth10, depth20, depth30),
			Min(depth01, depth11, depth21, depth31),
			Min(depth02, depth12, depth22, depth32),
			Min(depth03, depth13, depth23, depth33)
		);

	float maxDepth = cullData.RectMax.z;
	bool isOccluded = depth > maxDepth;

#if HZB_DEBUG_RENDER
	if(debug)
	{
		DrawRect(rect.xy, rect.zw, RectMode::MinMax, Colors::Red);
		TextWriter writer = CreateTextWriter(rect.xy * cView.ViewportDimensions);
		String hzbText = TEXT("HZB mip: ");
		writer.Text(hzbText);
		writer.Int(mip);
		writer.NewLine();
		String visibleText = TEXT("Visible: ");
		writer.Text(visibleText);
		if(isOccluded)
		{
			writer.SetColor(Colors::Red);
			String noText = TEXT("No");
			writer.Text(noText);
		}
		else
		{
			writer.SetColor(Colors::Green);
			String yesText = TEXT("Yes");
			writer.Text(yesText);
		}

		if(mip >= 3)
		{
			float4 gridColor = float4(1, 1, 1, 0.2f);
			for(float y = 0; y < 1; y += texelSize.y)
			{
				DrawScreenLine(float2(0, y), float2(1, y), gridColor);

				for(float x = 0; x < 1; x += texelSize.x)
				{
					DrawScreenLine(float2(x, 0), float2(x, 1), gridColor);
				}
			}
		}

		float2 rectSize = cView.ViewportDimensionsInv * 3;
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
#endif

	return cullData.IsVisible && !isOccluded;
}
