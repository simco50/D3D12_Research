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
#include <functional>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <fstream>

#if defined(_MSC_FULL_VER)
using int8 = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;

using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using uint32 = unsigned __int32;
using uint64 = unsigned __int64;
#else
using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
#endif

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

#include "Core/CommandLine.h"
#include "Math/MathTypes.h"
#include "Core/GameTimer.h"
#include "Core/BitField.h"
#include "Math/MathHelp.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Graphics/Core/D3DUtils.h"

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