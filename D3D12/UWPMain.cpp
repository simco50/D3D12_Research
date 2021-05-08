#include "stdafx.h"

#if PLATFORM_UWP

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

class UWPAppContainer : public winrt::implements<UWPAppContainer, IFrameworkView>
{
public:
	UWPAppContainer(const char* pTitle, uint32 width, uint32 height) :
		m_LogicalWidth((float)width),
		m_LogicalHeight((float)height),
		m_pTitle(pTitle),
		m_Exit(false),
		m_Visible(true),
		m_InSizeMove(false),
		m_DPI(96.f)
	{
	}

	// IFrameworkView methods
	virtual void Initialize(const CoreApplicationView& applicationView)
	{
		applicationView.Activated({ this, &UWPAppContainer::OnActivated });
	}

	virtual void Uninitialize()
	{
	}

	virtual void SetWindow(const CoreWindow& window)
	{
		window.SizeChanged({ this, &UWPAppContainer::OnWindowSizeChanged });
		window.Closed({ this, &UWPAppContainer::OnWindowClosed });
		window.KeyDown({ this, &UWPAppContainer::OnKeyDown });
		window.KeyUp({ this, &UWPAppContainer::OnKeyUp });
		window.PointerPressed({ this, &UWPAppContainer::OnPointerPressed });
		window.PointerReleased({ this, &UWPAppContainer::OnPointerReleased });
		window.PointerMoved({ this, &UWPAppContainer::OnPointerMoved });

#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
		try
		{
			window.ResizeStarted({ this, &UWPAppContainer::OnResizeStarted });
			window.ResizeCompleted({ this, &UWPAppContainer::OnResizeCompleted });
		}
		catch (...)
		{
			// Requires Windows 10 Creators Update (10.0.15063) or later
		}
#endif

		ApplicationView::GetForCurrentView().Title(MULTIBYTE_TO_UNICODE(m_pTitle));

		auto dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
		dispatcher.AcceleratorKeyActivated({ this, &UWPAppContainer::OnAcceleratorKeyActivated });

		auto currentDisplayInformation = DisplayInformation::GetForCurrentView();
		currentDisplayInformation.DpiChanged({ this, &UWPAppContainer::OnDpiChanged });
		currentDisplayInformation.OrientationChanged({ this, &UWPAppContainer::OnOrientationChanged });

		DisplayInformation::DisplayContentsInvalidated({ this, &UWPAppContainer::OnDisplayContentsInvalidated });

		m_DPI = currentDisplayInformation.LogicalDpi();

		m_LogicalWidth = window.Bounds().Width;
		m_LogicalHeight = window.Bounds().Height;

		m_pGraphics = std::make_unique<DemoApp>(&window, gMsaaSampleCount);
	}

	virtual void Load(winrt::hstring const& entryPoint)
	{
	}

	void Run()
	{
		Thread::SetMainThread();
		Console::Initialize();
		TaskQueue::Initialize(std::thread::hardware_concurrency());

		Time::Reset();

		while (PollMessages())
		{
			OPTICK_FRAME("MainThread");
			Time::Tick();
			m_pGraphics->Update();
			Input::Instance().Update();
		}

		TaskQueue::Shutdown();

		OPTICK_SHUTDOWN();
	}

	bool PollMessages()
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
		return !m_Exit;
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
		auto desiredSize = Size(ConvertPixelsToDips((int)m_LogicalWidth), ConvertPixelsToDips((int)m_LogicalHeight));
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
		if (args.EventType() == CoreAcceleratorKeyEventType::SystemKeyDown
			&& args.VirtualKey() == VirtualKey::Enter
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
	const char* m_pTitle;
	std::unique_ptr<DemoApp> m_pGraphics;

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

class UWPAppContainerFactory : public winrt::implements<UWPAppContainerFactory, IFrameworkViewSource>
{
public:
	IFrameworkView CreateView()
	{
		return winrt::make<UWPAppContainer>("D3D12", gWindowWidth, gWindowHeight);
	}
};

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	Console::Initialize();
	auto viewProviderFactory = winrt::make<UWPAppContainerFactory>();
	CoreApplication::Run(viewProviderFactory);
	return 0;
}

#endif
