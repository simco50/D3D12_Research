cbuffer ShaderParameters : register(b0)
{
    float4x4 cProjectionInverse;
    float2 cScreenDimensions;
    float2 padding;
    uint4 cNumThreadGroups;
    uint4 cNumThreads;
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

RWStructuredBuffer<Frustum> uOutFrustums : register(u0);

// Convert clip space coordinates to view space
float4 ClipToView( float4 clip )
{
    // View space position.
    float4 view = mul( cProjectionInverse, clip );
    // Perspective projection.
    view = view / view.w;
    return view;
}
 
// Convert screen space coordinates to view space.
float4 ScreenToView( float4 screen )
{
    // Convert to normalized texture coordinates
    float2 texCoord = screen.xy / cScreenDimensions;
    // Convert to clip space
    float4 clip = float4( float2( texCoord.x, 1.0f - texCoord.y ) * 2.0f - 1.0f, screen.z, screen.w );
    return ClipToView( clip );
}

Plane CalculatePlane(float3 a, float3 b, float3 c)
{
    float3 v0 = b - a;
    float3 v1 = c - a;
    
    Plane plane;
    plane.Normal = normalize(cross(v0, v1));
    plane.DistanceToOrigin = dot(plane.Normal, a);
    return plane;
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
    float3 eyePos = float3(0, 0, 0);

    // Compute the 4 corner points on the far clipping plane to use as the 
    // frustum vertices.
    float4 screenSpace[4];
    screenSpace[0] = float4( input.DispatchThreadId.xy * 16, 1.0f, 1.0f );
    screenSpace[1] = float4( float2( input.DispatchThreadId.x + 1, input.DispatchThreadId.y ) * 16, 1.0f, 1.0f );
    screenSpace[2] = float4( float2( input.DispatchThreadId.x, input.DispatchThreadId.y + 1 ) * 16, 1.0f, 1.0f );
    screenSpace[3] = float4( float2( input.DispatchThreadId.x + 1, input.DispatchThreadId.y + 1 ) * 16, 1.0f, 1.0f );

    float3 viewSpace[4];
    for(int i = 0; i < 4; ++i)
    {
        viewSpace[i] = ScreenToView(screenSpace[i]).xyz;
    }

    Frustum frustum;
    frustum.Left = CalculatePlane(eyePos, viewSpace[2], viewSpace[0]);
    frustum.Right = CalculatePlane(eyePos, viewSpace[1], viewSpace[3]);
    frustum.Top = CalculatePlane(eyePos, viewSpace[0], viewSpace[1]);
    frustum.Bottom = CalculatePlane(eyePos, viewSpace[3], viewSpace[2]);

    uint arrayIndex = input.DispatchThreadId.x + (input.DispatchThreadId.y * cNumThreads.x);
    uOutFrustums[arrayIndex] = frustum;
}