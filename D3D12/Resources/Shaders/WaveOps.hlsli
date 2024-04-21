#pragma once

/*
	Helper functions that accelerate atomic write operations between threads using wave operations.
*/

#define InterlockedAdd_WaveOps(bufferResource, elementIndex, numValues, originalValue) 			\
{																								\
	uint count = WaveActiveCountBits(true) * numValues;											\
	if(WaveIsFirstLane())																		\
		InterlockedAdd(bufferResource[elementIndex], count, originalValue);						\
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixCountBits(true);				\
}

#define InterlockedAdd_Varying_WaveOps(bufferResource, elementIndex, numValues, originalValue) 	\
{																								\
	uint count = WaveActiveSum(numValues);														\
	if(WaveIsFirstLane())																		\
		InterlockedAdd(bufferResource[elementIndex], count, originalValue);						\
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixSum(numValues);				\
}
