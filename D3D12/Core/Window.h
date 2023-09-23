#pragma once

class Window
{
public:
	static constexpr const char* WINDOW_CLASS_NAME = "WndClass";

	Window();
	~Window();

	void Init(uint32 width, uint32 height);

	static Vector2i GetDisplaySize();

	bool PollMessages();

	void SetTitle(const char* pTitle);

	HWND GetNativeWindow() const { return m_Window; }
	Vector2i GetRect() const { return Vector2i(m_DisplayWidth, m_DisplayHeight); }

	DECLARE_MULTICAST_DELEGATE(OnFocusChangedDelegate, bool);
	OnFocusChangedDelegate OnFocusChanged;

	DECLARE_MULTICAST_DELEGATE(OnResizeDelegate, uint32, uint32);
	OnResizeDelegate OnResizeOrMove;

	DECLARE_MULTICAST_DELEGATE(OnCharInputDelegate, uint32);
	OnCharInputDelegate OnCharInput;

	DECLARE_MULTICAST_DELEGATE(OnKeyInputDelegate, uint32, bool);
	OnKeyInputDelegate OnKeyInput;

	DECLARE_MULTICAST_DELEGATE(OnMouseInputDelegate, uint32, bool);
	OnMouseInputDelegate OnMouseInput;

	DECLARE_MULTICAST_DELEGATE(OnMouseMoveDelegate, uint32, uint32);
	OnMouseMoveDelegate OnMouseMove;

	DECLARE_MULTICAST_DELEGATE(OnMouseScrollDelegate, float);
	OnMouseScrollDelegate OnMouseScroll;

private:
	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND m_Window = nullptr;
	bool m_Minimized = false;
	bool m_Maximized = false;
	int m_DisplayWidth = 0;
	int m_DisplayHeight = 0;
	bool m_IsResizing = false;
};
