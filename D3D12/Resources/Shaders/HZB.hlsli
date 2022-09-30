#include "Common.hlsli"

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

bool HZBCull(FrustumCullData cullData, Texture2D<float> hzbTexture)
{
	float4 rect = saturate(float4(cullData.RectMin.xy, cullData.RectMax.xy) * float2(0.5f, -0.5f).xyxy + 0.5f).xwzy;
	float4 rectPixels = rect * cView.HZBDimensions.xyxy;
	float2 rectSize = abs(rectPixels.zw - rectPixels.xy);
	uint mip = ceil(log2(max(rectSize.x, rectSize.y)));

#if _SM_MAJ >= 6 && _SM_MIN >= 7
	bool isOccluded = hzbTexture.SampleCmpLevel(sLinearClampComparisonGreater, rect.xw, cullData.RectMax.z, mip) > 0;
#else
	float4 depths = 1;
	depths.x = hzbTexture.SampleLevel(sPointClamp, rect.xw, mip);
	depths.y = hzbTexture.SampleLevel(sPointClamp, rect.zw, mip);
	depths.z = hzbTexture.SampleLevel(sPointClamp, rect.zy, mip);
	depths.w = hzbTexture.SampleLevel(sPointClamp, rect.xy, mip);
	float depth = min(min3(depths.x, depths.y, depths.z), depths.w);
	bool isOccluded = cullData.RectMax.z < depth;
#endif

	return cullData.IsVisible && !isOccluded;
}
