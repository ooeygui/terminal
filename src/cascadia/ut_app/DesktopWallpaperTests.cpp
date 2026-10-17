// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "../TerminalApp/DesktopWallpaperGeometry.h"
#include "../TerminalApp/DesktopWallpaper.h"
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>

using namespace WEX::TestExecution;
using namespace TerminalApp::DesktopWallpaper;

namespace TerminalAppUnitTests
{
    class DesktopWallpaperTests
    {
        BEGIN_TEST_CLASS(DesktopWallpaperTests)
            TEST_CLASS_PROPERTY(L"ThreadingModel", L"MTA")
        END_TEST_CLASS()
        TEST_METHOD(WallpaperLayouts);
        TEST_METHOD(SpanningWallpaper);
        TEST_METHOD(NegativeMonitorCoordinates);
        TEST_METHOD(MonitorBoundaryAtMixedDpi);
        TEST_METHOD(WindowMovement);
        TEST_METHOD(SolidColorRendering);
        TEST_METHOD(TiledAndSpanningWallpaperRendering);
        TEST_METHOD(DesktopWallpaperEffects);
        TEST_METHOD(NativeWallpaperPresentation);
    };

    void DesktopWallpaperTests::WallpaperLayouts()
    {
        const RECT bounds{ 0, 0, 1920, 1080 };
        const auto fill = ImageRect(1000, 1000, bounds, DWPOS_FILL);
        VERIFY_ARE_EQUAL(0.0f, fill.left);
        VERIFY_ARE_EQUAL(-420.0f, fill.top);
        VERIFY_ARE_EQUAL(1920.0f, fill.right);
        VERIFY_ARE_EQUAL(1500.0f, fill.bottom);

        const auto fit = ImageRect(1000, 1000, bounds, DWPOS_FIT);
        VERIFY_ARE_EQUAL(420.0f, fit.left);
        VERIFY_ARE_EQUAL(0.0f, fit.top);
        VERIFY_ARE_EQUAL(1500.0f, fit.right);
        VERIFY_ARE_EQUAL(1080.0f, fit.bottom);

        const auto center = ImageRect(1000, 1000, bounds, DWPOS_CENTER);
        VERIFY_ARE_EQUAL(460.0f, center.left);
        VERIFY_ARE_EQUAL(40.0f, center.top);
        VERIFY_ARE_EQUAL(1460.0f, center.right);
        VERIFY_ARE_EQUAL(1040.0f, center.bottom);

        const auto stretch = ImageRect(1000, 1000, bounds, DWPOS_STRETCH);
        VERIFY_ARE_EQUAL(0.0f, stretch.left);
        VERIFY_ARE_EQUAL(0.0f, stretch.top);
        VERIFY_ARE_EQUAL(1920.0f, stretch.right);
        VERIFY_ARE_EQUAL(1080.0f, stretch.bottom);
    }

    void DesktopWallpaperTests::SpanningWallpaper()
    {
        const RECT desktop{ -1920, -1080, 1920, 1080 };
        const auto span = ImageRect(3840, 2160, desktop, DWPOS_SPAN);
        VERIFY_ARE_EQUAL(-1920.0f, span.left);
        VERIFY_ARE_EQUAL(-1080.0f, span.top);
        VERIFY_ARE_EQUAL(1920.0f, span.right);
        VERIFY_ARE_EQUAL(1080.0f, span.bottom);
    }

    void DesktopWallpaperTests::NegativeMonitorCoordinates()
    {
        const RECT monitor{ -2560, -1440, 0, 0 };
        const auto rect = MonitorRect(monitor, -2400, -1200, 2.0);
        VERIFY_ARE_EQUAL(-80.0f, rect.left);
        VERIFY_ARE_EQUAL(-120.0f, rect.top);
        VERIFY_ARE_EQUAL(1200.0f, rect.right);
        VERIFY_ARE_EQUAL(600.0f, rect.bottom);
    }

    void DesktopWallpaperTests::MonitorBoundaryAtMixedDpi()
    {
        const RECT left{ -1920, 0, 0, 1080 };
        const RECT right{ 0, -360, 2560, 1080 };
        // All monitor pixels use the XAML island's scale, not each monitor's DPI.
        for (const auto scale : { 1.0, 1.25, 1.5, 2.0 })
        {
            const auto l = MonitorRect(left, -200, 100, scale);
            const auto r = MonitorRect(right, -200, 100, scale);
            VERIFY_ARE_EQUAL(l.right, r.left);
            VERIFY_ARE_EQUAL(static_cast<float>(200 / scale), r.left);
            VERIFY_ARE_EQUAL(static_cast<float>(-460 / scale), r.top);
            VERIFY_ARE_EQUAL(static_cast<float>(980 / scale), l.bottom);
        }
    }

    void DesktopWallpaperTests::WindowMovement()
    {
        const RECT monitor{ 1920, 0, 4480, 1440 };
        for (const auto origin : { 1800.0, 1919.0, 1920.0, 1921.0, 2200.0 })
        {
            const auto rect = MonitorRect(monitor, origin, 120, 1.5);
            VERIFY_ARE_EQUAL(static_cast<float>((1920 - origin) / 1.5), rect.left);
            VERIFY_ARE_EQUAL(-80.0f, rect.top);
            VERIFY_ARE_EQUAL(880.0f, rect.bottom);
        }
    }

    void DesktopWallpaperTests::SolidColorRendering()
    {
        const auto apartment = wil::CoInitializeEx(COINIT_MULTITHREADED);
        const MonitorSource sources[]{
            { { -20, -10, 0, 10 }, L"" },
            { { 0, 0, 30, 20 }, L"" }
        };
        const auto monitors = Render(sources, DWPOS_FILL, RGB(12, 34, 56));
        VERIFY_ARE_EQUAL(size_t{ 2 }, monitors.size());
        for (const auto& monitor : monitors)
        {
            const auto width = monitor.bounds.right - monitor.bounds.left;
            const auto height = monitor.bounds.bottom - monitor.bounds.top;
            VERIFY_ARE_EQUAL(width, monitor.bitmap.PixelWidth());
            VERIFY_ARE_EQUAL(height, monitor.bitmap.PixelHeight());
            winrt::Windows::Storage::Streams::Buffer buffer(width * height * 4);
            monitor.bitmap.CopyToBuffer(buffer);
            const auto pixels = buffer.data();
            bool matches = true;
            for (uint32_t i = 0; i < buffer.Length(); i += 4)
            {
                matches = matches && pixels[i] == 56 && pixels[i + 1] == 34 &&
                          pixels[i + 2] == 12 && pixels[i + 3] == 255;
            }

            VERIFY_IS_TRUE(matches);
        }
    }

    void DesktopWallpaperTests::TiledAndSpanningWallpaperRendering()
    {
        const auto apartment = wil::CoInitializeEx(COINIT_MULTITHREADED);
        wchar_t folder[MAX_PATH];
        wchar_t path[MAX_PATH];
        VERIFY_IS_TRUE(GetTempPathW(MAX_PATH, folder) != 0);
        VERIFY_IS_TRUE(GetTempFileNameW(folder, L"wtw", 0, path) != 0);
        const auto cleanup = wil::scope_exit([&] { DeleteFileW(path); });
        {
            const auto wic = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
            winrt::com_ptr<IWICStream> stream;
            VERIFY_SUCCEEDED(wic->CreateStream(stream.put()));
            VERIFY_SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE));
            winrt::com_ptr<IWICBitmapEncoder> encoder;
            VERIFY_SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
            VERIFY_SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
            winrt::com_ptr<IWICBitmapFrameEncode> frame;
            VERIFY_SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr));
            VERIFY_SUCCEEDED(frame->Initialize(nullptr));
            VERIFY_SUCCEEDED(frame->SetSize(2, 2));
            auto format = GUID_WICPixelFormat24bppBGR;
            VERIFY_SUCCEEDED(frame->SetPixelFormat(&format));
            VERIFY_IS_TRUE(IsEqualGUID(format, GUID_WICPixelFormat24bppBGR));
            BYTE pixels[]{ 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255 };
            VERIFY_SUCCEEDED(frame->WritePixels(2, 6, sizeof(pixels), pixels));
            VERIFY_SUCCEEDED(frame->Commit());
            VERIFY_SUCCEEDED(encoder->Commit());
        }

        const MonitorSource whole[]{ { { -4, -2, 4, 2 }, path } };
        const MonitorSource split[]{
            { { -4, -2, 0, 2 }, path },
            { { 0, -2, 4, 2 }, path }
        };
        for (const auto position : { DWPOS_TILE, DWPOS_SPAN })
        {
            const auto desktop = Render(whole, position, RGB(0, 0, 0));
            const auto monitors = Render(split, position, RGB(0, 0, 0));
            winrt::Windows::Storage::Streams::Buffer expected(8 * 4 * 4);
            desktop[0].bitmap.CopyToBuffer(expected);
            for (size_t i = 0; i < monitors.size(); ++i)
            {
                winrt::Windows::Storage::Streams::Buffer actual(4 * 4 * 4);
                monitors[i].bitmap.CopyToBuffer(actual);
                bool matches = true;
                for (uint32_t y = 0; y < 4; ++y)
                {
                    for (uint32_t x = 0; x < 4 * 4; ++x)
                    {
                        matches = matches && actual.data()[y * 16 + x] == expected.data()[y * 32 + i * 16 + x];
                    }
                }
                VERIFY_IS_TRUE(matches);
            }
        }

        const auto plain = Render(whole, DWPOS_TILE, RGB(0, 0, 0));
        Effects effects;
        effects.blur = 1;
        const auto blurred = Render(whole, DWPOS_TILE, RGB(0, 0, 0), effects);
        winrt::Windows::Storage::Streams::Buffer before(8 * 4 * 4), after(8 * 4 * 4);
        plain[0].bitmap.CopyToBuffer(before);
        blurred[0].bitmap.CopyToBuffer(after);
        int originalVariation = 0, blurredVariation = 0;
        for (uint32_t y = 0; y < 4; ++y)
        {
            for (uint32_t x = 0; x < 8; ++x)
            {
                const auto pixel = y * 32 + x * 4;
                VERIFY_ARE_EQUAL(uint8_t{ 255 }, after.data()[pixel + 3]);
                if (x > 0)
                {
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        originalVariation += std::abs(before.data()[pixel + channel] - before.data()[pixel + channel - 4]);
                        blurredVariation += std::abs(after.data()[pixel + channel] - after.data()[pixel + channel - 4]);
                    }
                }
            }
        }
        VERIFY_IS_TRUE(blurredVariation < originalVariation / 2);
    }

    void DesktopWallpaperTests::DesktopWallpaperEffects()
    {
        const MonitorSource source[]{ { { 0, 0, 16, 16 }, L"" } };
        const auto check = [&](const Effects& effects, const BYTE r, const BYTE g, const BYTE b) {
            const auto monitors = Render(source, DWPOS_FILL, RGB(100, 150, 200), effects);
            winrt::Windows::Storage::Streams::Buffer pixels(16 * 16 * 4);
            monitors[0].bitmap.CopyToBuffer(pixels);
            bool matches = true;
            for (uint32_t i = 0; i < pixels.Length(); i += 4)
            {
                matches = matches && std::abs(pixels.data()[i] - b) <= 1 &&
                          std::abs(pixels.data()[i + 1] - g) <= 1 &&
                          std::abs(pixels.data()[i + 2] - r) <= 1 &&
                          pixels.data()[i + 3] == 255;
            }
            VERIFY_IS_TRUE(matches);
        };
        check({}, 100, 150, 200);
        Effects effects;
        effects.saturation = 0;
        check(effects, 143, 143, 143);
        effects.tint = RGB(20, 40, 60);
        effects.tintOpacity = 1;
        check(effects, 20, 40, 60);
        effects.saturation = 1;
        effects.tintOpacity = 0.5f;
        check(effects, 60, 95, 130);
        effects.blur = 2;
        check(effects, 60, 95, 130);
        effects.blur = -1;
        VERIFY_THROWS(Render(source, DWPOS_FILL, RGB(0, 0, 0), effects), wil::ResultException);
    }

    void DesktopWallpaperTests::NativeWallpaperPresentation()
    {
        wil::unique_hwnd parent{ CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
        VERIFY_IS_NOT_NULL(parent.get());
        const MonitorSource sources[]{
            { { -64, 0, 0, 64 }, L"" },
            { { 0, 0, 64, 64 }, L"" }
        };
        const auto monitors = Render(sources, DWPOS_FILL, RGB(12, 34, 56));
        {
            Presenter presenter{ parent.get() };
            presenter.SetMonitors(monitors);
            for (const auto x : { -32, -1, 0, 1, 32 })
            {
                presenter.Position({ 0, 0, 64, 64 }, x, 0, 1);
            }
            presenter.Position({ 0, 0, 64, 64 }, 0, 0, 0.5f);
            presenter.Position({}, 0, 0, 0.5f);
            presenter.Position({ 0, 0, 32, 32 }, 0, 0, 1);
            presenter.SetMonitors(monitors);
            presenter.Position({ 0, 0, 32, 32 }, 0, 0, 1);
        }
        VERIFY_IS_NULL(GetWindow(parent.get(), GW_CHILD));
    }
}
