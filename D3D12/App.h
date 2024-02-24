#pragma once
#include "Core/Window.h"
#include "Graphics/RHI/RHI.h"

class SwapChain;

class App
{
public:
	int Run();

protected:
	virtual void Init() {}
	virtual void Update() {}
	virtual void Shutdown() {}
	virtual void OnWindowResized(uint32 width, uint32 height) {}

protected:
	Ref<GraphicsDevice> m_pDevice;
	Ref<SwapChain> m_pSwapchain;
	Window m_Window;

	void Init_Internal();
	void Update_Internal();
	void Shutdown_Internal();
	void OnWindowResized_Internal(uint32 width, uint32 height);
};

#define DECLARE_MAIN(app_class) \
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)	\
{																				\
	app_class app;																\
	return app.Run();															\
}
