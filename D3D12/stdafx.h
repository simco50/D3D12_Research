#pragma once

#include <string>
#include <queue>
#include <vector>
#include <iostream>
#include <memory>
#include <array>
#include <queue>
#include <mutex>
#include <map>
#include <sstream>
#include <algorithm>
#include <assert.h>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <fstream>

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DXProgrammableCapture.h>
#include <dxc/dxcapi.h>

#include "External/d3dx12/d3dx12.h"
#include "External/Imgui/imgui.h"

#define DECLARE_BITMASK_TYPE(Enum) \
	inline Enum& operator|=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs | (__underlying_type(Enum))Rhs); } \
	inline Enum& operator&=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs & (__underlying_type(Enum))Rhs); } \
	inline Enum& operator^=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs ^ (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum  operator| (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs | (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum  operator& (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs & (__underlying_type(Enum))Rhs); } \
	inline constexpr Enum  operator^ (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs ^ (__underlying_type(Enum))Rhs); } \
	inline constexpr bool  operator! (Enum  E) { return !(__underlying_type(Enum))E; } \
	inline constexpr Enum  operator~ (Enum  E) { return (Enum)~(__underlying_type(Enum))E; }

template<typename Enum>
inline bool EnumHasAllFlags(Enum Flags, Enum Contains)
{
	return (((__underlying_type(Enum))Flags) & (__underlying_type(Enum))Contains) == ((__underlying_type(Enum))Contains);
}

template<typename Enum>
inline bool EnumHasAnyFlags(Enum Flags, Enum Contains)
{
	return (((__underlying_type(Enum))Flags) & (__underlying_type(Enum))Contains) != 0;
}

inline int ToMultibyte(const wchar_t* pStr, char* pOut, int len)
{
	size_t converted = 0;
	wcstombs_s(&converted, pOut, len, pStr, len);
	return (int)converted;
}

inline int ToWidechar(const char* pStr, wchar_t* pOut, int len)
{
	size_t converted = 0;
	mbstowcs_s(&converted, pOut, len, pStr, len - 1);
	return (int)converted;
}

#ifndef D3D12_USE_RENDERPASSES
#define D3D12_USE_RENDERPASSES 1
#endif

#ifndef WITH_CONSOLE
#define WITH_CONSOLE 1
#endif

#define check(expression) assert(expression)
#define checkf(expression, msg) assert(expression)
#define noEntry() assert(false && "Should not have reached this point!")


#include "Math/MathTypes.h"
#include "Core/Time.h"
#include "Math/Math.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Graphics/Core/D3DUtils.h"
