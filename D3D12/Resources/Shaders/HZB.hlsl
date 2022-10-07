#include "Common.hlsli"
#include "HZB.hlsli"

struct PassParameters
{
    float2 DimensionsInv;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture2D<float> uHZB : register(u0);
Texture2D<float> tSource : register(t0);

[numthreads(16, 16, 1)]
void HZBInitCS(uint3 threadID : SV_DispatchThreadID)
{
    float2 uv = ((float2)threadID.xy + 0.5f) * cPass.DimensionsInv;
    float4 depths = tSource.Gather(sPointClamp, uv);
    float minDepth = min(min(min(depths.x, depths.y), depths.z), depths.w);
    uHZB[threadID.xy] = minDepth;
}

// SPD HZB

struct SpdConstants
{
    uint Mips;
    uint NumWorkGroups;
    float2 WorkGroupOffset;
};

globallycoherent RWByteAddressBuffer uSpdGlobalAtomic : register(u0);
RWTexture2D<float4> uSource : register(u1);
globallycoherent RWTexture2D<float4> uDestination[12] : register(u2);

ConstantBuffer<SpdConstants> cConstants : register(b0);

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

// Define LDS variables
groupshared AF4 spdIntermediate[16][16];
groupshared AU1 spdCounter;

// if subgroup operations are not supported / can't use SM6.0
// #define SPD_NO_WAVE_OPERATIONS

// AU1 slice parameter is for Cube textures and texture2DArray
AF4 SpdLoadSourceImage(ASU2 tex, AU1 slice) { return uSource[tex]; }

// SpdLoad() takes a 32-bit signed integer 2D coordinate and loads color.
AF4 SpdLoad(ASU2 tex, AU1 slice) { return uDestination[5][tex]; }

// Define the store function
void SpdStore(ASU2 pix, AF4 value, AU1 mip, AU1 slice) { uDestination[mip][pix] = value; }

// Define the atomic Counter increase function
void SpdIncreaseAtomicCounter(AU1 slice) { uSpdGlobalAtomic.InterlockedAdd(0, 1, spdCounter); }
AU1 SpdGetAtomicCounter() { return spdCounter;}
void SpdResetAtomicCounter(AU1 slice) { uSpdGlobalAtomic.Store(0, 0); }

// Define the LDS load and store functions
AF4 SpdLoadIntermediate(AU1 x, AU1 y) { return spdIntermediate[x][y]; }
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value) { spdIntermediate[x][y] = value; }

// HZB reduction function. Takes as input the four 2x2 values and returns 1 output value
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return min(v0, min(v1, min(v2, v3)));
}

#include "ffx_spd.h"

[numthreads(256, 1, 1)]
void HZBCreateCS(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex) 
{
    SpdDownsample(
        AU2(WorkGroupId.xy), 
        AU1(LocalThreadIndex),
        AU1(cConstants.Mips), 
        AU1(cConstants.NumWorkGroups), 
        AU1(WorkGroupId.z)
        );
}