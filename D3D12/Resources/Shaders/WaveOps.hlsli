#pragma once

#define WAVE_OPS 1

#if WAVE_OPS

template<typename T>
void InterlockedAdd_WaveOps(T bufferResource, uint elementIndex, out uint originalValue)
{
	uint numValues = WaveActiveCountBits(true);
	if(WaveIsFirstLane())
		InterlockedAdd(bufferResource[elementIndex], numValues, originalValue);
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixCountBits(true);
}

template<typename T>
void InterlockedAdd_Varying_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	uint count = WaveActiveSum(numValues);
	if(WaveIsFirstLane())
		InterlockedAdd(bufferResource[elementIndex], count, originalValue);
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixSum(numValues);
}
#else

template<typename T>
void InterlockedAdd_WaveOps(T bufferResource, uint elementIndex, out uint originalValue)
{
	InterlockedAdd(bufferResource[elementIndex], 1, originalValue);
}

template<typename T>
void InterlockedAdd_Varying_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	InterlockedAdd(bufferResource[elementIndex], numValues, originalValue);
}

#endif
