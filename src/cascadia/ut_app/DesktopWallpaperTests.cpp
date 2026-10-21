// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "../WinRTUtils/inc/DesktopWallpaperGeometry.h"
#include "../WinRTUtils/inc/DesktopWallpaper.h"
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>
#include <wrl/implements.h>

using namespace WEX::TestExecution;
using namespace Microsoft::Terminal::UI::DesktopWallpaper;

namespace TerminalAppUnitTests
{
    class WallpaperWithStaleMonitor final :
        public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDesktopWallpaper>
    {
    public:
        HRESULT staleResult{ E_FAIL };
        bool staleWallpaperRequested{ false };

        STDMETHODIMP SetWallpaper(LPCWSTR, LPCWSTR) override { return E_NOTIMPL; }
        STDMETHODIMP GetWallpaper(LPCWSTR monitor, LPWSTR* path) override
        {
            staleWallpaperRequested |= wcscmp(monitor, L"stale") == 0;
            return CopyString(L"", path);
        }
        STDMETHODIMP GetMonitorDevicePathAt(UINT index, LPWSTR* path) override
        {
            const wchar_t* monitors[]{ L"left", L"stale", L"right" };
            return index < ARRAYSIZE(monitors) ? CopyString(monitors[index], path) : E_INVALIDARG;
        }
        STDMETHODIMP GetMonitorDevicePathCount(UINT* count) override
        {
            *count = 3;
            return S_OK;
        }
        STDMETHODIMP GetMonitorRECT(LPCWSTR monitor, RECT* bounds) override
        {
            *bounds = {};
            if (wcscmp(monitor, L"stale") == 0)
            {
                return staleResult;
            }
            *bounds = wcscmp(monitor, L"left") == 0 ? RECT{ -16, 0, 0, 16 } : RECT{ 0, 0, 16, 16 };
            return S_OK;
        }
        STDMETHODIMP SetBackgroundColor(COLORREF) override { return E_NOTIMPL; }
        STDMETHODIMP GetBackgroundColor(COLORREF* color) override
        {
            *color = RGB(12, 34, 56);
            return S_OK;
        }
        STDMETHODIMP SetPosition(DESKTOP_WALLPAPER_POSITION) override { return E_NOTIMPL; }
        STDMETHODIMP GetPosition(DESKTOP_WALLPAPER_POSITION* position) override
        {
            *position = DWPOS_FILL;
            return S_OK;
        }
        STDMETHODIMP SetSlideshow(IShellItemArray*) override { return E_NOTIMPL; }
        STDMETHODIMP GetSlideshow(IShellItemArray**) override { return E_NOTIMPL; }
        STDMETHODIMP SetSlideshowOptions(DESKTOP_SLIDESHOW_OPTIONS, UINT) override { return E_NOTIMPL; }
        STDMETHODIMP GetSlideshowOptions(DESKTOP_SLIDESHOW_OPTIONS*, UINT*) override { return E_NOTIMPL; }
        STDMETHODIMP AdvanceSlideshow(LPCWSTR, DESKTOP_SLIDESHOW_DIRECTION) override { return E_NOTIMPL; }
        STDMETHODIMP GetStatus(DESKTOP_SLIDESHOW_STATE*) override { return E_NOTIMPL; }
        STDMETHODIMP Enable(BOOL) override { return E_NOTIMPL; }

    private:
        static HRESULT CopyString(LPCWSTR text, LPWSTR* result)
        {
            auto copy = wil::make_cotaskmem_string_nothrow(text);
            *result = copy.release();
            return *result ? S_OK : E_OUTOFMEMORY;
        }
    };

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
        TEST_METHOD(DesktopWallpaperSelection);
        TEST_METHOD(SolidColorRendering);
        TEST_METHOD(TiledAndSpanningWallpaperRendering);
        TEST_METHOD(DesktopWallpaperEffects);
        TEST_METHOD(NativeWallpaperPresentation);
        TEST_METHOD(WallpaperDeviceRecovery);
        TEST_METHOD(WallpaperWithDisconnectedMonitor);
        TEST_METHOD(WallpaperUpdatesReusePresenter);
        TEST_METHOD(WallpaperPreview);
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

    void DesktopWallpaperTests::DesktopWallpaperSelection()
    {
        for (const auto path : { L"", L"desktopWallpaper", L"DESKTOPWALLPAPER" })
        {
            VERIFY_IS_TRUE(ShouldUseDesktopRelativeWallpaper(true, true, path));
            VERIFY_IS_FALSE(ShouldUseDesktopRelativeWallpaper(false, true, path));
            VERIFY_IS_FALSE(ShouldUseDesktopRelativeWallpaper(true, false, path));
            VERIFY_IS_FALSE(ShouldUseDesktopRelativeWallpaper(false, false, path));
        }

        for (const auto path : { L"C:\\background.png", L"ms-appx:///Images/background.png", L"https://example.com/background.png", L"desktopWallpaper.png" })
        {
            for (const auto wholeWindow : { false, true })
            {
                for (const auto relativeToDesktop : { false, true })
                {
                    VERIFY_IS_FALSE(ShouldUseDesktopRelativeWallpaper(wholeWindow, relativeToDesktop, path));
                }
            }
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
        const auto preview = CreatePreview(blurred, whole[0].bounds);
        winrt::Windows::Storage::Streams::Buffer previewPixels(8 * 4 * 4);
        preview.CopyToBuffer(previewPixels);
        VERIFY_ARE_EQUAL(after.Length(), previewPixels.Length());
        VERIFY_ARE_EQUAL(0, memcmp(after.data(), previewPixels.data(), after.Length()));
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
        effects.blur = 100;
        check(effects, 60, 95, 130);
        effects.blur = 100.5f;
        VERIFY_THROWS(Render(source, DWPOS_FILL, RGB(0, 0, 0), effects), wil::ResultException);
        effects.blur = -1;
        VERIFY_THROWS(Render(source, DWPOS_FILL, RGB(0, 0, 0), effects), wil::ResultException);
    }

    void DesktopWallpaperTests::WallpaperPreview()
    {
        const MonitorSource sources[]{
            { { -16, 0, 0, 16 }, L"" },
            { { 0, 0, 16, 16 }, L"" }
        };
        for (const auto saturation : { 0.0f, 1.0f })
        {
            Effects effects;
            effects.blur = 2;
            effects.saturation = saturation;
            const auto monitors = Render(sources, DWPOS_FILL, RGB(100, 150, 200), effects);
            const auto preview = CreatePreview(monitors, { -8, -2, 8, 8 });
            VERIFY_ARE_EQUAL(16, preview.PixelWidth());
            VERIFY_ARE_EQUAL(10, preview.PixelHeight());
            winrt::Windows::Storage::Streams::Buffer pixels(16 * 10 * 4);
            preview.CopyToBuffer(pixels);
            for (uint32_t y = 0; y < 10; ++y)
            {
                for (uint32_t x = 0; x < 16; ++x)
                {
                    const auto i = (y * 16 + x) * 4;
                    if (y < 2)
                    {
                        VERIFY_ARE_EQUAL(uint8_t{ 0 }, pixels.data()[i + 3]);
                    }
                    else
                    {
                        VERIFY_ARE_EQUAL(uint8_t{ 255 }, pixels.data()[i + 3]);
                        if (saturation == 0)
                        {
                            VERIFY_IS_TRUE(std::abs(pixels.data()[i] - pixels.data()[i + 1]) <= 1);
                            VERIFY_IS_TRUE(std::abs(pixels.data()[i + 1] - pixels.data()[i + 2]) <= 1);
                        }
                        else
                        {
                            VERIFY_IS_TRUE(std::abs(pixels.data()[i] - 200) <= 1);
                            VERIFY_IS_TRUE(std::abs(pixels.data()[i + 1] - 150) <= 1);
                            VERIFY_IS_TRUE(std::abs(pixels.data()[i + 2] - 100) <= 1);
                        }
                    }
                }
            }
            VERIFY_THROWS(CreatePreview(monitors, { 0, 0, 0, 16 }), wil::ResultException);
        }
    }

    void DesktopWallpaperTests::WallpaperWithDisconnectedMonitor()
    {
        const auto wallpaper = Microsoft::WRL::Make<WallpaperWithStaleMonitor>();
        VERIFY_IS_NOT_NULL(wallpaper.Get());
        for (const auto result : { S_FALSE, E_FAIL, E_INVALIDARG })
        {
            wallpaper->staleResult = result;
            const auto monitors = Load(wallpaper.Get());
            VERIFY_ARE_EQUAL(size_t{ 2 }, monitors.size());
            VERIFY_ARE_EQUAL(-16L, monitors[0].bounds.left);
            VERIFY_ARE_EQUAL(0L, monitors[1].bounds.left);
            VERIFY_ARE_EQUAL(16, monitors[0].bitmap.PixelWidth());
            VERIFY_ARE_EQUAL(16, monitors[1].bitmap.PixelHeight());
            VERIFY_IS_FALSE(wallpaper->staleWallpaperRequested);
        }
    }

    void DesktopWallpaperTests::WallpaperDeviceRecovery()
    {
        wil::unique_hwnd parent{ CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
        VERIFY_IS_NOT_NULL(parent.get());
        const MonitorSource originalSources[]{ { { 0, 0, 64, 64 }, L"" } };
        const auto cachedMonitors = Render(originalSources, DWPOS_FILL, RGB(12, 34, 56));
        auto presenter = std::make_unique<Presenter>(parent.get());
        presenter->SetMonitors(cachedMonitors);
        presenter->Position({ 0, 0, 64, 64 }, 0, 0, 1);
        VERIFY_IS_TRUE(presenter->IsDeviceValid());

        presenter.reset();

        presenter = std::make_unique<Presenter>(parent.get());
        presenter->SetMonitors(cachedMonitors);
        presenter->Position({ 0, 0, 64, 64 }, 0, 0, 1);
        VERIFY_IS_TRUE(presenter->IsDeviceValid());

        const MonitorSource changedSources[]{
            { { -64, 0, 0, 64 }, L"" },
            { { 0, -32, 128, 64 }, L"" }
        };
        const auto changedMonitors = Render(changedSources, DWPOS_FILL, RGB(56, 34, 12));
        presenter.reset();
        presenter = std::make_unique<Presenter>(parent.get());
        presenter->SetMonitors(changedMonitors);
        presenter->Position({ 0, 0, 64, 64 }, -32, 0, 1);
        VERIFY_IS_TRUE(presenter->IsDeviceValid());
    }

    void DesktopWallpaperTests::WallpaperUpdatesReusePresenter()
    {
        wil::unique_hwnd parent{ CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
        VERIFY_IS_NOT_NULL(parent.get());
        const MonitorSource sources[]{ { { 0, 0, 64, 64 }, L"" } };
        const auto original = Render(sources, DWPOS_FILL, RGB(12, 34, 56));
        Presenter presenter{ parent.get() };
        presenter.SetMonitors(original);
        presenter.Position({ 0, 0, 64, 64 }, 8, 16, 0.75f);

        const auto graphicsDevice = presenter._graphicsDevice;
        const auto device = presenter._device;
        const auto target = presenter._target;
        const auto root = presenter._root;
        Effects effects;
        effects.blur = 2;
        effects.saturation = 0.5f;
        effects.tintOpacity = 0.25f;
        for (const auto opacity : { 0.5f, 1.0f, 0.75f })
        {
            const auto replacement = Render(sources, DWPOS_FILL, RGB(56, 34, 12), effects);
            const auto previousVisual = presenter._monitors[0].visual;
            presenter.SetMonitors(replacement);
            VERIFY_IS_TRUE(presenter._visible);
            VERIFY_IS_FALSE(presenter._positioned);
            VERIFY_IS_TRUE(previousVisual.get() != presenter._monitors[0].visual.get());
            presenter.Position({ 0, 0, 64, 64 }, 8, 16, opacity);
            VERIFY_IS_TRUE(presenter._positioned);
            VERIFY_IS_TRUE(presenter._visible);
            VERIFY_IS_TRUE(graphicsDevice.get() == presenter._graphicsDevice.get());
            VERIFY_IS_TRUE(device.get() == presenter._device.get());
            VERIFY_IS_TRUE(target.get() == presenter._target.get());
            VERIFY_IS_TRUE(root.get() == presenter._root.get());
            VERIFY_ARE_EQUAL(opacity, presenter._currentOpacity);
        }

        const auto previousVisual = presenter._monitors[0].visual;
        auto invalidReplacement = original;
        winrt::Windows::Graphics::Imaging::SoftwareBitmap closedBitmap{
            winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, 64, 64 };
        closedBitmap.Close();
        invalidReplacement.push_back({ { 64, 0, 128, 64 }, closedBitmap });
        VERIFY_THROWS(presenter.SetMonitors(invalidReplacement), wil::ResultException);
        VERIFY_IS_TRUE(previousVisual.get() == presenter._monitors[0].visual.get());
        VERIFY_IS_TRUE(presenter._visible);
        VERIFY_IS_TRUE(presenter._positioned);
        presenter.Position({ 0, 0, 64, 64 }, 9, 16, 0.75f);
        VERIFY_IS_TRUE(presenter.IsDeviceValid());
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
            wil::unique_hwnd content{ CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 64, 64, parent.get(), nullptr, GetModuleHandleW(nullptr), nullptr) };
            VERIFY_IS_NOT_NULL(content.get());
            Presenter presenter{ parent.get() };
            VERIFY_ARE_EQUAL(content.get(), GetWindow(parent.get(), GW_CHILD));
            presenter.SetMonitors(monitors);
            for (const auto x : { -32, -1, 0, 1, 32 })
            {
                presenter.Position({ 8, 16, 72, 80 }, x, 0, 1);
                VERIFY_ARE_EQUAL(content.get(), GetWindow(parent.get(), GW_CHILD));
                VERIFY_IS_NULL(GetWindow(content.get(), GW_HWNDNEXT));
            }
            presenter.Position({ 0, 0, 64, 64 }, 0, 0, 0.5f);
            presenter.Position({}, 0, 0, 0.5f);
            presenter.Position({ 0, 0, 32, 32 }, 0, 0, 1);
            presenter.SetMonitors(monitors);
            presenter.Position({ 0, 0, 32, 32 }, 0, 0, 1);
        }
        VERIFY_IS_NULL(GetWindow(parent.get(), GW_CHILD));
        {
            Presenter presenter{ parent.get() };
            presenter.SetMonitors(monitors);
            presenter.Position({ 0, 0, 64, 64 }, 0, 0, 1);
        }
    }
}
