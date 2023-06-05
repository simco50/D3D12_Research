#pragma once

#include <stdint.h>

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

template<typename T>
struct FnProc
{
	FnProc(const char* pName)
		: pName(pName), pFunction(nullptr)
	{}

	T Load(HMODULE library)
	{
		assert(library);
		pFunction = (T)GetProcAddress(library, pName);
		assert(pFunction);
		return pFunction;
	}

	template<typename... Args>
	auto operator()(Args&&... args)
	{
		assert(pFunction && "Function is not yet loaded");
		return pFunction(std::forward<Args&&>(args)...);
	}

	const char* pName;
	T pFunction;
};

#define FN_PROC(fnName) static FnProc<decltype(&fnName)> fnName##Fn(#fnName)
