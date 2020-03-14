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

#include <d3d12.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "Graphics/d3dx12.h"
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DXProgrammableCapture.h>

#include "Math/MathTypes.h"
#include "Core/GameTimer.h"
#include "Core/BitField.h"
#include "Math/MathHelp.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Graphics/D3DUtils.h"
#include "External/Imgui/imgui.h"
#include "Core/Delegates.h"

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

template <size_t S>
struct EnumSize;
template <>
struct EnumSize<1>
{
	typedef int8 type;
};
template <>
struct EnumSize<2>
{
	typedef int16 type;
};
template <>
struct EnumSize<4>
{
	typedef int32 type;
};
template <>
struct EnumSize<8>
{
	typedef int64 type;
};

// used as an approximation of std::underlying_type<T>
template <class T>
struct EnumFlagSize
{
	typedef typename EnumSize<sizeof(T)>::type type;
};

#define DECLARE_BITMASK_TYPE(ENUMTYPE) \
inline constexpr ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) | ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline ENUMTYPE& operator |= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) |= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) & ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline ENUMTYPE& operator &= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) &= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr ENUMTYPE operator ~ (ENUMTYPE a) throw() { return ENUMTYPE(~((EnumFlagSize<ENUMTYPE>::type)a)); } \
inline constexpr ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((EnumFlagSize<ENUMTYPE>::type)a) ^ ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline ENUMTYPE& operator ^= (ENUMTYPE& a, ENUMTYPE b) throw() { return (ENUMTYPE&)(((EnumFlagSize<ENUMTYPE>::type&)a) ^= ((EnumFlagSize<ENUMTYPE>::type)b)); } \
inline constexpr bool Any(ENUMTYPE a, ENUMTYPE b) throw() { return (ENUMTYPE)(((EnumFlagSize<ENUMTYPE>::type&)a) & ((EnumFlagSize<ENUMTYPE>::type)b)) == b; }
