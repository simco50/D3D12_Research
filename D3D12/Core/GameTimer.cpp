#include "stdafx.h"
#include "GameTimer.h"

__int64 GameTimer::m_BaseTime = 0;
__int64 GameTimer::m_PausedTime = 0;
__int64 GameTimer::m_StopTime = 0;
__int64 GameTimer::m_PrevTime = 0;
__int64 GameTimer::m_CurrTime = 0;

double GameTimer::m_SecondsPerCount = 0.0;
double GameTimer::m_DeltaTime = 0.016f;

bool GameTimer::m_IsStopped = false;

int GameTimer::m_Ticks = 0;;

GameTimer::GameTimer()
{

}

GameTimer::~GameTimer()
{
}

void GameTimer::Tick()
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

float GameTimer::GameTime()
{
	if (m_IsStopped)
	{
		return (float)(((m_StopTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}
	return (float)(((m_CurrTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
}

float GameTimer::DeltaTime()
{
	return (float)m_DeltaTime;
}

void GameTimer::Reset()
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

void GameTimer::Start()
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

void GameTimer::Stop()
{
	if(!m_IsStopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		m_StopTime = currTime;
		m_IsStopped = true;
	}
}