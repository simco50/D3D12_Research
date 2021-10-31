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
}
