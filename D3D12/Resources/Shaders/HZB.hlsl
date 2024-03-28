#include "Common.hlsli"
#include "HZB.hlsli"

struct PassParameters
{
    float2 DimensionsInv;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture2D<float> uHZB : register(u0);
Texture2D<float> tSource : register(t0);

globallycoherent RWStructuredBuffer<uint> uSpdGlobalAtomic : register(u1);
globallycoherent RWTexture2D<float> uDestination6 : register(u2);
RWTexture2D<float> uDestination[12] : register(u3);

[numthreads(16, 16, 1)]
void HZBInitCS(uint3 threadID : SV_DispatchThreadID)
{
	if(all(threadID == 0))
		uSpdGlobalAtomic[0] = 0;

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

ConstantBuffer<SpdConstants> cConstants : register(b0);

#define A_GPU 1
#define A_HLSL 1
#include "Interop/SPD/ffx_a.h"

// Define LDS variables
groupshared AF1 spdIntermediate[16][16];
groupshared AU1 spdCounter;

// if subgroup operations are not supported / can't use SM6.0
// #define SPD_NO_WAVE_OPERATIONS

// AU1 slice parameter is for Cube textures and texture2DArray
AF4 SpdLoadSourceImage(ASU2 tex, AU1 slice) { return uDestination[0][tex]; }

// SpdLoad() takes a 32-bit signed integer 2D coordinate and loads color.
AF4 SpdLoad(ASU2 tex, AU1 slice) { return uDestination6[tex]; }

// Define the store function
void SpdStore(ASU2 pix, AF4 value, AU1 mip, AU1 slice)
{
	if(mip == 5)
	{

		uDestination6[pix] = value.x;
		return;
	}

	uDestination[mip + 1][pix] = value.x;
}

// Define the atomic Counter increase function
void SpdIncreaseAtomicCounter(AU1 slice) { InterlockedAdd(uSpdGlobalAtomic[slice], 1, spdCounter); }
AU1 SpdGetAtomicCounter() { return spdCounter;}
void SpdResetAtomicCounter(AU1 slice) { uSpdGlobalAtomic[slice] = 0; }

// Define the LDS load and store functions
AF4 SpdLoadIntermediate(AU1 x, AU1 y) { return spdIntermediate[x][y]; }
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value) { spdIntermediate[x][y] = value.x; }

// HZB reduction function. Takes as input the four 2x2 values and returns 1 output value
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return min(v0, min(v1, min(v2, v3)));
}

#include "Interop/SPD/ffx_spd.h"

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
