#include "stdafx.h"
#include "Input.h"

Input& Input::Instance()
{
	static Input i{};
	return i;
}

void Input::Update()
{
	m_CurrentKeyStates.ClearAll();
	m_CurrentMouseStates.ClearAll();

	m_MouseWheel = 0;
	m_MouseDelta = Vector2();
}

void Input::UpdateKey(uint32 keyCode, bool isDown)
{
	m_PersistentKeyStates.AssignBit(keyCode, isDown);
	m_CurrentKeyStates.AssignBit(keyCode, isDown);
}

void Input::UpdateMouseKey(uint32 keyCode, bool isDown)
{
	m_PersistentMouseStates.AssignBit(keyCode, isDown);
	m_CurrentMouseStates.AssignBit(keyCode, isDown);
}

void Input::UpdateMouseWheel(float mouseWheel)
{
	m_MouseWheel = mouseWheel;
}

bool Input::IsKeyDown(uint32 keyCode)
{
	return m_PersistentKeyStates.GetBit(keyCode);
}

bool Input::IsKeyPressed(uint32 keyCode)
{
	return m_PersistentKeyStates.GetBit(keyCode) && m_CurrentKeyStates.GetBit(keyCode);
}

bool Input::IsMouseDown(uint32 keyCode)
{
	return m_PersistentMouseStates.GetBit(keyCode);
}

bool Input::IsMousePressed(uint32 keyCode)
{
	return m_PersistentMouseStates.GetBit(keyCode) && m_CurrentMouseStates.GetBit(keyCode);
}

Vector2 Input::GetMousePosition() const
{
	return m_CurrentMousePosition;
}

Vector2 Input::GetMouseDelta() const
{
	return m_MouseDelta;
}

void Input::UpdateMousePosition(float x, float y)
{
#if PLATFORM_WINDOWS
	m_MouseDelta = Vector2(x, y) - m_CurrentMousePosition;
#endif
	m_CurrentMousePosition = Vector2(x, y);
}

void Input::UpdateMouseDelta(float x, float y)
{
	m_MouseDelta = Vector2(x, y);
}
