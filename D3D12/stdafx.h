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
#include <unordered_set>
//IO
#include <fstream>
//Misc
#include <mutex>

#define USE_PIX 1

#include "Core/MinWindows.h"
#include "d3d12.h"
#include <dxgi1_6.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12.h"

#include "imgui.h"

#include "Core/Defines.h"
#include "Core/CoreTypes.h"
#include "Core/CString.h"
#include "Core/Thread.h"
#include "Core/Time.h"
#include "Core/Console.h"
#include "Core/StringHash.h"
#include "Core/Delegates.h"
#include "Core/RefCountPtr.h"
#include "Core/Span.h"
#include "Math/MathTypes.h"
#include "Math/Math.h"
