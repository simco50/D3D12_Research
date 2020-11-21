#include "TonemappingCommon.hlsli"
#include "Color.hlsli"

#define HISTORY_REJECT_NONE             0
#define HISTORY_REJECT_CLAMP            1
#define HISTORY_REJECT_CLIP             2 // [Karis14]
#define HISTORY_REJECT_VARIANCE_CLIP    3 // [Salvi16]

#define HISTORY_RESOLVE_BILINEAR        0
#define HISTORY_RESOLVE_CATMULL_ROM     1

#define COLOR_SPACE_RGB                 0
#define COLOR_SPACE_YCOCG               1 // [Karis14]

#define MIN_BLEND_FACTOR                0.05
#define MAX_BLEND_FACTOR                0.12

#ifndef TAA_TEST
#define TAA_TEST 0
#endif

#define TAA_COLOR_SPACE             COLOR_SPACE_YCOCG           // Color space to use for neighborhood clamping
#define TAA_HISTORY_REJECT_METHOD   HISTORY_REJECT_CLIP         // Use neighborhood clipping to reject history samples
#define TAA_RESOLVE_METHOD          HISTORY_RESOLVE_CATMULL_ROM // History resolve filter
#define TAA_REPROJECT               1                           // Use per pixel velocity to reproject
#define TAA_TONEMAP                 0                           // Tonemap before resolving history to prevent high luminance pixels from overpowering
#define TAA_AABB_ROUNDED            1                           // Use combine 3x3 neighborhood with plus-pattern neighborhood
#define TAA_VELOCITY_CORRECT        0                           // Reduce blend factor when the subpixel motion is high to reduce blur under motion
#define TAA_DEBUG_RED_HISTORY       0
#define TAA_LUMINANCE_WEIGHT        0                           // [Lottes]
#define TAA_DILATE_VELOCITY         0

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
				"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

struct ShaderParameters
{
    float2 InvScreenDimensions;
    float2 Jitter;
};

ConstantBuffer<ShaderParameters> cParameters : register(b0);

SamplerState sPointSampler : register(s0);
SamplerState sLinearSampler : register(s1);

Texture2D tVelocity : register(t0);
Texture2D tPreviousColor : register(t1);
Texture2D tCurrentColor : register(t2);
Texture2D tDepth : register(t3);

RWTexture2D<float4> uOutColor : register(u0);

//Temporal Reprojection in Inside
float4 ClipAABB(float3 aabb_min, float3 aabb_max, float4 p, float4 q)
{
    // note: only clips towards aabb center (but fast!)
    float3 p_clip = 0.5 * (aabb_max + aabb_min);
    float3 e_clip = 0.5 * (aabb_max - aabb_min) + 0.00000001f;

    float4 v_clip = q - float4(p_clip, p.w);
    float3 v_unit = v_clip.xyz / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
    {
        return float4(p_clip, p.w) + v_clip / ma_unit;
    }
    else
    {
        return q;// point inside aabb
    }
}

float3 TransformColor(float3 color)
{
    #if TAA_COLOR_SPACE == COLOR_SPACE_RGB
    return color;
#elif TAA_COLOR_SPACE == COLOR_SPACE_YCOCG
    return RGB_to_YCoCg(color);
#else
    #error No color space defined
#endif
}

float3 ResolveColor(float3 color)
{
#if TAA_COLOR_SPACE == COLOR_SPACE_RGB
    return color;
#elif TAA_COLOR_SPACE == COLOR_SPACE_YCOCG
    return YCoCg_to_RGB(color);
#else
    #error No color space defined
#endif
}

float3 SampleColor(Texture2D tex, SamplerState textureSampler, float2 uv)
{
    return TransformColor(tex.SampleLevel(textureSampler, uv, 0).rgb);
}

// The following code is licensed under the MIT license: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
float4 SampleTextureCatmullRom(in Texture2D<float4> tex, in SamplerState textureSampler, in float2 uv, in float2 texSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0f;
    result += tex.SampleLevel(textureSampler, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result += tex.SampleLevel(textureSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(textureSampler, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result += tex.SampleLevel(textureSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(textureSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(textureSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(textureSampler, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result += tex.SampleLevel(textureSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(textureSampler, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return float4(TransformColor(result.rgb), result.a);
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(uint3 ThreadId : SV_DISPATCHTHREADID)
{
    const float2 dxdy = cParameters.InvScreenDimensions;
    uint2 pixelIndex = ThreadId.xy;
    float2 texCoord = dxdy * ((float2)pixelIndex + 0.5f);
    float2 dimensions;
    tCurrentColor.GetDimensions(dimensions.x, dimensions.y);

    float3 cc = SampleColor(tCurrentColor, sPointSampler, texCoord);
    float3 currColor = cc;

#if TAA_HISTORY_REJECT_METHOD != HISTORY_REJECT_NONE
    // Get a 3x3 neighborhood to clip/clamp against
    float3 lt = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(-dxdy.x, -dxdy.y));
    float3 ct = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(0, -dxdy.y));
    float3 rt = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(dxdy.x, -dxdy.y));
    float3 lc = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(-dxdy.x, 0));
    float3 rc = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(dxdy.x, 0));
    float3 lb = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(-dxdy.x, dxdy.y));
    float3 cb = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(0, dxdy.y));
    float3 rb = SampleColor(tCurrentColor, sPointSampler, texCoord + float2(dxdy.x, dxdy.y));

#if TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLAMP || TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLIP
    float3 aabb_min = min(lt, min(ct, min(rt, min(lc, min(cc, min(rc, min(lb, min(cb, rb))))))));
    float3 aabb_max = max(lt, max(ct, max(rt, max(lc, max(cc, max(rc, max(lb, max(cb, rb))))))));
    float3 aabb_avg = (lt + ct + rt + lc + cc + rc + lb + cb + rb) / 9.0f;
#if TAA_AABB_ROUNDED 
    //[Karis14] - Average 3x3 neighborhoord with 5 sample plus pattern neighborhood to remove 'filtered' look
    float3 aabb_min2 = min(min(min(min(lc, cc), ct), rc), cb);
    float3 aabb_max2 = max(max(max(max(lc, cc), ct), rc), cb);
    float3 aabb_avg2 = (lc + cc + ct + rc + cb) / 5.0f;
    aabb_min = (aabb_min + aabb_min2) * 0.5f;
    aabb_max = (aabb_max + aabb_max2) * 0.5f;
    aabb_avg = (aabb_avg + aabb_avg2) * 0.5f;
#endif // TAA_AABB_ROUNDED

#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_VARIANCE_CLIP
    // [Salvi16] - Use first and second moment to clip history color
    float3 m1 = lt + ct + rt + lc + cc + rc + lb + cb + rb;
    float3 m2 = Square(lt) + Square(ct) + Square(rt) + Square(lc) + Square(cc) + Square(rc) + Square(lb) + Square(cb) + Square(rb);
    float3 mu = m1 / 9.0f;
    float3 sigma = sqrt(m2 / 9.0f - mu * mu);
    const float gamma = 1.0f;
    float3 aabb_min = mu - gamma * sigma;
    float3 aabb_max = mu + gamma * sigma;
    float3 aabb_avg = mu;
#endif // TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLAMP || TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLIP

#endif // TAA_HISTORY_REJECT_METHOD != HISTORY_REJECT_NONE

    float2 uvReproj = texCoord;

#if TAA_REPROJECT
    float depth = tDepth.SampleLevel(sPointSampler, uvReproj, 0).r;

#if TAA_DILATE_VELOCITY
    // [Karis14] - Use closest pixel to move edge along
    const float crossDilation = 2;
    float4 crossDepths;
    crossDepths.x = tDepth.SampleLevel(sPointSampler, uvReproj + float2(-crossDilation, -crossDilation) * dxdy, 0).r;
    crossDepths.y = tDepth.SampleLevel(sPointSampler, uvReproj + float2(crossDilation, -crossDilation) * dxdy, 0).r;
    crossDepths.z = tDepth.SampleLevel(sPointSampler, uvReproj + float2(-crossDilation, crossDilation) * dxdy, 0).r;
    crossDepths.w = tDepth.SampleLevel(sPointSampler, uvReproj + float2(crossDilation, crossDilation) * dxdy, 0).r;
    if(crossDepths.x > depth)
    {
        depth = crossDepths.x;
        uvReproj = texCoord + float2(-crossDilation, -crossDilation) * dxdy;
    }
    if(crossDepths.y > depth)
    {
        depth = crossDepths.y;
        uvReproj = texCoord + float2(crossDilation, -crossDilation) * dxdy;
    }
    if(crossDepths.z > depth)
    {
        depth = crossDepths.z;
        uvReproj = texCoord + float2(-crossDilation, crossDilation) * dxdy;
    }
    if(crossDepths.w > depth)
    {
        depth = crossDepths.w;
        uvReproj = texCoord + float2(crossDilation, crossDilation) * dxdy;
    }
#endif // TAA_DILATE_VELOCITY

    float2 velocity = tVelocity.SampleLevel(sPointSampler, uvReproj, 0).xy;
    uvReproj = texCoord + velocity;
#endif // TAA_REPROJECT

#if TAA_RESOLVE_METHOD == HISTORY_RESOLVE_CATMULL_ROM 
    // [Karis14] Cubic filter to avoid blurry result from billinear filter
    float3 prevColor = SampleTextureCatmullRom(tPreviousColor, sLinearSampler, uvReproj, dimensions).rgb;
#elif TAA_RESOLVE_METHOD == HISTORY_RESOLVE_BILINEAR
    float3 prevColor = SampleColor(tPreviousColor, sLinearSampler, uvReproj);
#endif // TAA_RESOLVE_METHOD

#if TAA_DEBUG_RED_HISTORY
    // DEBUG: Use red history to debug how correct neighborhood clamp is
    prevColor = TransformColor(float3(1,0,0));
#endif // TAA_DEBUG_RED_HISTORY

#if TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLAMP
    prevColor = clamp(prevColor, aabb_min, aabb_max);
#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_CLIP //Karis Siggraph 2014 - Clip instead of clamp
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#elif TAA_HISTORY_REJECT_METHOD == HISTORY_REJECT_VARIANCE_CLIP
    prevColor = ClipAABB(aabb_min, aabb_max, float4(aabb_avg, 1), float4(prevColor, 1)).xyz;
#endif // TAA_HISTORY_REJECT_METHOD

    float blendFactor = MIN_BLEND_FACTOR;

#if TAA_VELOCITY_CORRECT 
    // [Xu16] Reduce blend factor when the motion is more subpixel
	float subpixelCorrection = frac(max(abs(velocity.x) * dimensions.x, abs(velocity.y) * dimensions.y)) * 0.5f;
    blendFactor = saturate(lerp(blendFactor, 0.8f, subpixelCorrection));
#endif // TAA_VELOCITY_CORRECT

#if TAA_LUMINANCE_WEIGHT 
    // [Lottes] Feedback weight from unbiased luminance diff
#if TAA_COLOR_SPACE == COLOR_SPACE_RGB
    float lum0 = GetLuminance(currColor);
    float lum1 = GetLuminance(prevColor);
#else
    float lum0 = currColor.x;
    float lum1 = prevColor.x;
#endif
    float unbiased_diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2));
    float unbiased_weight = unbiased_diff;
    float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
    blendFactor = lerp(MIN_BLEND_FACTOR, MAX_BLEND_FACTOR, blendFactor);
#endif

#if TAA_TONEMAP
    currColor = Reinhard(currColor);
    prevColor = Reinhard(prevColor);
#endif

    currColor = lerp(prevColor, currColor, blendFactor);

#if TAA_TONEMAP
    currColor = InverseReinhard(currColor);
#endif

    currColor = ResolveColor(currColor);

    uOutColor[pixelIndex] = float4(currColor, 1);
}