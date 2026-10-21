// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <shobjidl_core.h>
#include <dcomp.h>
#include <d3d11.h>
#include <winrt/Windows.Graphics.Imaging.h>

namespace TerminalApp::DesktopWallpaper
{
    struct Monitor
    {
        RECT bounds;
        winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap{ nullptr };
    };

    struct MonitorSource
    {
        RECT bounds;
        std::wstring path;
    };

    struct Effects
    {
        float blur{ 0 };
        float saturation{ 1 };
        COLORREF tint{ RGB(0, 0, 0) };
        float tintOpacity{ 0 };

        bool operator==(const Effects&) const = default;
    };

    class Presenter
    {
    public:
        explicit Presenter(HWND parent);
        bool IsDeviceValid() const;
        void SetMonitors(std::span<const Monitor> monitors);
        void Position(const RECT& bounds, double originX, double originY, float opacity);

    private:
        struct MonitorVisual
        {
            RECT bounds;
            winrt::com_ptr<IDCompositionVisual2> visual;
        };

        winrt::com_ptr<ID3D11Device> _graphicsDevice;
        winrt::com_ptr<IDCompositionDesktopDevice> _device;
        winrt::com_ptr<IDCompositionTarget> _target;
        winrt::com_ptr<IDCompositionVisual2> _root;
        winrt::com_ptr<IDCompositionEffectGroup> _opacity;
        std::vector<MonitorVisual> _monitors;
        RECT _bounds{};
        double _originX{ 0 };
        double _originY{ 0 };
        float _currentOpacity{ 1 };
        bool _positioned{ false };
        bool _visible{ false };
    };

    bool ShouldUseDesktopRelativeWallpaper(bool useBackgroundImageForWindow, bool relativeToDesktop, std::wstring_view imagePath) noexcept;

    // Called off the UI thread. Movement reuses these bitmaps.
    std::vector<Monitor> Load(const Effects& effects = {});
    std::vector<Monitor> Load(IDesktopWallpaper* wallpaper, const Effects& effects = {});
    std::vector<Monitor> Render(std::span<const MonitorSource> sources, DESKTOP_WALLPAPER_POSITION position, COLORREF background, const Effects& effects = {});
}
