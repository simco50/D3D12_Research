#pragma once

namespace Utils
{
	struct ForceFunctionToBeLinked
	{
		ForceFunctionToBeLinked(const void* p) { SetLastError(PtrToInt(p)); }
	};

	inline std::string GetTimeString()
	{
		SYSTEMTIME time;
		GetSystemTime(&time);
		return Sprintf("%d_%02d_%02d__%02d_%02d_%02d_%d",
			time.wYear, time.wMonth, time.wDay,
			time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	}

	class TimeScope
	{
	public:
		TimeScope()
		{
			QueryPerformanceFrequency(&m_Frequency);
			QueryPerformanceCounter(&m_StartTime);
		}

		float Stop()
		{
			LARGE_INTEGER endTime;
			QueryPerformanceCounter(&endTime);
			return (float)((double)endTime.QuadPart - m_StartTime.QuadPart) / m_Frequency.QuadPart;
		}

	private:
		LARGE_INTEGER m_StartTime, m_Frequency;
	};

}
