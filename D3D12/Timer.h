#pragma once

class Timer
{
public:
	Timer(std::wstring name) :
		m_Name(name)
	{
		__int64 freq;
		QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
		QueryPerformanceCounter(&m_Begin);

		m_SecondsPerCount = 1000.0f / (double)(freq);
	}

	~Timer()
	{
		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);

		double time = (double)((end.QuadPart - m_Begin.QuadPart) * m_SecondsPerCount);
		wcout << "[" << m_Name << "] Completed after " << time << " ms." << endl;
	}

private:
	std::wstring m_Name;
	LARGE_INTEGER m_Begin;
	double m_SecondsPerCount;
};