#pragma once
class Time
{
public:
	Time();
	~Time();

	static float TotalTime();
	static float DeltaTime();

	static void Reset();
	static void Start();
	static void Stop();
	static void Tick();
	static bool IsPaused() { return m_IsStopped; }
	static int Ticks() { return m_Ticks; }

private:
	static double m_SecondsPerCount;
	static double m_DeltaTime;

	static __int64 m_BaseTime;
	static __int64 m_PausedTime;
	static __int64 m_StopTime;
	static __int64 m_PrevTime;
	static __int64 m_CurrTime;

	static bool m_IsStopped;

	static int m_Ticks;
};

