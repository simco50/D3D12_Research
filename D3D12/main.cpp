#include "stdafx.h"
#include "Graphics/Core/Graphics.h"
#include "Core/Input.h"
#include "Core/Console.h"

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"

const int gWindowWidth = 1240;
const int gWindowHeight = 720;
const int gMsaaSampleCount = 4;

class ViewWrapper
{
public:
	int Run(HINSTANCE hInstance, const char* pTitle, const char* lpCmdLine)
	{
		Thread::SetMainThread();

		CommandLine::Parse(lpCmdLine);

		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		//_CrtSetBreakAlloc(6528);
		Console::Initialize();

		E_LOG(Info, "Startup");

		TaskQueue::Initialize(std::thread::hardware_concurrency());

		m_DisplayWidth = gWindowWidth;
		m_DisplayHeight = gWindowHeight;

		HWND window = MakeWindow(hInstance, pTitle);
		Input::Instance().SetWindow(window);

		m_pGraphics = new Graphics(m_DisplayWidth, m_DisplayHeight, gMsaaSampleCount);
		m_pGraphics->Initialize(window);

		Time::Reset();

		MSG msg = {};
		bool quit = false;
		while(!quit)
		{
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if (msg.message == WM_QUIT)
				{
					quit = true;
					break;
				}
			}

			Time::Tick();
			m_pGraphics->Update();
			Input::Instance().Update();
		}

		m_pGraphics->Shutdown();
		delete m_pGraphics;

		TaskQueue::Shutdown();
		return 0;
	}

private:
	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		ViewWrapper* pThis = nullptr;
		if (message == WM_NCCREATE)
		{
			pThis = static_cast<ViewWrapper*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
			SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));

		}
		else
		{
			pThis = reinterpret_cast<ViewWrapper*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
			return pThis->WndProc(hWnd, message, wParam, lParam);
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	void OnResize()
	{
		m_pGraphics->OnResize(m_DisplayWidth, m_DisplayHeight);
	}

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_ACTIVATE:
			LOWORD(wParam) == WA_INACTIVE ? Time::Stop() : Time::Start();
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
					Time::Stop();
					m_Minimized = true;
					m_Maximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					Time::Start();
					m_Minimized = false;
					m_Maximized = true;
					OnResize();
				}
				else if (wParam == SIZE_RESTORED)
				{
					// Restoring from minimized state?
					if (m_Minimized)
					{
						Time::Start();
						m_Minimized = false;
						OnResize();
					}
					// Restoring from maximized state?
					else if (m_Maximized)
					{
						Time::Start();
						m_Maximized = false;
						OnResize();
					}
					else if (!m_IsResizing) // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
					{
						OnResize();
					}
				}
			}
			return 0;
		}
		case WM_MOUSEWHEEL:
		{
			float mouseWheel = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			Input::Instance().UpdateMouseWheel(mouseWheel);
			return 0;
		}
		case WM_KEYUP:
		{
			Input::Instance().UpdateKey((uint32)wParam, false);
			if(wParam < 256)
				ImGui::GetIO().KeysDown[wParam] = 0;
			return 0;
		}
		case WM_KEYDOWN:
		{
			Input::Instance().UpdateKey((uint32)wParam, true);
			if (wParam < 256)
				ImGui::GetIO().KeysDown[wParam] = 1;
			return 0;
		}
		case WM_CHAR:
		{
			if (wParam < 256)
				ImGui::GetIO().AddInputCharacter((uint32)wParam);
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
			Time::Stop();
			m_IsResizing = true;
			break;
		case WM_EXITSIZEMOVE:
			Time::Start();
			m_IsResizing = false;
			OnResize();
			break;
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	HWND MakeWindow(HINSTANCE hInstance, const char* pTitle)
	{
		WNDCLASSEX wc{};

		wc.cbSize = sizeof(WNDCLASSEX);
		wc.hInstance = hInstance;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpfnWndProc = WndProcStatic;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpszClassName = TEXT("wndClass");
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

		if (!RegisterClassEx(&wc))
		{
			return nullptr;
		}

		int displayWidth = GetSystemMetrics(SM_CXSCREEN);
		int displayHeight = GetSystemMetrics(SM_CYSCREEN);

		DWORD windowStyle = WS_OVERLAPPEDWINDOW;
		RECT windowRect = { 0, 0, (LONG)m_DisplayWidth, (LONG)m_DisplayHeight };
		AdjustWindowRect(&windowRect, windowStyle, false);

		int x = (displayWidth - m_DisplayWidth) / 2;
		int y = (displayHeight - m_DisplayHeight) / 2;

		HWND window = CreateWindowA(
			TEXT("wndClass"),
			pTitle,
			windowStyle,
			x,
			y,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top,
			nullptr,
			nullptr,
			hInstance,
			this
		);

		if (window == nullptr)
		{
			return window;
		}

		ShowWindow(window, SW_SHOWDEFAULT);

		return window;
	}

private:
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 1240;
	int m_DisplayHeight = 720;
	bool m_IsResizing = false;
	Graphics* m_pGraphics = nullptr;
};

int WINAPI WinMain(HINSTANCE hInstance,	HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	ViewWrapper vp;
	return vp.Run(hInstance, "D3D12", lpCmdLine);
}