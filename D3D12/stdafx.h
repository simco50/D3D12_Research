#pragma once

#ifndef PLATFORM_WINDOWS
#define PLATFORM_WINDOWS 0
#endif

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
#include <unordered_set>
//IO
#include <sstream>
#include <fstream>
#include <iostream>
//Misc
#include <mutex>

#include "Core/MinWindows.h"
#include <wrl/client.h>

#define USE_PIX 1

#include "d3d12.h"

#include <dxgi1_6.h>

#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12/d3dx12.h"
#include "Imgui/imgui.h"

#define USE_OPTICK 1
#define OPTICK_ENABLE_TRACING 1
#define OPTICK_ENABLE_GPU_D3D12 1
#define OPTICK_ENABLE_GPU_VULKAN 0
#include "optick.h"

#ifndef D3D12_USE_RENDERPASSES
#define D3D12_USE_RENDERPASSES 1
#endif

#ifndef WITH_CONSOLE
#define WITH_CONSOLE 1
#endif

#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )

#define checkf(expression, msg, ...) if((expression)){} else Console::LogFormat(LogType::FatalError, msg, ##__VA_ARGS__)
#define check(expression) checkf(expression, "")
#define noEntry() checkf(false, "Should not have reached this point!")
#define validateOncef(expression, msg, ...) if(!(expression)) { \
	static bool hasExecuted = false; \
	if(!hasExecuted) \
	{ \
		Console::LogFormat(LogType::Warning, "Validate failed: '" #expression "'. " msg, ##__VA_ARGS__); \
		hasExecuted = true; \
	} \
} \

#define validateOnce(expression) validateOncef(expression, "")

#include "Core/CoreTypes.h"
#include "Core/CString.h"
#include "Core/String.h"
#include "Core/Thread.h"
#include "Math/MathTypes.h"
#include "Core/Time.h"
#include "Math/Math.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Graphics/Core/D3DUtils.h"
