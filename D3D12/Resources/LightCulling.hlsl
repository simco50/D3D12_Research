#include "Common.hlsl"
#include "Constants.hlsl"

cbuffer ShaderParameters : register(b0)
{
    float4x4 cView;
    uint4 cNumThreadGroups;
    float4x4 cProjectionInverse;
}

cbuffer LightData : register(b1)
{
    Light cLights[LIGHT_COUNT];
}

StructuredBuffer<Frustum> tInFrustums : register(t0);
Texture2D tDepthTexture : register(t1);
RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWTexture2D<uint2> uOutLightGrid : register(u2);

groupshared uint MinDepth;
groupshared uint MaxDepth;
groupshared Frustum GroupFrustum;
groupshared uint LightCount;
groupshared uint LightIndexStartOffset;
groupshared uint LightList[1024];

void AddLight(uint lightIndex)
{
    uint index;
    InterlockedAdd(LightCount, 1, index);
    LightList[index] = lightIndex;
}

struct CS_INPUT
{
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
};

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    int2 texCoord = input.DispatchThreadId.xy;
    float fDepth = tDepthTexture.Load(int3(texCoord, 0)).r;

    //Convert to uint because you can't used interlocked functions on floats
    uint depth = asuint(fDepth);

    //Initialize the groupshared data only on the first thread of the group
    if ( input.GroupIndex == 0 )
    {
        MinDepth = 0xffffffff;
        MaxDepth = 0;
        LightCount = 0;
        GroupFrustum = tInFrustums[input.GroupId.x + (input.GroupId.y * cNumThreadGroups.x)];
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
    
    // Convert depth values to view space.
    float minDepthVS = ClipToView(float4(0, 0, fMinDepth, 1), cProjectionInverse).z;
    float maxDepthVS = ClipToView(float4(0, 0, fMaxDepth, 1), cProjectionInverse).z;
    float nearClipVS = ClipToView(float4(0, 0, 0, 1), cProjectionInverse).z;
    
    // Clipping plane for minimum depth value 
    Plane minPlane;
    minPlane.Normal = float3(0.0f, 0.0f, 1.0f);
    minPlane.DistanceToOrigin = minDepthVS;

    //Perform the light culling
    for(uint i = input.GroupIndex; i < LIGHT_COUNT; i += BLOCK_SIZE * BLOCK_SIZE)
    {
        if(cLights[i].Enabled)
        {
            switch(cLights[i].Type)
            {
                case 1:
                    Sphere sphere;
                    sphere.Radius = cLights[i].Range;
                    //Need the light position in view space
                    sphere.Position = mul(float4(cLights[i].Position, 1.0f), cView).xyz;
                    if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
                    {
                        if(!SphereBehindPlane(sphere, minPlane))
                        {
                            AddLight(i);
                        }
                    }
                    break;
                default:
                    AddLight(i);
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
    for (uint j = input.GroupIndex; j < LightCount; j += BLOCK_SIZE * BLOCK_SIZE)
    {
        uLightIndexList[LightIndexStartOffset + j] = LightList[j];
    }
}