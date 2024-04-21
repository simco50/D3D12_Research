#pragma once

//STL
#include <assert.h>
//Containers
#include <string>
#include <queue>
#include <vector>
#include <memory>
#include <array>
#include <unordered_map>
#include <unordered_set>
//Misc
#include <mutex>
#include <numeric>

#include "Core/MinWindows.h"
#include "d3d12.h"
#include <dxgi1_6.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12/d3dx12.h"

#include <External/Imgui/imgui.h>

#include "Core/Defines.h"
#include "Core/CoreTypes.h"
#include "Core/CString.h"
#include "Core/Thread.h"
#include "Core/Time.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Core/Ref.h"
#include "Core/Span.h"
#include "Math/MathTypes.h"
#include "Math/Math.h"
