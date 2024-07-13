#include "stdafx.h"
#include "Window.h"

#include <imgui_impl_win32.h>

Window::Window()
{

}

Window::~Window()
{
	CloseWindow(m_Window);
	UnregisterClassA(WINDOW_CLASS_NAME, GetModuleHandleA(nullptr));
}

void Window::Init(uint32 width, uint32 height)
{
	ImGui_ImplWin32_EnableDpiAwareness();

	WNDCLASSEX wc{};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.hInstance = GetModuleHandleA(0);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpfnWndProc = WndProcStatic;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpszClassName = WINDOW_CLASS_NAME;
	wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
	gVerify(RegisterClassExA(&wc), == TRUE);

	Vector2i displayDimensions = GetDisplaySize();

	DWORD windowStyle = WS_OVERLAPPEDWINDOW;
	RECT windowRect = { 0, 0, (LONG)width, (LONG)height };
	gVerify(AdjustWindowRect(&windowRect, windowStyle, false), == TRUE);

	int x = (displayDimensions.x - width) / 2;
	int y = (displayDimensions.y - height) / 2;

	m_Window = CreateWindowExA(
		0,
		WINDOW_CLASS_NAME,
		"",
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
	gAssert(m_Window);

	gVerify(ShowWindow(m_Window, SW_SHOWDEFAULT), == TRUE);
	gVerify(UpdateWindow(m_Window), == TRUE);
}

Vector2i Window::GetDisplaySize()
{
	int displayWidth = GetSystemMetrics(SM_CXSCREEN);
	int displayHeight = GetSystemMetrics(SM_CYSCREEN);
	return Vector2i(displayWidth, displayHeight);
}

bool Window::PollMessages()
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
	OnMouseMove.Broadcast(p.x, p.y);

	return true;
}

void Window::SetTitle(const char* pTitle)
{
	SetWindowTextA(m_Window, pTitle);
}

LRESULT Window::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Window* pThis = nullptr;
	if (message == WM_NCCREATE)
	{
		pThis = static_cast<Window*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	}
	else
	{
		pThis = reinterpret_cast<Window*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
		return pThis->WndProc(hWnd, message, wParam, lParam);
	}
	return DefWindowProcA(hWnd, message, wParam, lParam);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT Window::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

	switch (message)
	{
	case WM_DESTROY:
	{
		PostQuitMessage(0);
		break;
	}
	case WM_ACTIVATE:
	{
		OnFocusChanged.Broadcast(LOWORD(wParam) != WA_INACTIVE);
		break;
	}
	case WM_SIZE:
	{
		// Save the new client area dimensions.
		int newWidth = LOWORD(lParam);
		int newHeight = HIWORD(lParam);
		bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
		bool shouldResize = false;

		if (wParam == SIZE_MINIMIZED)
		{
			OnFocusChanged.Broadcast(false);
			m_Minimized = true;
			m_Maximized = false;
		}
		else if (wParam == SIZE_MAXIMIZED)
		{
			OnFocusChanged.Broadcast(true);
			m_Minimized = false;
			m_Maximized = true;
			shouldResize = true;
		}
		else if (wParam == SIZE_RESTORED)
		{
			// Restoring from minimized state?
			if (m_Minimized)
			{
				OnFocusChanged.Broadcast(true);
				m_Minimized = false;
				shouldResize = true;
			}
			// Restoring from maximized state?
			else if (m_Maximized)
			{
				OnFocusChanged.Broadcast(true);
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
			m_DisplayWidth = LOWORD(lParam);
			m_DisplayHeight = HIWORD(lParam);
			OnResizeOrMove.Broadcast(m_DisplayWidth, m_DisplayHeight);
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		float mouseWheel = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
		OnMouseScroll.Broadcast(mouseWheel);
		break;
	}
	case WM_KEYUP:
	{
		OnKeyInput.Broadcast((uint32)wParam, false);
		break;
	}
	case WM_KEYDOWN:
	{
		OnKeyInput.Broadcast((uint32)wParam, true);
		break;
	}
	case WM_CHAR:
	{
		if (wParam < 256)
			OnCharInput.Broadcast((uint32)wParam);
		break;
	}
	case WM_LBUTTONDOWN:
		OnMouseInput.Broadcast(VK_LBUTTON, true);
		break;
	case WM_MBUTTONDOWN:
		OnMouseInput.Broadcast(VK_MBUTTON, true);
		break;
	case WM_RBUTTONDOWN:
		OnMouseInput.Broadcast(VK_RBUTTON, true);
		break;
	case WM_LBUTTONUP:
		OnMouseInput.Broadcast(VK_LBUTTON, false);
		break;
	case WM_MBUTTONUP:
		OnMouseInput.Broadcast(VK_MBUTTON, false);
		break;
	case WM_RBUTTONUP:
		OnMouseInput.Broadcast(VK_RBUTTON, false);
		break;
	case WM_ENTERSIZEMOVE:
		OnFocusChanged.Broadcast(false);
		m_IsResizing = true;
		break;
	case WM_EXITSIZEMOVE:
		OnFocusChanged.Broadcast(true);
		RECT rect;
		GetClientRect(hWnd, &rect);
		m_DisplayWidth = rect.right - rect.left;
		m_DisplayHeight = rect.bottom - rect.top;
		OnResizeOrMove.Broadcast(m_DisplayWidth, m_DisplayHeight);
		m_IsResizing = false;
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}
