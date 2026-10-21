// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Profiles_Appearance.h"
#include "Appearances.h"

#include "ProfileViewModel.h"
#include "PreviewConnection.h"

#include "Profiles_Appearance.g.cpp"
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Navigation;

static constexpr std::wstring_view AppearanceSettingPrefix{ L"App." };

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    Profiles_Appearance::Profiles_Appearance()
    {
        InitializeComponent();
        _previewConnection = winrt::make_self<PreviewConnection>();
    }

    void Profiles_Appearance::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _Profile = args.ViewModel().as<Editor::ProfileViewModel>();
        _weakWindowRoot = args.WindowRoot();
        _previewActive = true;

        // Settings are stored in Profiles_Appearance and Appearances.
        // We use the "App." prefix to indicate if it's in Appearances,
        // and remove it on the way to Appearances object.
        const auto elementToFocus = args.ElementToFocus();
        if (elementToFocus.starts_with(AppearanceSettingPrefix))
        {
            std::wstring correctedName{ elementToFocus.c_str() };
            get_self<implementation::Appearances>(DefaultAppearanceView())->BringIntoViewWhenLoaded(hstring{ correctedName.substr(AppearanceSettingPrefix.size()) });
        }
        else
        {
            BringIntoViewWhenLoaded(elementToFocus);
        }

        if (!_previewControl)
        {
            const auto settings = winrt::get_self<implementation::ProfileViewModel>(_Profile)->TermSettings();
            _previewConnection->DisplayPowerlineGlyphs(_Profile.DefaultAppearance().HasPowerlineCharacters());
            _previewControl = Control::TermControl(settings, settings, *_previewConnection);
            _previewControl.CursorVisibility(Control::CursorDisplayState::Shown);
            _previewControl.IsEnabled(false);
            _previewControl.AllowFocusWhenDisabled(false);
            PreviewContent().Children().Append(_previewControl);
        }

        // Subscribe to some changes in the view model
        // These changes should force us to update our own set of "Current<Setting>" members,
        // and propagate those changes to the UI
        _ViewModelChangedRevoker = _Profile.PropertyChanged(winrt::auto_revoke, { this, &Profiles_Appearance::_onProfilePropertyChanged });
        // The Appearances object handles updating the values in the settings UI, but
        // we still need to listen to the changes here just to update the preview control
        _AppearanceViewModelChangedRevoker = _Profile.DefaultAppearance().PropertyChanged(winrt::auto_revoke, { this, &Profiles_Appearance::_onProfilePropertyChanged });
        _UpdatePreviewWallpaper();

        TraceLoggingWrite(
            g_hTerminalSettingsEditorProvider,
            "NavigatedToPage",
            TraceLoggingDescription("Event emitted when the user navigates to a page in the settings UI"),
            TraceLoggingValue("profile.appearance", "PageId", "The identifier of the page that was navigated to"),
            TraceLoggingValue(_Profile.IsBaseLayer(), "IsProfileDefaults", "If the modified profile is the profile.defaults object"),
            TraceLoggingValue(static_cast<GUID>(_Profile.Guid()), "ProfileGuid", "The guid of the profile that was navigated to"),
            TraceLoggingValue(_Profile.Source().c_str(), "ProfileSource", "The source of the profile that was navigated to"),
            TraceLoggingValue(_Profile.DefaultAppearance().BackgroundImageSettingsEnabled(), "HasBackgroundImage", "If the profile has a background image defined"),
            TraceLoggingValue(_Profile.HasUnfocusedAppearance(), "HasUnfocusedAppearance", "If the profile has an unfocused appearance defined"),
            TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
            TelemetryPrivacyDataTag(PDT_ProductAndServiceUsage));
    }

    void Profiles_Appearance::OnNavigatedFrom(const NavigationEventArgs& /*e*/)
    {
        _previewActive = false;
        ++_previewWallpaperGeneration;
        _previewWallpaperEffects.reset();
        _ViewModelChangedRevoker.revoke();
        _AppearanceViewModelChangedRevoker.revoke();
    }

    void Profiles_Appearance::_onProfilePropertyChanged(const IInspectable&, const PropertyChangedEventArgs&)
    {
        ++_previewWallpaperGeneration;
        if (!_updatePreviewControl)
        {
            _updatePreviewControl = std::make_shared<ThrottledFunc<>>(
                winrt::Windows::System::DispatcherQueue::GetForCurrentThread(),
                til::throttled_func_options{
                    .delay = std::chrono::milliseconds{ 100 },
                    .debounce = true,
                    .trailing = true,
                },
                [this]() {
                    if (!_previewActive)
                    {
                        return;
                    }
                    const auto profile = winrt::get_self<implementation::ProfileViewModel>(_Profile);
                    const auto settings = profile->TermSettings(_previewWallpaperVisible && profile->UseDesktopWallpaperForPreview());
                    _previewConnection->DisplayPowerlineGlyphs(_Profile.DefaultAppearance().HasPowerlineCharacters());
                    _previewControl.UpdateControlSettings(settings, settings);
                    _UpdatePreviewWallpaper();
                });
        }

        _updatePreviewControl->Run();
    }

    winrt::fire_and_forget Profiles_Appearance::_UpdatePreviewWallpaper()
    try
    {
        const auto profile = winrt::get_self<implementation::ProfileViewModel>(_Profile);
        if (!profile->UseDesktopWallpaperForPreview())
        {
            ++_previewWallpaperGeneration;
            _previewWallpaperEffects.reset();
            _previewWallpaperVisible = false;
            DesktopWallpaperPreview().Source(nullptr);
            co_return;
        }
        const auto appearance = _Profile.DefaultAppearance();
        DesktopWallpaperPreview().Opacity(appearance.BackgroundImageOpacity());
        const auto tint = appearance.DesktopWallpaperTint();
        const ::Microsoft::Terminal::UI::DesktopWallpaper::Effects effects{
            appearance.DesktopWallpaperBlur(),
            appearance.DesktopWallpaperSaturation(),
            RGB(tint.R, tint.G, tint.B),
            appearance.DesktopWallpaperTintOpacity()
        };
        const auto generation = ++_previewWallpaperGeneration;
        if (_previewWallpaperEffects == effects)
        {
            co_return;
        }
        const auto window = reinterpret_cast<HWND>(WindowRoot().GetHostingWindow());
        RECT windowBounds{};
        THROW_IF_WIN32_BOOL_FALSE(GetWindowRect(window, &windowBounds));
        const auto dpi = GetDpiForWindow(window);
        const auto width = MulDiv(400, dpi, 96);
        const auto height = MulDiv(180, dpi, 96);
        const auto x = windowBounds.left + (windowBounds.right - windowBounds.left - width) / 2;
        const auto y = windowBounds.top + (windowBounds.bottom - windowBounds.top - height) / 2;
        const RECT bounds{ x, y, x + width, y + height };
        const auto weak = get_weak();
        const winrt::apartment_context uiThread;
        winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap{ nullptr };
        co_await winrt::resume_background();
        try
        {
            const auto monitors = ::Microsoft::Terminal::UI::DesktopWallpaper::Load(effects);
            bitmap = ::Microsoft::Terminal::UI::DesktopWallpaper::CreatePreview(monitors, bounds);
        }
        CATCH_LOG();
        co_await uiThread;
        if (const auto page = weak.get(); page && page->_previewActive && page->_previewWallpaperGeneration == generation && bitmap)
        {
            Media::Imaging::SoftwareBitmapSource source;
            co_await source.SetBitmapAsync(bitmap);
            if (!page->_previewActive || page->_previewWallpaperGeneration != generation)
            {
                co_return;
            }
            // Retain the last image until its replacement is ready, just like the window presenter.
            page->DesktopWallpaperPreview().Source(source);
            page->_previewWallpaperEffects = effects;
            page->_previewWallpaperVisible = true;
            const auto settings = winrt::get_self<implementation::ProfileViewModel>(page->_Profile)->TermSettings(true);
            page->_previewControl.UpdateControlSettings(settings, settings);
        }
    }
    CATCH_LOG();
}
