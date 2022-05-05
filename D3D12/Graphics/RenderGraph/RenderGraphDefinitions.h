#pragma once

#ifndef RG_ASSERT
#define RG_ASSERT(expression, msg, ...) checkf(expression, msg, ##__VA_ARGS__)
#endif

#ifndef RG_STATIC_ASSERT
#define RG_STATIC_ASSERT(expression, msg) static_assert(expression, msg)
#endif

struct RGResourceHandle
{
	explicit RGResourceHandle(uint32 id = InvalidIndex)
		: Index(id)
	{}

	bool operator==(const RGResourceHandle& other) const { return Index == other.Index; }
	bool operator!=(const RGResourceHandle& other) const { return Index != other.Index; }

	constexpr static const uint32 InvalidIndex = 0xFFFFFFFF;
	inline bool IsValid() const { return Index != InvalidIndex; }

	int Index;
};
