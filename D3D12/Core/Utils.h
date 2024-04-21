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

	inline std::string AddThousandsSeperator(int value)
	{
		std::string output;
		int absV = value > 0 ? value : -value;
		while (absV > 0)
		{
			output = Sprintf(absV > 1000 ? "%03d%s%s" : "%d%s%s", absV % 1000, output.empty() ? "" : ",", output);
			absV /= 1000;
		}
		if (value < 0)
			output = "-" + output;
		return output;
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
