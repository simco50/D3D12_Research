#include "stdafx.h"
#include "Graphics/Core/Graphics.h"
#include "Core/Input.h"
#include "Core/Console.h"

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include "Core/CommandLine.h"
#include "Core/TaskQueue.h"

#define BREAK_ON_ALLOC 0

const int gWindowWidth = 1240;
const int gWindowHeight = 720;
const int gMsaaSampleCount = 1;

#if PLATFORM_WINDOWS

class ViewWrapper
{
public:
	int Run(HINSTANCE hInstance, const char* pTitle, const char* lpCmdLine)
	{
		Thread::SetMainThread();
		CommandLine::Parse(lpCmdLine);

		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

#if BREAK_ON_ALLOC > 0
		_CrtSetBreakAlloc(BREAK_ON_ALLOC);
#endif

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
			OPTICK_FRAME("MainThread");
			while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageA(&msg);

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

		OPTICK_SHUTDOWN();
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
			int newWidth = LOWORD(lParam);
			int newHeight = HIWORD(lParam);
			bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
			bool shouldResize = false;

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
					shouldResize = true;
				}
				else if (wParam == SIZE_RESTORED)
				{
					// Restoring from minimized state?
					if (m_Minimized)
					{
						Time::Start();
						m_Minimized = false;
						shouldResize = true;
					}
					// Restoring from maximized state?
					else if (m_Maximized)
					{
						Time::Start();
						m_Maximized = false;
						shouldResize = true;
					}
					else if (!m_IsResizing) // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
					{
						shouldResize = true;
					}
				}
			}

			if (shouldResize && resized)
			{
				OnResize();
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
			Input::Instance().UpdateMouseKey(VK_LBUTTON, true);
			break;
		case WM_MBUTTONDOWN:
			Input::Instance().UpdateMouseKey(VK_MBUTTON, true);
			break;
		case WM_RBUTTONDOWN:
			Input::Instance().UpdateMouseKey(VK_RBUTTON, true);
			break;
		case WM_LBUTTONUP:
			Input::Instance().UpdateMouseKey(VK_LBUTTON, false);
			break;
		case WM_MBUTTONUP:
			Input::Instance().UpdateMouseKey(VK_MBUTTON, false);
			break;
		case WM_RBUTTONUP:
			Input::Instance().UpdateMouseKey(VK_RBUTTON, false);
			break;
		case WM_ENTERSIZEMOVE:
			Time::Stop();
			m_IsResizing = true;
			break;
		case WM_EXITSIZEMOVE:
			Time::Start();
			RECT rect;
			GetClientRect(hWnd, &rect);
			int newWidth = rect.right - rect.left;
			int newHeight = rect.bottom - rect.top;
			bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
			if (resized)
			{
				OnResize();
			}
			m_IsResizing = false;
			break;
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	HWND MakeWindow(HINSTANCE hInstance, const char* pTitle)
	{
		::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

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

#elif PLATFORM_UWP

#include "winrt/Windows.ApplicationModel.h"
#include "winrt/Windows.ApplicationModel.Core.h"
#include "winrt/Windows.ApplicationModel.Activation.h"
#include "winrt/Windows.Foundation.h"
#include "winrt/Windows.Graphics.Display.h"
#include "winrt/Windows.System.h"
#include "winrt/Windows.UI.Core.h"
#include "winrt/Windows.UI.Input.h"
#include "winrt/Windows.UI.ViewManagement.h"

using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::UI::ViewManagement;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics::Display;

class ViewProvider : public winrt::implements<ViewProvider, IFrameworkView>
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
	virtual void Initialize(const CoreApplicationView& applicationView)
	{
		applicationView.Activated({ this, &ViewProvider::OnActivated });
		m_pGraphics = std::make_unique<Graphics>(gWindowWidth, gWindowHeight, gMsaaSampleCount);
	}

	virtual void Uninitialize()
	{
		m_pGraphics.reset();
	}

	virtual void SetWindow(const CoreWindow& window)
	{
		window.SizeChanged({ this, &ViewProvider::OnWindowSizeChanged });
		window.Closed({ this, &ViewProvider::OnWindowClosed });
		window.KeyDown({ this, &ViewProvider::OnKeyDown });
		window.KeyUp({ this, &ViewProvider::OnKeyUp });
		window.PointerPressed({ this, &ViewProvider::OnPointerPressed });
		window.PointerReleased({ this, &ViewProvider::OnPointerReleased });
		window.PointerMoved({ this, &ViewProvider::OnPointerMoved });

#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
		try
		{
			window.ResizeStarted({ this, &ViewProvider::OnResizeStarted });
			window.ResizeCompleted({ this, &ViewProvider::OnResizeCompleted });
		}
		catch (...)
		{
			// Requires Windows 10 Creators Update (10.0.15063) or later
		}
#endif

		auto dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
		dispatcher.AcceleratorKeyActivated({ this, &ViewProvider::OnAcceleratorKeyActivated });

		auto currentDisplayInformation = DisplayInformation::GetForCurrentView();
		currentDisplayInformation.DpiChanged({ this, &ViewProvider::OnDpiChanged });
		currentDisplayInformation.OrientationChanged({ this, &ViewProvider::OnOrientationChanged });

		DisplayInformation::DisplayContentsInvalidated({ this, &ViewProvider::OnDisplayContentsInvalidated });

		m_DPI = currentDisplayInformation.LogicalDpi();

		m_LogicalWidth = window.Bounds().Width;
		m_LogicalHeight = window.Bounds().Height;

		m_pGraphics->Initialize(&window);
	}

	virtual void Load(winrt::hstring const& entryPoint)
	{
	}

	virtual void Run()
	{
		while (!m_Exit)
		{
			if (m_Visible)
			{
				Input::Instance().Update();
				CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
				m_pGraphics->Update();
			}
			else
			{
				CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
			}
		}
	}

protected:

	void OnKeyDown(const CoreWindow& sender, const KeyEventArgs& args)
	{
		Input::Instance().UpdateKey((uint32)args.VirtualKey(), true);
	}

	void OnKeyUp(const CoreWindow& sender, const KeyEventArgs& args)
	{
		Input::Instance().UpdateKey((uint32)args.VirtualKey(), false);
	}

	void OnPointerReleased(const CoreWindow& sender, const PointerEventArgs& args)
	{
		Input::Instance().UpdateMouseKey(0, args.CurrentPoint().Properties().IsLeftButtonPressed());
		Input::Instance().UpdateMouseKey(1, args.CurrentPoint().Properties().IsRightButtonPressed());
		Input::Instance().UpdateMouseKey(2, args.CurrentPoint().Properties().IsHorizontalMouseWheel());
	}

	void OnPointerPressed(const CoreWindow& sender, const PointerEventArgs& args)
	{
		Input::Instance().UpdateMouseKey(0, args.CurrentPoint().Properties().IsLeftButtonPressed());
		Input::Instance().UpdateMouseKey(1, args.CurrentPoint().Properties().IsRightButtonPressed());
		Input::Instance().UpdateMouseKey(2, args.CurrentPoint().Properties().IsHorizontalMouseWheel());
	}

	void OnPointerMoved(const CoreWindow& sender, const PointerEventArgs& args)
	{
		Input::Instance().UpdateMousePosition(args.CurrentPoint().RawPosition().X, args.CurrentPoint().RawPosition().Y);
	}

	// Event handlers
	void OnActivated(const CoreApplicationView& applicationView, const IActivatedEventArgs& args)
	{
		if (args.Kind() == ActivationKind::Launch)
		{
			auto launchArgs = args.as<LaunchActivatedEventArgs>();

			CommandLine::Parse(UNICODE_TO_MULTIBYTE(launchArgs.Arguments().c_str()));
			if (launchArgs.PrelaunchActivated())
			{
				// Opt-out of Prelaunch
				CoreApplication::Exit();
				return;
			}
		}
		m_DPI = DisplayInformation::GetForCurrentView().LogicalDpi();
		ApplicationView::PreferredLaunchWindowingMode(ApplicationViewWindowingMode::PreferredLaunchViewSize);
		// TODO: Change to ApplicationViewWindowingMode::FullScreen to default to full screen
		auto desiredSize = Size(ConvertPixelsToDips(gWindowWidth), ConvertPixelsToDips(gWindowHeight));
		ApplicationView::PreferredLaunchViewSize(desiredSize);
		auto view = ApplicationView::GetForCurrentView();
		auto minSize = Size(ConvertPixelsToDips(320), ConvertPixelsToDips(200));
		view.SetPreferredMinSize(minSize);
		CoreWindow::GetForCurrentThread().Activate();
		view.FullScreenSystemOverlayMode(FullScreenSystemOverlayMode::Minimal);
		view.TryResizeView(desiredSize);
	}

	void OnWindowSizeChanged(const CoreWindow& sender, const WindowSizeChangedEventArgs& args)
	{
		m_LogicalWidth = sender.Bounds().Width;
		m_LogicalHeight = sender.Bounds().Height;

		if (m_InSizeMove)
			return;

		HandleWindowSizeChanged();
	}

#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
	void OnResizeStarted(const CoreWindow& sender, const IInspectable& args)
	{
		m_InSizeMove = true;
		Time::Stop();
	}

	void OnResizeCompleted(const CoreWindow& sender, const IInspectable& args)
	{
		m_InSizeMove = false;
		HandleWindowSizeChanged();
		Time::Start();
	}
#endif

	void OnVisibilityChanged(const CoreWindow& sender, const VisibilityChangedEventArgs& args)
	{

	}

	void OnWindowClosed(const CoreWindow& sender, const CoreWindowEventArgs& args)
	{
		m_Exit = true;
	}

	void OnAcceleratorKeyActivated(const CoreDispatcher&, const AcceleratorKeyEventArgs& args)
	{
		if (args.EventType()== CoreAcceleratorKeyEventType::SystemKeyDown
			&& args.VirtualKey()== VirtualKey::Enter
			&& args.KeyStatus().IsMenuKeyDown
			&& !args.KeyStatus().WasKeyDown)
		{
			// Implements the classic ALT+ENTER fullscreen toggle
			auto view = ApplicationView::GetForCurrentView();

			if (view.IsFullScreenMode())
				view.ExitFullScreenMode();
			else
				view.TryEnterFullScreenMode();

			args.Handled(true);
		}
	}

	void OnDpiChanged(const DisplayInformation& sender, const IInspectable& args)
	{
		m_DPI = sender.LogicalDpi();
		HandleWindowSizeChanged();
	}

	void OnOrientationChanged(const DisplayInformation& sender, const IInspectable& args)
	{
		auto resizeManager = CoreWindowResizeManager::GetForCurrentView();
		resizeManager.ShouldWaitForLayoutCompletion(true);
		HandleWindowSizeChanged();
		resizeManager.NotifyLayoutCompleted();
	}

	void OnDisplayContentsInvalidated(const DisplayInformation& sender, const IInspectable& args)
	{
	}

private:
	bool m_Exit;
	bool m_Visible;
	bool m_InSizeMove;
	float m_DPI;
	float m_LogicalWidth;
	float m_LogicalHeight;
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

class ViewProviderFactory : public winrt::implements<ViewProviderFactory, IFrameworkViewSource>
{
public:
	IFrameworkView CreateView()
	{
		return winrt::make<ViewProvider>();
	}
};

int WINAPI WinMain(_In_ HINSTANCE, _In_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	Console::Initialize();
	auto viewProviderFactory = winrt::make<ViewProviderFactory>();
	CoreApplication::Run(viewProviderFactory);
	return 0;
}

#endif
