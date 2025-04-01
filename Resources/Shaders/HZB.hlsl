#include "Common.hlsli"
#include "HZB.hlsli"

struct InitHZBParams
{
	float2 DimensionsInv;
	RWTexture2DH<float> HZB;
	Texture2DH<float> Source;
};
DEFINE_CONSTANTS(InitHZBParams, 0);

[numthreads(16, 16, 1)]
void HZBInitCS(uint3 threadID : SV_DispatchThreadID)
{
	InitHZBParams params = cInitHZBParams;
	float2 uv = TexelToUV(threadID.xy, params.DimensionsInv);
	float4 depths = params.Source.Get().Gather(sPointClamp, uv);
	float minDepth = min(min(min(depths.x, depths.y), depths.z), depths.w);
	params.HZB.Store(threadID.xy, minDepth);
}

// SPD HZB

struct SPDConstants
{
	uint Mips;
	uint NumWorkGroups;
	float2 WorkGroupOffset;
	RWTypedBufferH<uint> SpdGlobalAtomic;
	RWTexture2DH<float> Destination6;
	RWTexture2DH<float> Destination[12];
};
DEFINE_CONSTANTS(SPDConstants, 0);

#define A_GPU 1
#define A_HLSL 1
#include "Interop/SPD/ffx_a.h"

// Define LDS variables
groupshared AF1 spdIntermediate[16][16];
groupshared AU1 spdCounter;

// if subgroup operations are not supported / can't use SM6.0
// #define SPD_NO_WAVE_OPERATIONS

// AU1 slice parameter is for Cube textures and texture2DArray
AF4 SpdLoadSourceImage(ASU2 tex, AU1 slice) 
{
	return cSPDConstants.Destination[0][tex]; 
}

// SpdLoad() takes a 32-bit signed integer 2D coordinate and loads color.
AF4 SpdLoad(ASU2 tex, AU1 slice) 
{
	RWTexture2D<float> destination = cSPDConstants.Destination6.Get();
	return destination[tex]; 
}

// Define the store function
void SpdStore(ASU2 pix, AF4 value, AU1 mip, AU1 slice)
{
	if(mip == 5)
	{
		RWTexture2D<float> destination = cSPDConstants.Destination6.Get();
		destination[pix] = value.x;
		return;
	}

	cSPDConstants.Destination[mip + 1].Store(pix, value.x);
}

// Define the atomic Counter increase function
void SpdIncreaseAtomicCounter(AU1 slice) 
{ 
	RWBuffer<uint> counter = cSPDConstants.SpdGlobalAtomic.Get();
	InterlockedAdd(counter[slice], 1, spdCounter); 
}

AU1 SpdGetAtomicCounter()
{
	return spdCounter;
}

void SpdResetAtomicCounter(AU1 slice) 
{
	RWBuffer<uint> counter = cSPDConstants.SpdGlobalAtomic.Get();
	counter[slice] = 0; 
}

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
	SPDConstants params = cSPDConstants;
	SpdDownsample(
		AU2(WorkGroupId.xy),
		AU1(LocalThreadIndex),
		AU1(params.Mips),
		AU1(params.NumWorkGroups),
		AU1(WorkGroupId.z)
		);
}
