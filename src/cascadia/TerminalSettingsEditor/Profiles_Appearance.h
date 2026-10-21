// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <ThrottledFunc.h>

#include "Profiles_Appearance.g.h"
#include "PreviewConnection.h"
#include "Utils.h"
#include <DesktopWallpaper.h>

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct Profiles_Appearance : public HasScrollViewer<Profiles_Appearance>, Profiles_AppearanceT<Profiles_Appearance>
    {
    public:
        Profiles_Appearance();

        void OnNavigatedTo(const Windows::UI::Xaml::Navigation::NavigationEventArgs& e);
        void OnNavigatedFrom(const Windows::UI::Xaml::Navigation::NavigationEventArgs& e);

        Editor::IHostedInWindow WindowRoot() const noexcept { return _weakWindowRoot.get(); };

        til::property_changed_event PropertyChanged;
        WINRT_PROPERTY(Editor::ProfileViewModel, Profile, nullptr);

    private:
        void _onProfilePropertyChanged(const IInspectable&, const PropertyChangedEventArgs&);
        winrt::fire_and_forget _UpdatePreviewWallpaper();

        winrt::com_ptr<PreviewConnection> _previewConnection{ nullptr };
        Microsoft::Terminal::Control::TermControl _previewControl{ nullptr };
        std::shared_ptr<ThrottledFunc<>> _updatePreviewControl;
        Windows::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker _ViewModelChangedRevoker;
        Windows::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker _AppearanceViewModelChangedRevoker;
        winrt::weak_ref<Editor::IHostedInWindow> _weakWindowRoot;
        std::optional<::Microsoft::Terminal::UI::DesktopWallpaper::Effects> _previewWallpaperEffects;
        uint64_t _previewWallpaperGeneration{ 0 };
        bool _previewActive{ false };
        bool _previewWallpaperVisible{ false };
    };
};

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(Profiles_Appearance);
}
