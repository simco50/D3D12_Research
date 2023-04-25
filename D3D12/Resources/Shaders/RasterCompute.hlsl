#include "Common.hlsli"
#include "Random.hlsli"

struct Params
{
    uint InstanceID;
    uint NumPrimitives;
};

ConstantBuffer<Params> cParams : register(b0);

RWTexture2D<uint64_t> uOutput : register(u0);

Texture2D<uint64_t> tVisibility : register(t0);
RWTexture2D<float4> uColorOutput : register(u0);

void WritePixel(uint2 coord, uint value, float depth)
{
    uint depthInt = asuint(depth);
    uint64_t packed = ((uint64_t)depthInt << 32u) | value;
    InterlockedMax(uOutput[coord], packed);
}

struct InterpolantsVSToPS
{
	float4 Position;
};

float EdgeFunction(const float2 a, const float2 b, const float2 c)
{
	 return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
};

[numthreads(64, 1, 1)]
void RasterizeCS(uint threadID : SV_DispatchThreadID)
{
    if(threadID >= cParams.NumPrimitives)
        return;

    /*
        Vertex shader
    */
    InstanceData instance = GetInstance(cParams.InstanceID);
	MeshData mesh = GetMesh(instance.MeshIndex);

    uint3 vertexIDs = GetPrimitive(mesh, threadID);

    float3 p0 = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexIDs[0], mesh.PositionsOffset)).xyz;
    float3 p1 = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexIDs[1], mesh.PositionsOffset)).xyz;
    float3 p2 = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexIDs[2], mesh.PositionsOffset)).xyz;

    float4 csP0 = mul(mul(float4(p0, 1), instance.LocalToWorld), cView.ViewProjection);
    float4 csP1 = mul(mul(float4(p1, 1), instance.LocalToWorld), cView.ViewProjection);
    float4 csP2 = mul(mul(float4(p2, 1), instance.LocalToWorld), cView.ViewProjection);

    /*
        Perspective divide
    */
    csP0.xyz /= csP0.w;
    csP1.xyz /= csP1.w;
    csP2.xyz /= csP2.w;

    /*
        Viewport transform
    */
    float2 vpP0 = (float2(csP0.x, -csP0.y) * 0.5f + 0.5f) * cView.ViewportDimensions;
    float2 vpP1 = (float2(csP1.x, -csP1.y) * 0.5f + 0.5f) * cView.ViewportDimensions;
    float2 vpP2 = (float2(csP2.x, -csP2.y) * 0.5f + 0.5f) * cView.ViewportDimensions;

    /*
        Viewport clamp
    */
    float2 minBounds = Min(vpP0, vpP1, vpP2);
    float2 maxBounds = Max(vpP0, vpP1, vpP2);

	minBounds = Max(float2(0.0f, 0.0f), minBounds);
	maxBounds = Min(cView.ViewportDimensions - 1, maxBounds);

    float rcpDet = rcp(EdgeFunction(vpP0, vpP1, vpP2));

 	float A01 = vpP0.y - vpP1.y, B01 = vpP1.x - vpP0.x;
    float A12 = vpP1.y - vpP2.y, B12 = vpP2.x - vpP1.x;
    float A20 = vpP2.y - vpP0.y, B20 = vpP0.x - vpP2.x;

	float2 p = minBounds;
	float w0_row = EdgeFunction(vpP1, vpP2, p);
	float w1_row = EdgeFunction(vpP2, vpP0, p);
	float w2_row = EdgeFunction(vpP0, vpP1, p);

    /*
        Raster loop
    */
    for(uint y = (uint)minBounds.y; y <= (uint)maxBounds.y; ++y)
    {
		float w0 = w0_row;
		float w1 = w1_row;
		float w2 = w2_row;

        for(uint x = (uint)minBounds.x; x <= (uint)maxBounds.x; ++x)
        {
            float2 pixel = float2(x, y) + 0.5f;
            if(Min(w0, w1, w2) >= 0.0f)
            {
                float z = csP0.z * w0 + csP1.z * w1 + csP2.z * w2;
				z *= rcpDet;

                /*
                    Pixel shader
                */

                // nop

                /*
                    Output merger
                */
                WritePixel(uint2(x, y), threadID, 1);
            }

			w0 += A12;
			w1 += A20;
			w2 += A01;
        }

		w0_row += B12;
		w1_row += B20;
		w2_row += B01;
    }
}


[numthreads(16, 16, 1)]
void ResolveVisBufferCS(uint3 threadID : SV_DispatchThreadID)
{
    uint2 pixel = threadID.xy;
    uint64_t visibility = tVisibility[pixel];

    uint triangleID = (uint)(visibility & 0xFFFFFFFF);
    uint seed = SeedThread(triangleID);
    uColorOutput[pixel] = float4(RandomColor(seed), 1.0f);
}
