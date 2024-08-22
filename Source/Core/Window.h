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

	MulticastDelegate<bool>				OnFocusChanged;
	MulticastDelegate<uint32, uint32>	OnResizeOrMove;
	MulticastDelegate<uint32>			OnCharInput;
	MulticastDelegate<uint32, bool>		OnKeyInput;
	MulticastDelegate<uint32, bool>		OnMouseInput;
	MulticastDelegate<uint32, uint32>	OnMouseMove;
	MulticastDelegate<float>			OnMouseScroll;

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
