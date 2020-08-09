#include "stdafx.h"
#include "Time.h"

__int64 Time::m_BaseTime = 0;
__int64 Time::m_PausedTime = 0;
__int64 Time::m_StopTime = 0;
__int64 Time::m_PrevTime = 0;
__int64 Time::m_CurrTime = 0;

double Time::m_SecondsPerCount = 0.0;
double Time::m_DeltaTime = 0.016f;

bool Time::m_IsStopped = false;

int Time::m_Ticks = 0;;

Time::Time()
{

}

Time::~Time()
{
}

void Time::Tick()
{
	if(m_IsStopped)
	{
		m_DeltaTime = 0.0f;
		return;
	}
	++m_Ticks;

	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	m_DeltaTime = (currTime - m_PrevTime) * m_SecondsPerCount;
	m_CurrTime = currTime;
	m_PrevTime = m_CurrTime;

	if (m_DeltaTime < 0.0f)
	{
		m_DeltaTime = 0.0f;
	}
}

float Time::TotalTime()
{
	if (m_IsStopped)
	{
		return (float)(((m_StopTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}
	return (float)(((m_CurrTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
}

float Time::DeltaTime()
{
	return (float)m_DeltaTime;
}

void Time::Reset()
{
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	m_SecondsPerCount = 1.0 / (double)(countsPerSec);

	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	m_BaseTime = currTime;
	m_PrevTime = currTime;
	m_StopTime = 0;
	m_IsStopped = false;
}

void Time::Start()
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

	if(m_IsStopped)
	{
		m_PausedTime += (startTime - m_StopTime);
		m_PrevTime = startTime;

		m_StopTime = 0;
		m_IsStopped = false;
	}
}

void Time::Stop()
{
	if(!m_IsStopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		m_StopTime = currTime;
		m_IsStopped = true;
	}
}