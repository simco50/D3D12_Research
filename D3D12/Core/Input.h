#pragma once

enum KeyState
{
	None,
	Down,
	Pressed,
	DownAndPressed = Down | Pressed,
};

class Input
{
public:
	static Input& Instance();

	void SetWindow(HWND window);

	void Update();
	void UpdateKey(uint32 keyCode, bool isDown);
	void UpdateMouseKey(uint32 keyCode, bool isDown);
	void UpdateMouseWheel(float mouseWheel);
	void UpdateMousePosition(float x, float y);
	void UpdateMouseDelta(float x, float y);

	bool IsKeyDown(uint32 keyCode);
	bool IsKeyPressed(uint32 keyCode);

	bool IsMouseDown(uint32 keyCode);
	bool IsMousePressed(uint32 keyCode);

	Vector2 GetMousePosition() const;
	Vector2 GetMouseDelta() const;
	float GetMouseWheelDelta() const { return m_MouseWheel; }

private:
	std::array<KeyState, 256> m_KeyStates = {};
	std::array<KeyState, 16> m_MouseStates = {};

	Input() = default;
	HWND m_pWindow = nullptr;
	Vector2 m_MouseDelta;
	Vector2 m_CurrentMousePosition;
	float m_MouseWheel = 0;
};

