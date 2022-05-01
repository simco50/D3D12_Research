#pragma once

namespace Utils
{
	template<typename From, typename To, typename Fn>
	void Transform(const std::vector<From>& in, std::vector<To>& out, Fn&& fn)
	{
		for (const From& v : in)
		{
			out.push_back(fn(v));
		}
	}

	inline std::string GetTimeString()
	{
		SYSTEMTIME time;
		GetSystemTime(&time);
		return Sprintf("%d_%02d_%02d__%02d_%02d_%02d_%d",
			time.wYear, time.wMonth, time.wDay,
			time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	}
}
