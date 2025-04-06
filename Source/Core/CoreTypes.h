#pragma once

#include <stdint.h>

//Containers
#include <string>
#include <queue>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>
//Misc
#include <mutex>
#include <numeric>

#include "unordered_dense.h"

#define STRINGIFY_HELPER(a) #a
#define STRINGIFY(a) STRINGIFY_HELPER(a)
#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )

#define NODISCARD [[nodiscard]]

#ifdef _DEBUG
#define IF_DEBUG(x) x
#else
#define IF_DEBUG(x)
#endif

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

static_assert(sizeof(int8) == 1, "int8 size incorrect.");
static_assert(sizeof(int16) == 2, "int16 size incorrect.");
static_assert(sizeof(int32) == 4, "int32 size incorrect.");
static_assert(sizeof(int64) == 8, "int64 size incorrect.");
static_assert(sizeof(uint8) == 1, "uint8 size incorrect.");
static_assert(sizeof(uint16) == 2, "uint16 size incorrect.");
static_assert(sizeof(uint32) == 4, "uint32 size incorrect.");
static_assert(sizeof(uint64) == 8, "uint64 size incorrect.");

#define DECLARE_BITMASK_TYPE(Enum) \
	inline constexpr Enum& operator|=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs | (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum& operator&=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs & (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum& operator^=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs ^ (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum  operator| (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs | (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum  operator& (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs & (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum  operator^ (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs ^ (__underlying_type(Enum))Rhs); } \
	inline constexpr bool  operator! (Enum  E) { return !(__underlying_type(Enum))E; } \
	inline constexpr Enum  operator~ (Enum  E) { return (Enum)~(__underlying_type(Enum))E; }

template<typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
constexpr inline bool EnumHasAllFlags(Enum Flags, Enum Contains)
{
	return (((__underlying_type(Enum))Flags) & (__underlying_type(Enum))Contains) == ((__underlying_type(Enum))Contains);
}

template<typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
constexpr inline bool EnumHasAnyFlags(Enum Flags, Enum Contains)
{
	return (((__underlying_type(Enum))Flags) & (__underlying_type(Enum))Contains) != 0;
}


template<typename Key, typename Value, typename Hash = ankerl::unordered_dense::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using HashMap = ankerl::unordered_dense::map<Key, Value, Hash, KeyEqual>;

template<typename Key, typename Hash = ankerl::unordered_dense::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using HashSet = ankerl::unordered_dense::set<Key, Hash, KeyEqual>;

using String = std::string;
using StringView = std::string_view;

template<typename T, typename Allocator = std::allocator<T>>
using Array = std::vector<T, Allocator>;

template<typename T, size_t Size>
using StaticArray = std::array<T, Size>;

template<typename T>
using UniquePtr = std::unique_ptr<T>;

inline uint64 gHash(uint64 inValue)
{
	return ankerl::unordered_dense::detail::wyhash::hash(inValue);
}

inline uint64 gHashCombine(uint64 inA, uint64 inB)
{
	return ankerl::unordered_dense::detail::wyhash::mix(inA, inB);
}

template<typename T>
inline uint64 gHash(T& value)
{
	static_assert(std::has_unique_object_representations_v<T>);
	return ankerl::unordered_dense::detail::wyhash::hash(&value, sizeof(T));
}

