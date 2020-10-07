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

#ifdef PLATFORM_WINDOWS

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

#elif defined(PLATFORM_UWP)


using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::ViewManagement;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;
using namespace DirectX;

ref class ViewProvider sealed : public IFrameworkView
{
public:
	ViewProvider() :
		m_Exit(false),
		m_Visible(true),
		m_InSizeMove(false),
		m_DPI(96.f)
	{
	}

	// IFrameworkView methods
	virtual void Initialize(CoreApplicationView^ applicationView)
	{
		applicationView->Activated +=
			ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &ViewProvider::OnActivated);

		CoreApplication::Suspending +=
			ref new EventHandler<SuspendingEventArgs^>(this, &ViewProvider::OnSuspending);

		CoreApplication::Resuming +=
			ref new EventHandler<Platform::Object^>(this, &ViewProvider::OnResuming);

		m_pGraphics = std::make_unique<Graphics>(gWindowWidth, gWindowHeight, gMsaaSampleCount);
	}

	virtual void Uninitialize()
	{
		m_pGraphics.reset();
	}

	virtual void SetWindow(CoreWindow^ window)
	{
		window->SizeChanged += ref new TypedEventHandler<CoreWindow^, WindowSizeChangedEventArgs^>(this, &ViewProvider::OnWindowSizeChanged);
		window->VisibilityChanged += ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &ViewProvider::OnVisibilityChanged);
		window->Closed += ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &ViewProvider::OnWindowClosed);
		window->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &ViewProvider::OnKeyDown);
		window->KeyUp += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &ViewProvider::OnKeyUp);
		window->PointerPressed += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &ViewProvider::OnPointerPressed);
		window->PointerReleased += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &ViewProvider::OnPointerReleased);
		window->PointerMoved += ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &ViewProvider::OnPointerMoved);
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
		try
		{
			window->ResizeStarted += ref new TypedEventHandler<CoreWindow^, Object^>(this, &ViewProvider::OnResizeStarted);
			window->ResizeCompleted += ref new TypedEventHandler<CoreWindow^, Object^>(this, &ViewProvider::OnResizeCompleted);
		}
		catch (...)
		{
			// Requires Windows 10 Creators Update (10.0.15063) or later
		}
#endif


		auto dispatcher = CoreWindow::GetForCurrentThread()->Dispatcher;
		dispatcher->AcceleratorKeyActivated += ref new TypedEventHandler<CoreDispatcher^, AcceleratorKeyEventArgs^>(this, &ViewProvider::OnAcceleratorKeyActivated);

		auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
		navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &ViewProvider::OnBackRequested);

		auto currentDisplayInformation = DisplayInformation::GetForCurrentView();
		currentDisplayInformation->DpiChanged += ref new TypedEventHandler<DisplayInformation^, Object^>(this, &ViewProvider::OnDpiChanged);
		currentDisplayInformation->OrientationChanged += ref new TypedEventHandler<DisplayInformation^, Object^>(this, &ViewProvider::OnOrientationChanged);

		DisplayInformation::DisplayContentsInvalidated += ref new TypedEventHandler<DisplayInformation^, Object^>(this, &ViewProvider::OnDisplayContentsInvalidated);

		m_DPI = currentDisplayInformation->LogicalDpi;

		m_LogicalWidth = window->Bounds.Width;
		m_LogicalHeight = window->Bounds.Height;

		m_pGraphics->Initialize(window);
	}

	virtual void Load(Platform::String^ entryPoint)
	{
	}

	virtual void Run()
	{
		while (!m_Exit)
		{
			if (m_Visible)
			{
				Input::Instance().Update();
				m_pGraphics->Update();
				CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
			}
			else
			{
				CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
			}
		}
	}

protected:

	void OnKeyDown(CoreWindow^ sender, KeyEventArgs^ args)
	{
		Input::Instance().UpdateKey((uint32)args->VirtualKey, true);
	}

	void OnKeyUp(CoreWindow^ sender, KeyEventArgs^ args)
	{
		Input::Instance().UpdateKey((uint32)args->VirtualKey, false);
	}

	void OnPointerReleased(CoreWindow^ sender, PointerEventArgs^ args)
	{
		Input::Instance().UpdateMouseKey(0, true);
	}

	void OnPointerPressed(CoreWindow^ sender, PointerEventArgs^ args)
	{
		Input::Instance().UpdateMouseKey(0, false);
		if (m_CapturingMouse)
		{
			sender->ReleasePointerCapture();
			m_CapturingMouse = false;
		}
		else
		{
			sender->SetPointerCapture();
			m_CapturingMouse = true;
			//Input::Instance().UpdateMousePosition(args->CurrentPoint->RawPosition.X, args->CurrentPoint->RawPosition.Y);
		}
	}

	void OnPointerMoved(CoreWindow^ sender, PointerEventArgs^ args)
	{
		if (!m_CapturingMouse)
			return;
		//Input::Instance().UpdateMousePosition(args->CurrentPoint->RawPosition.X, args->CurrentPoint->RawPosition.Y);
	}

	// Event handlers
	void OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args)
	{
		if (args->Kind == ActivationKind::Launch)
		{
			auto launchArgs = static_cast<LaunchActivatedEventArgs^>(args);

			char commandline[256];
			ToMultibyte(launchArgs->Arguments->Data(), commandline, 256);
			CommandLine::Parse(commandline);
			if (launchArgs->PrelaunchActivated)
			{
				// Opt-out of Prelaunch
				CoreApplication::Exit();
				return;
			}
		}
		m_CapturingMouse = false;
		m_DPI = DisplayInformation::GetForCurrentView()->LogicalDpi;
		ApplicationView::PreferredLaunchWindowingMode = ApplicationViewWindowingMode::PreferredLaunchViewSize;
		// TODO: Change to ApplicationViewWindowingMode::FullScreen to default to full screen
		auto desiredSize = Size(ConvertPixelsToDips(gWindowWidth), ConvertPixelsToDips(gWindowHeight));
		ApplicationView::PreferredLaunchViewSize = desiredSize;
		auto view = ApplicationView::GetForCurrentView();
		auto minSize = Size(ConvertPixelsToDips(320), ConvertPixelsToDips(200));
		view->SetPreferredMinSize(minSize);
		CoreWindow::GetForCurrentThread()->Activate();
		view->FullScreenSystemOverlayMode = FullScreenSystemOverlayMode::Minimal;
		view->TryResizeView(desiredSize);
	}

	void OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args)
	{
	}

	void OnResuming(Platform::Object^ sender, Platform::Object^ args)
	{
	}

	void OnWindowSizeChanged(CoreWindow^ sender, WindowSizeChangedEventArgs^ args)
	{
		m_LogicalWidth = sender->Bounds.Width;
		m_LogicalHeight = sender->Bounds.Height;

		if (m_InSizeMove)
			return;

		HandleWindowSizeChanged();
	}

#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
	void OnResizeStarted(CoreWindow^ sender, Platform::Object^ args)
	{
		m_InSizeMove = true;
		Time::Stop();
	}

	void OnResizeCompleted(CoreWindow^ sender, Platform::Object^ args)
	{
		m_InSizeMove = false;
		HandleWindowSizeChanged();
		Time::Start();
	}
#endif

	void OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
	{

	}

	void OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
	{
		m_Exit = true;
	}

	void OnAcceleratorKeyActivated(CoreDispatcher^, AcceleratorKeyEventArgs^ args)
	{
		if (args->EventType == CoreAcceleratorKeyEventType::SystemKeyDown
			&& args->VirtualKey == VirtualKey::Enter
			&& args->KeyStatus.IsMenuKeyDown
			&& !args->KeyStatus.WasKeyDown)
		{
			// Implements the classic ALT+ENTER fullscreen toggle
			auto view = ApplicationView::GetForCurrentView();

			if (view->IsFullScreenMode)
				view->ExitFullScreenMode();
			else
				view->TryEnterFullScreenMode();

			args->Handled = true;
		}
	}

	void OnBackRequested(Platform::Object^, Windows::UI::Core::BackRequestedEventArgs^ args)
	{
		// UWP on Xbox One triggers a back request whenever the B button is pressed
		// which can result in the app being suspended if unhandled
		args->Handled = true;
	}

	void OnDpiChanged(DisplayInformation^ sender, Object^ args)
	{
		m_DPI = sender->LogicalDpi;
		HandleWindowSizeChanged();
	}

	void OnOrientationChanged(DisplayInformation^ sender, Object^ args)
	{
		auto resizeManager = CoreWindowResizeManager::GetForCurrentView();
		resizeManager->ShouldWaitForLayoutCompletion = true;
		HandleWindowSizeChanged();
		resizeManager->NotifyLayoutCompleted();
	}

	void OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args)
	{
	}

private:
	bool m_Exit;
	bool m_Visible;
	bool m_InSizeMove;
	float m_DPI;
	float m_LogicalWidth;
	float m_LogicalHeight;
	bool m_CapturingMouse = false;
	std::unique_ptr<Graphics> m_pGraphics;

	inline int ConvertDipsToPixels(float dips) const
	{
		return int(dips * m_DPI / 96.f + 0.5f);
	}

	inline float ConvertPixelsToDips(int pixels) const
	{
		return (float(pixels) * 96.f / m_DPI);
	}

	void HandleWindowSizeChanged()
	{
		int outputWidth = ConvertDipsToPixels(m_LogicalWidth);
		int outputHeight = ConvertDipsToPixels(m_LogicalHeight);
		m_pGraphics->OnResize(outputWidth, outputHeight);
	}
};

ref class ViewProviderFactory : IFrameworkViewSource
{
public:
	virtual IFrameworkView^ CreateView()
	{
		return ref new ViewProvider();
	}
};

[Platform::MTAThread]
int __cdecl main(Platform::Array<Platform::String^>^ argv)
{
	Console::Initialize();

	auto viewProviderFactory = ref new ViewProviderFactory();
	CoreApplication::Run(viewProviderFactory);
	return 0;
}

#endif