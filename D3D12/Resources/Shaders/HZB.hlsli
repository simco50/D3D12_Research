#include "Common.hlsli"

struct HZBCullData
{
	bool IsVisible;
	float3 RectMin;
	float3 RectMax;
	uint MipLevel;
	float2 MinUV;
	float2 MaxUV;
};

HZBCullData HZBCull(float3 aabbCenter, float3 aabbExtents, Texture2D<float> hzbTexture)
{
	HZBCullData data = (HZBCullData)0;
	data.IsVisible = true;

	// Clip space AABB
	float4 dx = mul(float4(aabbExtents.x * 2, 0, 0, 0), cView.ViewProjectionPrev);
	float4 dy = mul(float4(0, aabbExtents.y * 2, 0, 0), cView.ViewProjectionPrev);
	float4 dz = mul(float4(0, 0, aabbExtents.z * 2, 0), cView.ViewProjectionPrev);
	float4 pos000 = mul(float4(aabbCenter - aabbExtents, 1), cView.ViewProjectionPrev);
	float4 pos100 = pos000 + dx;
	float4 pos010 = pos000 + dy;
	float4 pos110 = pos010 + dx;
	float4 pos001 = pos000 + dz;
	float4 pos101 = pos100 + dz;
	float4 pos011 = pos010 + dz;
	float4 pos111 = pos110 + dz;

	bool2 frustumMin = and(pos000.xy > pos000.w, and(pos001.xy > pos001.w, and(pos010.xy > pos010.w,
						and(pos011.xy > pos011.w, and(pos100.xy > pos100.w, and(pos101.xy > pos101.w,
						and(pos110.xy > pos110.w, pos111.xy > pos111.w)))))));
	bool2 frustumMax = and(-pos000.xy > pos000.w, and(-pos001.xy > pos001.w, and(-pos010.xy > pos010.w,
						and(-pos011.xy > pos011.w, and(-pos100.xy > pos100.w, and(-pos101.xy > pos101.w,
						and(-pos110.xy > pos110.w, -pos111.xy > pos111.w)))))));

	data.IsVisible &= !any(or(frustumMin, frustumMax));

	if(data.IsVisible)
	{
		float minW = min3(min3(pos000.w, pos100.w, pos010.w),
					min3(pos110.w, pos001.w, pos101.w),
					min(pos011.w, pos111.w));

		float maxW = max3(max3(pos000.w, pos100.w, pos010.w),
						max3(pos110.w, pos001.w, pos101.w),
						max(pos011.w, pos111.w));

		// Rect takes up the whole screen, so the rect will be wrong
		if(!(minW <= 0 && maxW > 0))
		{
			// Screen space AABB
			float3 ssPos000 = pos000.xyz / pos000.w;
			float3 ssPos100 = pos100.xyz / pos100.w;
			float3 ssPos010 = pos010.xyz / pos010.w;
			float3 ssPos110 = pos110.xyz / pos110.w;
			float3 ssPos001 = pos001.xyz / pos001.w;
			float3 ssPos101 = pos101.xyz / pos101.w;
			float3 ssPos011 = pos011.xyz / pos011.w;
			float3 ssPos111 = pos111.xyz / pos111.w;

			data.RectMin = min3(min3(ssPos000, ssPos100, ssPos010),
								min3(ssPos110, ssPos001, ssPos101),
								min3(ssPos011, ssPos111, float3(1, 1, 1)));

			data.RectMax = max3(max3(ssPos000, ssPos100, ssPos010),
								max3(ssPos110, ssPos001, ssPos101),
								max3(ssPos011, ssPos111, float3(-1, -1, -1)));

			float4 rect = saturate(float4(data.RectMin.xy, data.RectMax.xy) * float2(0.5f, -0.5f).xyxy + 0.5f).xwzy;
			float4 rectPixels = rect * cView.HZBDimensions.xyxy;
			float2 rectSize = abs(rectPixels.zw - rectPixels.xy);
			uint mip = ceil(log2(max(rectSize.x, rectSize.y)));

			data.MipLevel = mip;
			data.MinUV = rect.xy;
			data.MaxUV = rect.zw;

			float4 depths = 1;
			depths.x = hzbTexture.SampleLevel(sPointClamp, rect.xw, mip);
			depths.y = hzbTexture.SampleLevel(sPointClamp, rect.zw, mip);
			depths.z = hzbTexture.SampleLevel(sPointClamp, rect.zy, mip);
			depths.w = hzbTexture.SampleLevel(sPointClamp, rect.xy, mip);
			float depth = min(min3(depths.x, depths.y, depths.z), depths.w);
			data.IsVisible &= data.RectMax.z >= depth;
		}
	}

	return data;
}
