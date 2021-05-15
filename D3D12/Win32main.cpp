#include "stdafx.h"

#if PLATFORM_WINDOWS

#include "Graphics/Core/Graphics.h"
#include "Core/Input.h"
#include "Core/Console.h"
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"
#include "DemoApp.h"

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include "Core/ConsoleVariables.h"
#endif

#define BREAK_ON_ALLOC 0

class Win32AppContainer
{
public:
	static constexpr const char* WINDOW_CLASS_NAME = "WndClass";

	Win32AppContainer(const char* pTitle, uint32 width, uint32 height)
	{
		::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

		WNDCLASSEX wc{};
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.hInstance = GetModuleHandleA(0);
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = WINDOW_CLASS_NAME;
		wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
		check(RegisterClassExA(&wc));

		int displayWidth = GetSystemMetrics(SM_CXSCREEN);
		int displayHeight = GetSystemMetrics(SM_CYSCREEN);

		DWORD windowStyle = WS_OVERLAPPEDWINDOW;
		RECT windowRect = { 0, 0, (LONG)width, (LONG)height };
		AdjustWindowRect(&windowRect, windowStyle, false);

		int x = (displayWidth - width) / 2;
		int y = (displayHeight - height) / 2;

		m_Window = CreateWindowExA(
			0,
			WINDOW_CLASS_NAME,
			pTitle,
			windowStyle,
			x,
			y,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top,
			nullptr,
			nullptr,
			GetModuleHandleA(nullptr),
			this
		);
		check(m_Window);

		ShowWindow(m_Window, SW_SHOWDEFAULT);
		UpdateWindow(m_Window);
	}

	~Win32AppContainer()
	{
		CloseWindow(m_Window);
		UnregisterClassA(WINDOW_CLASS_NAME, GetModuleHandleA(nullptr));
	}

	bool PollMessages()
	{
		MSG msg{};
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);

			if (msg.message == WM_QUIT)
			{
				return false;
			}
		}

		// Listening to WM_MOVE gives some pretty bad results so just do this every frame...
		POINT p;
		::GetCursorPos(&p);
		::ScreenToClient(m_Window, &p);
		OnMouseMoveEvent.Broadcast(p.x, p.y);

		return true;
	}

	void SetWindowTitle(const char* pTitle)
	{
		SetWindowTextA(m_Window, pTitle);
	}

	HWND GetNativeWindow() const { return m_Window; }
	IntVector2 GetRect() const { return IntVector2(m_DisplayWidth, m_DisplayHeight); }

	DECLARE_MULTICAST_DELEGATE(OnFocusChanged, bool);
	OnFocusChanged OnFocusChangedEvent;

	DECLARE_MULTICAST_DELEGATE(OnResize, uint32, uint32);
	OnResize OnResizeEvent;

	DECLARE_MULTICAST_DELEGATE(OnCharInput, uint32);
	OnCharInput OnCharInputEvent;

	DECLARE_MULTICAST_DELEGATE(OnKeyInput, uint32, bool);
	OnKeyInput OnKeyInputEvent;

	DECLARE_MULTICAST_DELEGATE(OnMouseInput, uint32, bool);
	OnMouseInput OnMouseInputEvent;

	DECLARE_MULTICAST_DELEGATE(OnMouseMove, uint32, uint32);
	OnMouseMove OnMouseMoveEvent;

	DECLARE_MULTICAST_DELEGATE(OnMouseScroll, float);
	OnMouseScroll OnMouseScrollEvent;

private:
	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Win32AppContainer* pThis = nullptr;
		if (message == WM_NCCREATE)
		{
			pThis = static_cast<Win32AppContainer*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
			SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			pThis = reinterpret_cast<Win32AppContainer*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
			return pThis->WndProc(hWnd, message, wParam, lParam);
		}
		return DefWindowProcA(hWnd, message, wParam, lParam);
	}

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			break;
		}
		case WM_ACTIVATE:
		{
			LOWORD(wParam) == WA_INACTIVE ? Time::Stop() : Time::Start();
			break;
		}
		case WM_SIZE:
		{
			// Save the new client area dimensions.
			int newWidth = LOWORD(lParam);
			int newHeight = HIWORD(lParam);
			bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
			bool shouldResize = false;

			m_DisplayWidth = LOWORD(lParam);
			m_DisplayHeight = HIWORD(lParam);

			if (wParam == SIZE_MINIMIZED)
			{
				OnFocusChangedEvent.Broadcast(false);
				m_Minimized = true;
				m_Maximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				OnFocusChangedEvent.Broadcast(true);
				m_Minimized = false;
				m_Maximized = true;
				shouldResize = true;
			}
			else if (wParam == SIZE_RESTORED)
			{
				// Restoring from minimized state?
				if (m_Minimized)
				{
					OnFocusChangedEvent.Broadcast(true);
					m_Minimized = false;
					shouldResize = true;
				}
				// Restoring from maximized state?
				else if (m_Maximized)
				{
					OnFocusChangedEvent.Broadcast(true);
					m_Maximized = false;
					shouldResize = true;
				}
				else if (!m_IsResizing) // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					shouldResize = true;
				}
			}

			if (shouldResize && resized)
			{
				OnResizeEvent.Broadcast(m_DisplayWidth, m_DisplayHeight);
			}
			break;
		}
		case WM_MOUSEWHEEL:
		{
			float mouseWheel = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			OnMouseScrollEvent.Broadcast(mouseWheel);
			break;
		}
		case WM_KEYUP:
		{
			OnKeyInputEvent.Broadcast((uint32)wParam, false);
			break;
		}
		case WM_KEYDOWN:
		{
			OnKeyInputEvent.Broadcast((uint32)wParam, true);
			break;
		}
		case WM_CHAR:
		{
			if (wParam < 256)
				OnCharInputEvent.Broadcast((uint32)wParam);
			break;
		}
		case WM_LBUTTONDOWN:
			OnMouseInputEvent.Broadcast(VK_LBUTTON, true);
			break;
		case WM_MBUTTONDOWN:
			OnMouseInputEvent.Broadcast(VK_MBUTTON, true);
			break;
		case WM_RBUTTONDOWN:
			OnMouseInputEvent.Broadcast(VK_RBUTTON, true);
			break;
		case WM_LBUTTONUP:
			OnMouseInputEvent.Broadcast(VK_LBUTTON, false);
			break;
		case WM_MBUTTONUP:
			OnMouseInputEvent.Broadcast(VK_MBUTTON, false);
			break;
		case WM_RBUTTONUP:
			OnMouseInputEvent.Broadcast(VK_RBUTTON, false);
			break;
		case WM_ENTERSIZEMOVE:
			OnFocusChangedEvent.Broadcast(false);
			m_IsResizing = true;
			break;
		case WM_EXITSIZEMOVE:
			OnFocusChangedEvent.Broadcast(true);
			RECT rect;
			GetClientRect(hWnd, &rect);
			int newWidth = rect.right - rect.left;
			int newHeight = rect.bottom - rect.top;
			bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
			if (resized)
			{
				m_DisplayWidth = newWidth;
				m_DisplayHeight = newHeight;
				OnResizeEvent.Broadcast(newWidth, newHeight);
			}
			m_IsResizing = false;
			break;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

private:
	HWND m_Window = nullptr;
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 0;
	int m_DisplayHeight = 0;
	bool m_IsResizing = false;
};

int WINAPI WinMain(_In_ HINSTANCE /*hInstance*/, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int /*nShowCmd*/)
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#if BREAK_ON_ALLOC > 0
	_CrtSetBreakAlloc(BREAK_ON_ALLOC);
#endif
#endif

	Thread::SetMainThread();
	CommandLine::Parse(GetCommandLineA());
	Console::Initialize();
	CVarManager::Initialize();
	TaskQueue::Initialize(std::thread::hardware_concurrency());

	Win32AppContainer app("D3D12", 1240, 720);
	DemoApp graphics(app.GetNativeWindow(), app.GetRect(), 1);

	app.OnKeyInputEvent += [](uint32 character, bool isDown) {
		Input::Instance().UpdateKey(character, isDown);
		ImGui::GetIO().KeysDown[character] = isDown;
	};
	app.OnMouseInputEvent += [](uint32 mouse, bool isDown) { Input::Instance().UpdateMouseKey(mouse, isDown); };
	app.OnMouseMoveEvent += [](uint32 x, uint32 y) { Input::Instance().UpdateMousePosition((float)x, (float)y); };
	app.OnResizeEvent += [&graphics](uint32 width, uint32 height) { graphics.OnResize(width, height); };
	app.OnCharInputEvent += [](uint32 character) { ImGui::GetIO().AddInputCharacter(character); };
	app.OnMouseScrollEvent += [](float wheel) { Input::Instance().UpdateMouseWheel(wheel); };

	Time::Reset();

	while (app.PollMessages())
	{
		OPTICK_FRAME("MainThread");
		Time::Tick();
		graphics.Update();
		Input::Instance().Update();
	}

	OPTICK_SHUTDOWN();
	TaskQueue::Shutdown();
	Console::Shutdown();

	return 0;
}
#endif
