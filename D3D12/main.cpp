#include "stdafx.h"
#include "Graphics/Core/Graphics.h"
#include "Core/Input.h"
#include "Core/Console.h"

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include "Core/CommandLine.h"

const int gWindowWidth = 1240;
const int gWindowHeight = 720;
const int gMsaaSampleCount = 4;

class ViewWrapper
{
public:
	void Run(const char* pTitle)
	{
		m_DisplayWidth = gWindowWidth;
		m_DisplayHeight = gWindowHeight;

		MakeWindow(pTitle);

		m_pGraphics = std::make_unique<Graphics>(m_DisplayWidth, m_DisplayHeight, gMsaaSampleCount);
		m_pGraphics->Initialize(m_Window);

		GameTimer::Reset();
		//Game loop
		MSG msg = {};
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				GameTimer::Tick();
				m_pGraphics->Update();
				Input::Instance().Update();
			}
		}
		m_pGraphics->Shutdown();
	}

private:
	void OnPause(const bool paused)
	{
		m_Pause = paused;
		if (paused)
		{
			GameTimer::Stop();
		}
		else
		{
			GameTimer::Start();
		}
	}

	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		ViewWrapper* pThis = nullptr;

		if (message == WM_NCCREATE)
		{
			pThis = static_cast<ViewWrapper*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
			SetLastError(0);
			if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
			{
				if (GetLastError() != 0)
					return 0;
			}
		}
		else
		{
			pThis = reinterpret_cast<ViewWrapper*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		}
		if (pThis)
		{
			LRESULT callback = pThis->WndProc(hWnd, message, wParam, lParam);
			return callback;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_ACTIVATE:
			OnPause(LOWORD(wParam) == WA_INACTIVE);
			return 0;
		// WM_SIZE is sent when the user resizes the window.  
		case WM_SIZE:
		{
			// Save the new client area dimensions.
			m_DisplayWidth = LOWORD(lParam);
			m_DisplayHeight = HIWORD(lParam);
			if (m_pGraphics)
			{
				if (wParam == SIZE_MINIMIZED)
				{
					OnPause(true);
					m_Minimized = true;
					m_Maximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					OnPause(false);
					m_Minimized = false;
					m_Maximized = true;
					m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
				}
				else if (wParam == SIZE_RESTORED)
				{
					// Restoring from minimized state?
					if (m_Minimized)
					{
						OnPause(false);
						m_Minimized = false;
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
					// Restoring from maximized state?
					else if (m_Maximized)
					{
						OnPause(false);
						m_Maximized = false;
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
					else if (!m_IsResizing) // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
					{
						m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
					}
				}
			}
			return 0;
		}
		case WM_KEYUP:
		{
			Input::Instance().UpdateKey((uint32)wParam, false);
			return 0;
		}
		case WM_KEYDOWN:
		{
			Input::Instance().UpdateKey((uint32)wParam, true);
			return 0;
		}
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}
		case WM_LBUTTONDOWN:
			Input::Instance().UpdateMouseKey(0, true);
			break;
		case WM_MBUTTONDOWN:
			Input::Instance().UpdateMouseKey(2, true);
			break;
		case WM_RBUTTONDOWN:
			Input::Instance().UpdateMouseKey(1, true);
			break;
		case WM_LBUTTONUP:
			Input::Instance().UpdateMouseKey(0, false);
			break;
		case WM_MBUTTONUP:
			Input::Instance().UpdateMouseKey(2, false);
			break;
		case WM_RBUTTONUP:
			Input::Instance().UpdateMouseKey(1, false);
			break;
		case WM_ENTERSIZEMOVE:
			OnPause(true);
			m_IsResizing = true;
			break;
		case WM_EXITSIZEMOVE:
			OnPause(false);
			m_IsResizing = false;
			m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
			break;
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	void MakeWindow(const char* pTitle)
	{
		WNDCLASS wc;

		wc.hInstance = GetModuleHandle(0);
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hIcon = 0;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = TEXT("wndClass");
		wc.lpszMenuName = nullptr;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

		if (!RegisterClass(&wc))
		{
			return;
		}

		int displayWidth = GetSystemMetrics(SM_CXSCREEN);
		int displayHeight = GetSystemMetrics(SM_CYSCREEN);

		DWORD windowStyle = WS_OVERLAPPEDWINDOW;

		RECT windowRect = { 0, 0, (LONG)m_DisplayWidth, (LONG)m_DisplayHeight };
		AdjustWindowRect(&windowRect, windowStyle, false);

		int x = (displayWidth - m_DisplayWidth) / 2;
		int y = (displayHeight - m_DisplayHeight) / 2;

		m_Window = CreateWindow(
			TEXT("wndClass"),
			pTitle,
			windowStyle,
			x,
			y,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top,
			nullptr,
			nullptr,
			GetModuleHandle(0),
			this
		);

		if (m_Window == nullptr)
			return;

		ShowWindow(m_Window, SW_SHOWDEFAULT);
		if (!UpdateWindow(m_Window))
		{
			return;
		}
		Input::Instance().SetWindow(m_Window);
	}

private:
	bool m_Pause = false;
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 1240;
	int m_DisplayHeight = 720;
	bool m_IsResizing = false;
	HWND m_Window;
	std::unique_ptr<Graphics> m_pGraphics;
};


int WINAPI WinMain(HINSTANCE hInstance,	HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	CommandLine::Parse(lpCmdLine);
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	Console::Startup();

	E_LOG(Info, "Startup");
	ViewWrapper vp;
	vp.Run("D3D12 - Hello World");
	return 0;
}