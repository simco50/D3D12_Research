#pragma once

#include "Core/MinWindows.h"
#include "d3d12.h"
#include <dxgi1_6.h>
#include "d3dx12/d3dx12.h"

using ID3D12DeviceX					= ID3D12Device14;
using ID3D12GraphicsCommandListX	= ID3D12GraphicsCommandList10;
using ID3D12ResourceX				= ID3D12Resource2;

using IDXGIFactoryX					= IDXGIFactory7;
using IDXGISwapChainX				= IDXGISwapChain4;

#include <imgui.h>

#include "Core/CoreTypes.h"
#include "Core/Assert.h"
#include "Core/CString.h"
#include "Core/Thread.h"
#include "Core/Time.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Core/Ref.h"
#include "Core/Span.h"
#include "Core/EnumSet.h"
#include "Math/MathTypes.h"
#include "Math/Math.h"
