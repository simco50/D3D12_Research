#include "stdafx.h"
#include "App.h"
#include "Core/Input.h"
#include "Core/Console.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"
#include "Core/ConsoleVariables.h"
#include "Core/Window.h"
#include "Core/Profiler.h"

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#define BREAK_ON_ALLOC 0

#ifdef LIVE_PP_PATH
#include "LPP_API_x64_CPP.h"
#define LIVE_PP() LivePPAgent livePP;
struct LivePPAgent
{
	LivePPAgent()
	{
		// create a default agent, loading the Live++ agent from the given path, e.g. "ThirdParty/LivePP"
		Agent = lpp::LppCreateDefaultAgent(LIVE_PP_PATH);
		// bail out in case the agent is not valid
		if (lpp::LppIsValidDefaultAgent(&Agent))
		{
			// enable Live++ for all loaded modules
			Agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr);
		}
	}

	~LivePPAgent()
	{
		if (lpp::LppIsValidDefaultAgent(&Agent))
		{
			// destroy the Live++ agent
			lpp::LppDestroyDefaultAgent(&Agent);
		}
	}

	lpp::LppDefaultAgent Agent;
};
#else
#define LIVE_PP()
#endif

int App::Run()
{
	Init_Internal();
	while (m_Window.PollMessages())
	{
		PROFILE_FRAME();

		Update_Internal();
	}
	Shutdown_Internal();
	return 0;
}

void App::Init_Internal()
{
	LIVE_PP();

#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#if BREAK_ON_ALLOC > 0
	_CrtSetBreakAlloc(BREAK_ON_ALLOC);
#endif
#endif

	Thread::SetMainThread();
	CommandLine::Parse(GetCommandLineA());

	if (CommandLine::GetBool("debuggerwait"))
	{
		while (!::IsDebuggerPresent())
		{
			::Sleep(100);
		}
	}

	Console::Initialize();
	ConsoleManager::Initialize();

	PROFILE_REGISTER_THREAD("Main Thread");
	TaskQueue::Initialize(std::thread::hardware_concurrency());

	Vector2i displayDimensions = Window::GetDisplaySize();

	m_Window.Init((int)(displayDimensions.x * 0.7f), (int)(displayDimensions.y * 0.7f));
	m_Window.OnKeyInput += [](uint32 character, bool isDown) { Input::Instance().UpdateKey(character, isDown); };
	m_Window.OnMouseInput += [](uint32 mouse, bool isDown) { Input::Instance().UpdateMouseKey(mouse, isDown); };
	m_Window.OnMouseMove += [](uint32 x, uint32 y) { Input::Instance().UpdateMousePosition((float)x, (float)y); };
	m_Window.OnMouseScroll += [](float wheel) { Input::Instance().UpdateMouseWheel(wheel); };
	m_Window.OnResizeOrMove += [this](uint32 width, uint32 height) { OnWindowResized(width, height); };

	Time::Reset();

	Init();
}

void App::Update_Internal()
{
	Time::Tick();
	Update();
	Input::Instance().Update();
}

void App::Shutdown_Internal()
{
	Shutdown();

	TaskQueue::Shutdown();
	Console::Shutdown();
}
