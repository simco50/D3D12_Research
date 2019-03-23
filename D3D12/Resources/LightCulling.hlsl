#include "Common.hlsl"
#include "Constants.hlsl"

cbuffer ShaderParameters : register(b0)
{
    float4x4 cView;
    uint4 cNumThreadGroups;
    float4x4 cProjectionInverse;
    float2 cScreenDimensions;
}

cbuffer LightData : register(b1)
{
    Light cLights[LIGHT_COUNT];
}

Texture2D tDepthTexture : register(t0);
RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWTexture2D<uint2> uOutLightGrid : register(u2);

groupshared uint MinDepth;
groupshared uint MaxDepth;
groupshared Frustum GroupFrustum;
groupshared AABB GroupAABB;
groupshared uint LightCount;
groupshared uint LightIndexStartOffset;
groupshared uint LightList[1024];
#if SPLITZ_CULLING
groupshared uint DepthMask;
#endif

void AddLight(uint lightIndex)
{
    uint index;
    InterlockedAdd(LightCount, 1, index);
    LightList[index] = lightIndex;
}

bool SphereBehindPlane(Sphere sphere, Plane plane)
{
    return dot(plane.Normal, sphere.Position) - plane.DistanceToOrigin < -sphere.Radius;
}

bool PointBehindPlane(float3 p, Plane plane)
{
    return dot(plane.Normal, p) - plane.DistanceToOrigin < 0;
}

bool ConeBehindPlane(Cone cone, Plane plane)
{
    float3 furthestPointDirection = cross(cross(plane.Normal, cone.Direction), cone.Direction);
    float3 furthestPointOnCircle = cone.Tip + cone.Direction * cone.Height - furthestPointDirection * cone.Radius;
    return PointBehindPlane(cone.Tip, plane) && PointBehindPlane(furthestPointOnCircle, plane);
}

bool ConeInFrustum(Cone cone, Frustum frustum, float zNear, float zFar)
{
    Plane nearPlane, farPlane;
    nearPlane.Normal = float3(0, 0, 1);
    nearPlane.DistanceToOrigin = zNear;
    farPlane.Normal = float3(0, 0, -1);
    farPlane.DistanceToOrigin = -zFar;
 
    bool inside = !(ConeBehindPlane(cone, nearPlane) || ConeBehindPlane(cone, farPlane));
    for(int i = 0; i < 4 && inside; ++i)
    {
        inside = !ConeBehindPlane(cone, frustum.Planes[i]);
    }
    return inside;
}

bool SphereInFrustum(Sphere sphere, Frustum frustum, float depthNear, float depthFar)
{
    bool inside = !(sphere.Position.z + sphere.Radius < depthNear || sphere.Position.z - sphere.Radius > depthFar);
    for(int i = 0; i < 4 && inside; ++i)
    {
        inside = !SphereBehindPlane(sphere, frustum.Planes[i]);
    }
    return inside;
}

bool SphereInAABB(Sphere sphere, AABB aabb)
{
    float3 d = max(0, abs(aabb.Center - sphere.Position) - aabb.Extents);
    float distanceSq = dot(d, d);
    return distanceSq <= sphere.Radius * sphere.Radius;
}

struct CS_INPUT
{
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
};

uint CreateLightMask(float depthRangeMin, float depthRange, Sphere sphere)
{
    float fMin = sphere.Position.z - sphere.Radius;
    float fMax = sphere.Position.z + sphere.Radius;
    uint maskIndexStart = max(0, min(31, floor((fMin - depthRangeMin) * depthRange)));
    uint maskIndexEnd = max(0, min(31, floor((fMax - depthRangeMin) * depthRange)));

    uint mask = 0xFFFFFFFF;
    mask >>= 31 - (maskIndexEnd - maskIndexStart);
    mask <<= maskIndexStart;
    return mask;
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    int2 texCoord = input.DispatchThreadId.xy;
    float fDepth = tDepthTexture[texCoord].r;

    //Convert to uint because you can't used interlocked functions on floats
    uint depth = asuint(fDepth);

    //Initialize the groupshared data only on the first thread of the group
    if (input.GroupIndex == 0)
    {
        MinDepth = 0xffffffff;
        MaxDepth = 0;
        LightCount = 0;
#if SPLITZ_CULLING
        DepthMask = 0;
#endif
    }

    //Wait for thread 0 to finish with initializing the groupshared data
    GroupMemoryBarrierWithGroupSync();

    //Find the min and max depth values in the threadgroup
    InterlockedMin(MinDepth, depth);
    InterlockedMax(MaxDepth, depth);

    //Wait for all the threads to finish
    GroupMemoryBarrierWithGroupSync();

    float fMinDepth = asfloat(MinDepth);
    float fMaxDepth = asfloat(MaxDepth);

    if(input.GroupIndex == 0)
    {
        float3 viewSpace[8];
		viewSpace[0] = ScreenToView(float4(input.GroupId.xy * BLOCK_SIZE, fMinDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[1] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y) * BLOCK_SIZE, fMinDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[2] = ScreenToView(float4(float2(input.GroupId.x, input.GroupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[3] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[4] = ScreenToView(float4(input.GroupId.xy * BLOCK_SIZE, fMaxDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[5] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y) * BLOCK_SIZE, fMaxDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[6] = ScreenToView(float4(float2(input.GroupId.x, input.GroupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;
		viewSpace[7] = ScreenToView(float4(float2(input.GroupId.x + 1, input.GroupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0f), cScreenDimensions, cProjectionInverse).xyz;

        GroupFrustum.Planes[0] = CalculatePlane(float3(0, 0, 0), viewSpace[6], viewSpace[4]);
        GroupFrustum.Planes[1] = CalculatePlane(float3(0, 0, 0), viewSpace[5], viewSpace[7]);
        GroupFrustum.Planes[2] = CalculatePlane(float3(0, 0, 0), viewSpace[4], viewSpace[5]);
        GroupFrustum.Planes[3] = CalculatePlane(float3(0, 0, 0), viewSpace[7], viewSpace[6]);

        float3 minAABB = 1000000;
        float3 maxAABB = -1000000;
        [unroll]
        for(uint i = 0; i < 8; ++i)
        {
            minAABB = min(minAABB, viewSpace[i]);
            maxAABB = max(maxAABB, viewSpace[i]);
        }
        AABBFromMinMax(GroupAABB, minAABB, maxAABB);        
    }
    
    // Convert depth values to view space.
    float minDepthVS = ScreenToView(float4(0, 0, fMinDepth, 1), cScreenDimensions, cProjectionInverse).z;
    float maxDepthVS = ScreenToView(float4(0, 0, fMaxDepth, 1), cScreenDimensions, cProjectionInverse).z;
    float nearClipVS = ScreenToView(float4(0, 0, 0, 1), cScreenDimensions, cProjectionInverse).z;

#if SPLITZ_CULLING
    float depthVS = ScreenToView(float4(0, 0, fDepth, 1), cScreenDimensions, cProjectionInverse).z;
    float depthRange = 31.0f / (maxDepthVS - minDepthVS);
    uint cellIndex = max(0, min(31, floor((depthVS - minDepthVS) * depthRange)));
    InterlockedOr(DepthMask, 1 << cellIndex);
#endif

    // Clipping plane for minimum depth value 
    Plane minPlane;
    minPlane.Normal = float3(0.0f, 0.0f, 1.0f);
    minPlane.DistanceToOrigin = minDepthVS;

    GroupMemoryBarrierWithGroupSync();

    //Perform the light culling
    [loop]
    for(uint i = input.GroupIndex; i < LIGHT_COUNT; i += BLOCK_SIZE * BLOCK_SIZE)
    {
        Light light = cLights[i];

        switch(light.Type)
        {
            case LIGHT_DIRECTIONAL:
            {
                AddLight(i);
                break;
            }
            case LIGHT_POINT:
            {
                Sphere sphere = (Sphere)0;
                sphere.Radius = light.Range;
                sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;
                if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
                {
                    if(SphereInAABB(sphere, GroupAABB))
                    {
#if SPLITZ_CULLING
                        if(DepthMask & CreateLightMask(minDepthVS, depthRange, sphere))
#endif
                        {
                            AddLight(i);
                        }
                    }
                }
                break;
            }
            case LIGHT_SPOT:
            {
                Sphere sphere;
                sphere.Radius = light.Range * 0.5f / pow(cos(radians(light.SpotLightAngle / 2)), 2);
                sphere.Position = mul(float4(light.Position, 1), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;
                if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
                {
                    if(SphereInAABB(sphere, GroupAABB))
                    {
#if SPLITZ_CULLING
                        if(DepthMask & CreateLightMask(minDepthVS, depthRange, sphere))
#endif
                        {
                            AddLight(i);
                        }
                    }
                }

                break;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    //Populate the light grid only on the first thread in the group
    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], LightCount, LightIndexStartOffset);
        uOutLightGrid[input.GroupId.xy] = uint2(LightIndexStartOffset, LightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    //Distribute populating the light index light amonst threads in the thread group
    [loop]
    for (uint j = input.GroupIndex; j < LightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uLightIndexList[LightIndexStartOffset + j] = LightList[j];
    }
}