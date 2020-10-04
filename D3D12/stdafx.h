#pragma once


//STL
#include <assert.h>
//Containers
#include <string>
#include <queue>
#include <vector>
#include <memory>
#include <array>
#include <map>
#include <unordered_map>
//IO
#include <sstream>
#include <fstream>
#include <iostream>
//Misc
#include <algorithm>
#include <mutex>

#include "Core/CoreTypes.h"

#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#define USE_PIX 1

#include <d3d12.h>
#include <dxgi1_6.h>

#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12/d3dx12.h"
#include "d3dx12/d3dx12_extra.h"
#include "Imgui/imgui.h"

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

#define check(expression) if((expression)){} else Console::LogFormat(LogType::FatalError, #expression)
#define checkf(expression, msg, ...) if((expression)){} else Console::LogFormat(LogType::FatalError, msg, ##__VA_ARGS__)
#define noEntry() checkf(false, "Should not have reached this point!")

#include "Core/Thread.h"
#include "Math/MathTypes.h"
#include "Core/Time.h"
#include "Math/Math.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Graphics/Core/D3DUtils.h"
