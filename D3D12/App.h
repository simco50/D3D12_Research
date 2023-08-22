#pragma once
#include "Core/Window.h"

class App
{
public:
	int Run();

protected:
	virtual void Init() {}
	virtual void Update() {}
	virtual void Shutdown() {}
	virtual void OnWindowResized(uint32 width, uint32 height) {}

	Window GetWindow() const { return m_Window; }

private:
	virtual void Init_Internal();
	virtual void Update_Internal();
	virtual void Shutdown_Internal();

	Window m_Window;
};

#define DECLARE_MAIN(app_class) \
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)	\
{																				\
	app_class app;																\
	return app.Run();															\
}
