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
#include <algorithm>
#include <fstream>
#include "Core/CoreTypes.h"

#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#include <d3d12.h>
#include <dxgi1_6.h>

#include "External/d3dx12/d3dx12.h"
#include "External/d3dx12/d3dx12_extra.h"
#include "External/Imgui/imgui.h"

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
#define checkf(expression, msg, ...) assert(expression && msg)
#define noEntry() checkf(false, "Should not have reached this point!")

#include "Core/Thread.h"
#include "Math/MathTypes.h"
#include "Core/Time.h"
#include "Math/Math.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Graphics/Core/D3DUtils.h"
