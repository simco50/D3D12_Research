#pragma once

/*
	Helper functions that accelerate atomic write operations between threads using wave operations.
*/

template<typename T>
void AtomicAddBuffer(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	InterlockedAdd(bufferResource[elementIndex], numValues, originalValue);
}

template<>
void AtomicAddBuffer(RWByteAddressBuffer bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	bufferResource.InterlockedAdd(elementIndex * 4, numValues, originalValue);
}


#define WAVE_OPS 1

#if WAVE_OPS

template<typename T>
void InterlockedAdd_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	uint count = WaveActiveCountBits(true) * numValues;
	if(WaveIsFirstLane())
		AtomicAddBuffer(bufferResource, elementIndex, count, originalValue);
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixCountBits(true);
}

template<typename T>
void InterlockedAdd_Varying_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	uint count = WaveActiveSum(numValues);
	if(WaveIsFirstLane())
		AtomicAddBuffer(bufferResource, elementIndex, count, originalValue);
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixSum(numValues);
}
#else

template<typename T>
void InterlockedAdd_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	AtomicAddBuffer(bufferResource, elementIndex, numValues, originalValue);
}

template<typename T>
void InterlockedAdd_Varying_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	AtomicAddBuffer(bufferResource, elementIndex, numValues, originalValue);
}

#endif
