cbuffer ShaderParameters : register(b0)
{
    float4x4 cView;
    float2 cScreenDimensions;
    float2 padding;
    uint4 cNumThreadGroups;
    uint4 cNumThreads;
}

struct Light
{
	int Enabled;
	float3 Position;
	float3 Direction;
	float Intensity;
	float4 Color;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	uint Type;
};

cbuffer LightData : register(b1)
{
    Light cLights[20];
}

struct Plane
{
    float3 Normal;
    float DistanceToOrigin;
};

struct Frustum
{
    Plane Left;
    Plane Right;
    Plane Top;
    Plane Bottom;
};

struct Sphere
{
    float3 Position;
    float Radius;
};

bool SphereInPlane(Sphere sphere, Plane plane)
{
    return dot(plane.Normal, sphere.Radius) - plane.DistanceToOrigin < -sphere.Radius;
}

bool SphereInFrustum(Sphere sphere, Frustum frustum, float depthNear, float depthFar)
{
    if(sphere.Position.z - sphere.Radius > depthNear || sphere.Position.z + sphere.Radius < depthFar)
    {
        return false;
    }

    if(SphereInPlane(sphere, frustum.Left))
    {
        return false;
    }
    if(SphereInPlane(sphere, frustum.Right))
    {
        return false;
    }
    if(SphereInPlane(sphere, frustum.Top))
    {
        return false;
    }
    if(SphereInPlane(sphere, frustum.Bottom))
    {
        return false;
    }
    return true;
}

StructuredBuffer<Frustum> tInFrustums : register(t0);
RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWTexture2D<uint2> uOutLightGrid : register(u2);

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

[numthreads(16, 16, 1)]
void CSMain(CS_INPUT input)
{
    int2 texCoord = input.DispatchThreadId.xy;
    if ( input.GroupIndex == 0 )
    {
        LightCount = 0;
        GroupFrustum = tInFrustums[input.GroupId.x + ( input.GroupId.y * cNumThreadGroups.x )];
    }
    GroupMemoryBarrierWithGroupSync();

    for(uint i = 0; i < 20; ++i)
    {
        Sphere sphere = { mul(float4(cLights[i].Position.xyz, 1), cView).xyz, cLights[i].Range };
        if ( i == 1/*SphereInFrustum( sphere, GroupFrustum, 0, 1000000000 )*/ )
        {
            AddLight( i );
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.GroupIndex == 0)
    {
        InterlockedAdd(uLightIndexCounter[0], LightCount, LightIndexStartOffset);
        uOutLightGrid[input.GroupId.xy] = uint2(LightIndexStartOffset, LightCount);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = input.GroupIndex; j < LightCount; j += 16 * 16)
    {
        uLightIndexList[LightIndexStartOffset + j] = LightList[j];
    }
}