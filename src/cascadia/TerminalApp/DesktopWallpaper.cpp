// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "DesktopWallpaper.h"
#include "DesktopWallpaperGeometry.h"

#include <wincodec.h>
#include <windows.graphics.imaging.interop.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <winrt/Windows.Storage.Streams.h>

namespace TerminalApp::DesktopWallpaper
{
    namespace
    {
        winrt::com_ptr<ID2D1DeviceContext> CreateGraphicsContext()
        {
            winrt::com_ptr<ID3D11Device> d3d;
            const auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr);
            if (hr == DXGI_ERROR_UNSUPPORTED)
            {
                LOG_HR(hr);
                THROW_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr));
            }
            else
            {
                THROW_IF_FAILED(hr);
            }
            winrt::com_ptr<ID2D1Factory1> factory;
            THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
            winrt::com_ptr<ID2D1Device> device;
            THROW_IF_FAILED(factory->CreateDevice(d3d.as<IDXGIDevice>().get(), device.put()));
            winrt::com_ptr<ID2D1DeviceContext> context;
            THROW_IF_FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.put()));
            return context;
        }
    }

    Presenter::Presenter(const HWND parent)
    {
        static const auto className = [] {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = DefWindowProcW;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"TerminalDesktopWallpaper";
            THROW_LAST_ERROR_IF(!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS);
            return windowClass.lpszClassName;
        }();
        _window.reset(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, className, L"", WS_CHILD | WS_DISABLED, 0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr));
        THROW_LAST_ERROR_IF(!_window);
        const auto context = CreateGraphicsContext();
        winrt::com_ptr<ID2D1Device> d2d;
        context->GetDevice(d2d.put());
        THROW_IF_FAILED(DCompositionCreateDevice2(d2d.get(), __uuidof(IDCompositionDesktopDevice), _device.put_void()));
        THROW_IF_FAILED(_device->CreateTargetForHwnd(_window.get(), false, _target.put()));
        THROW_IF_FAILED(_device->CreateVisual(_root.put()));
        THROW_IF_FAILED(_device->CreateEffectGroup(_opacity.put()));
        THROW_IF_FAILED(_root->SetEffect(_opacity.get()));
        THROW_IF_FAILED(_target->SetRoot(_root.get()));
    }

    void Presenter::SetMonitors(const std::span<const Monitor> monitors)
    {
        std::vector<MonitorVisual> visuals;
        for (const auto& monitor : monitors)
        {
            const auto width = monitor.bitmap.PixelWidth();
            const auto height = monitor.bitmap.PixelHeight();
            THROW_HR_IF(E_INVALIDARG, width <= 0 || height <= 0);
            const auto bytes = static_cast<uint64_t>(width) * height * 4;
            THROW_HR_IF(E_BOUNDS, bytes > UINT32_MAX);
            winrt::Windows::Storage::Streams::Buffer pixels{ static_cast<uint32_t>(bytes) };
            monitor.bitmap.CopyToBuffer(pixels);
            winrt::com_ptr<IDCompositionSurface> surface;
            THROW_IF_FAILED(_device->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
            {
                winrt::com_ptr<ID2D1DeviceContext> context;
                POINT offset{};
                THROW_IF_FAILED(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset));
                auto endDraw = wil::scope_exit([&] { LOG_IF_FAILED(surface->EndDraw()); });
                context->SetDpi(96, 96);
                context->SetTransform(D2D1::Matrix3x2F::Identity());
                winrt::com_ptr<ID2D1Bitmap1> bitmap;
                THROW_IF_FAILED(context->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), bitmap.put()));
                const auto rect = D2D1::RectF(static_cast<float>(offset.x), static_cast<float>(offset.y), static_cast<float>(offset.x + width), static_cast<float>(offset.y + height));
                context->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
                context->Clear(D2D1::ColorF(0, 0));
                context->DrawBitmap(bitmap.get(), rect);
                context->PopAxisAlignedClip();
                const auto hr = surface->EndDraw();
                endDraw.release();
                THROW_IF_FAILED(hr);
            }
            winrt::com_ptr<IDCompositionVisual2> visual;
            THROW_IF_FAILED(_device->CreateVisual(visual.put()));
            THROW_IF_FAILED(visual->SetContent(surface.get()));
            visuals.push_back({ monitor.bounds, std::move(visual) });
        }
        THROW_IF_FAILED(_root->RemoveAllVisuals());
        for (const auto& monitor : visuals)
        {
            THROW_IF_FAILED(_root->AddVisual(monitor.visual.get(), true, nullptr));
        }
        _monitors = std::move(visuals);
        _positioned = false;
    }

    void Presenter::Position(const RECT& bounds, const double originX, const double originY, const float opacity)
    {
        const auto width = bounds.right - bounds.left;
        const auto height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0)
        {
            ShowWindow(_window.get(), SW_HIDE);
            _visible = false;
            return;
        }
        bool changed = !_positioned;
        if (!_visible || !EqualRect(&_bounds, &bounds))
        {
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(_window.get(), HWND_BOTTOM, bounds.left, bounds.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW));
            THROW_IF_FAILED(_root->SetClip(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height))));
            _bounds = bounds;
            _visible = true;
            changed = true;
        }
        if (!_positioned || _originX != originX || _originY != originY)
        {
            for (const auto& monitor : _monitors)
            {
                const auto rect = MonitorRect(monitor.bounds, originX, originY, 1);
                THROW_IF_FAILED(monitor.visual->SetOffsetX(rect.left));
                THROW_IF_FAILED(monitor.visual->SetOffsetY(rect.top));
            }
            _originX = originX;
            _originY = originY;
            changed = true;
        }
        if (_currentOpacity != opacity)
        {
            THROW_IF_FAILED(_opacity->SetOpacity(opacity));
            _currentOpacity = opacity;
            changed = true;
        }
        if (changed)
        {
            // Submit directly; XAML's dispatcher does not drive native dragging.
            THROW_IF_FAILED(_device->Commit());
            _positioned = true;
        }
    }

    namespace
    {
        void ApplyEffects(ID2D1DeviceContext* context, IWICBitmap* bitmap, const Effects& effects)
        {
            UINT width, height;
            THROW_IF_FAILED(bitmap->GetSize(&width, &height));
            winrt::com_ptr<ID2D1Bitmap1> input;
            THROW_IF_FAILED(context->CreateBitmapFromWicBitmap(bitmap, nullptr, input.put()));
            winrt::com_ptr<ID2D1Image> output = input;
            if (effects.saturation != 1)
            {
                winrt::com_ptr<ID2D1Effect> saturation;
                THROW_IF_FAILED(context->CreateEffect(CLSID_D2D1Saturation, saturation.put()));
                saturation->SetInput(0, output.get());
                THROW_IF_FAILED(saturation->SetValue(D2D1_SATURATION_PROP_SATURATION, effects.saturation));
                output = nullptr;
                saturation->GetOutput(output.put());
            }
            if (effects.blur > 0)
            {
                winrt::com_ptr<ID2D1Effect> blur;
                THROW_IF_FAILED(context->CreateEffect(CLSID_D2D1GaussianBlur, blur.put()));
                blur->SetInput(0, output.get());
                THROW_IF_FAILED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, effects.blur));
                THROW_IF_FAILED(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD));
                output = nullptr;
                blur->GetOutput(output.put());
            }
            if (effects.tintOpacity > 0)
            {
                const auto opacity = effects.tintOpacity;
                const auto retain = 1 - opacity;
                const auto matrix = D2D1::Matrix5x4F(
                    retain, 0, 0, 0, 0, retain, 0, 0, 0, 0, retain, 0, 0, 0, 0, 1, opacity * GetRValue(effects.tint) / 255.0f, opacity * GetGValue(effects.tint) / 255.0f, opacity * GetBValue(effects.tint) / 255.0f, 0);
                winrt::com_ptr<ID2D1Effect> tint;
                THROW_IF_FAILED(context->CreateEffect(CLSID_D2D1ColorMatrix, tint.put()));
                tint->SetInput(0, output.get());
                THROW_IF_FAILED(tint->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix));
                output = nullptr;
                tint->GetOutput(output.put());
            }

            const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
            winrt::com_ptr<ID2D1Bitmap1> target;
            THROW_IF_FAILED(context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), target.put()));
            context->SetTarget(target.get());
            context->BeginDraw();
            context->Clear(D2D1::ColorF(0, 0));
            context->DrawImage(output.get());
            THROW_IF_FAILED(context->EndDraw());
            context->SetTarget(nullptr);

            winrt::com_ptr<ID2D1Bitmap1> readback;
            THROW_IF_FAILED(context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format), readback.put()));
            THROW_IF_FAILED(readback->CopyFromBitmap(nullptr, target.get(), nullptr));
            D2D1_MAPPED_RECT mapped{};
            THROW_IF_FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
            const auto unmap = wil::scope_exit([&] { LOG_IF_FAILED(readback->Unmap()); });
            winrt::com_ptr<IWICBitmapLock> lock;
            THROW_IF_FAILED(bitmap->Lock(nullptr, WICBitmapLockWrite, lock.put()));
            UINT stride, length;
            BYTE* pixels;
            THROW_IF_FAILED(lock->GetStride(&stride));
            THROW_IF_FAILED(lock->GetDataPointer(&length, &pixels));
            THROW_HR_IF(E_UNEXPECTED, stride < width * 4 || length < static_cast<uint64_t>(height - 1) * stride + width * 4);
            for (UINT y = 0; y < height; ++y)
            {
                memcpy(pixels + static_cast<size_t>(y) * stride, mapped.bits + static_cast<size_t>(y) * mapped.pitch, static_cast<size_t>(width) * 4);
            }
        }
    }

    std::vector<Monitor> Load(const Effects& effects)
    {
        const auto apartment = wil::CoInitializeEx(COINIT_MULTITHREADED);
        const auto wallpaper = winrt::create_instance<IDesktopWallpaper>(CLSID_DesktopWallpaper, CLSCTX_ALL);
        DESKTOP_WALLPAPER_POSITION position;
        COLORREF background;
        UINT count;
        THROW_IF_FAILED(wallpaper->GetPosition(&position));
        THROW_IF_FAILED(wallpaper->GetBackgroundColor(&background));
        THROW_IF_FAILED(wallpaper->GetMonitorDevicePathCount(&count));

        std::vector<MonitorSource> sources;
        for (UINT i = 0; i < count; ++i)
        {
            wil::unique_cotaskmem_string device;
            THROW_IF_FAILED(wallpaper->GetMonitorDevicePathAt(i, device.put()));
            RECT bounds;
            const auto hr = wallpaper->GetMonitorRECT(device.get(), &bounds);
            if (hr == S_FALSE)
            {
                continue; // The shell also enumerates disconnected monitors.
            }
            THROW_IF_FAILED(hr);
            wil::unique_cotaskmem_string path;
            THROW_IF_FAILED(wallpaper->GetWallpaper(device.get(), path.put()));
            sources.push_back({ bounds, path ? path.get() : L"" });
        }
        return Render(sources, position, background, effects);
    }

    std::vector<Monitor> Render(const std::span<const MonitorSource> sources, const DESKTOP_WALLPAPER_POSITION position, const COLORREF background, const Effects& effects)
    {
        THROW_HR_IF(E_INVALIDARG, !std::isfinite(effects.blur) || effects.blur < 0 || effects.blur > 250 || !std::isfinite(effects.saturation) || effects.saturation < 0 || effects.saturation > 1 || !std::isfinite(effects.tintOpacity) || effects.tintOpacity < 0 || effects.tintOpacity > 1);
        const auto effectsContext = effects.blur > 0 || effects.saturation != 1 || effects.tintOpacity > 0 ? CreateGraphicsContext() : nullptr;
        RECT desktop{};
        for (size_t i = 0; i < sources.size(); ++i)
        {
            if (i == 0)
            {
                desktop = sources[i].bounds;
            }
            else
            {
                UnionRect(&desktop, &desktop, &sources[i].bounds);
            }
        }
        const auto wic = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
        winrt::com_ptr<ID2D1Factory> d2d;
        THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.put()));
        const auto bitmapFactory = winrt::create_instance<ISoftwareBitmapNativeFactory>(CLSID_SoftwareBitmapNativeFactory);
        std::map<std::wstring, winrt::com_ptr<IWICFormatConverter>> decodedImages;
        std::vector<Monitor> monitors;
        for (const auto& source : sources)
        {
            const auto width = source.bounds.right - source.bounds.left;
            const auto height = source.bounds.bottom - source.bounds.top;
            THROW_HR_IF(E_UNEXPECTED, width <= 0 || height <= 0);
            winrt::com_ptr<IWICBitmap> bitmap;
            THROW_IF_FAILED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.put()));
            winrt::com_ptr<ID2D1RenderTarget> target;
            const auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96.0f,
                96.0f);
            THROW_IF_FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.get(), properties, target.put()));

            winrt::com_ptr<ID2D1Bitmap> image;
            if (!source.path.empty())
            {
                auto& decoded = decodedImages[source.path];
                if (!decoded)
                {
                    winrt::com_ptr<IWICBitmapDecoder> decoder;
                    THROW_IF_FAILED(wic->CreateDecoderFromFilename(source.path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()));
                    winrt::com_ptr<IWICBitmapFrameDecode> frame;
                    THROW_IF_FAILED(decoder->GetFrame(0, frame.put()));
                    THROW_IF_FAILED(wic->CreateFormatConverter(decoded.put()));
                    THROW_IF_FAILED(decoded->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
                }
                const auto imageProperties = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                    96.0f,
                    96.0f);
                THROW_IF_FAILED(target->CreateBitmapFromWicBitmap(decoded.get(), imageProperties, image.put()));
            }

            target->BeginDraw();
            target->Clear(D2D1::ColorF(GetRValue(background) / 255.0f, GetGValue(background) / 255.0f, GetBValue(background) / 255.0f));
            if (image)
            {
                // Use physical desktop pixels, not work areas or monitor DPI.
                target->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(source.bounds.left), -static_cast<float>(source.bounds.top)));
                if (position == DWPOS_TILE)
                {
                    winrt::com_ptr<ID2D1BitmapBrush> brush;
                    THROW_IF_FAILED(target->CreateBitmapBrush(image.get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), brush.put()));
                    target->FillRectangle(D2D1::RectF(
                                              static_cast<float>(source.bounds.left), static_cast<float>(source.bounds.top), static_cast<float>(source.bounds.right), static_cast<float>(source.bounds.bottom)),
                                          brush.get());
                }
                else
                {
                    const auto size = image->GetSize();
                    const auto rect = ImageRect(size.width, size.height, position == DWPOS_SPAN ? desktop : source.bounds, position);
                    target->DrawBitmap(image.get(), rect);
                }
            }
            THROW_IF_FAILED(target->EndDraw());
            target = nullptr;
            if (effectsContext)
            {
                ApplyEffects(effectsContext.get(), bitmap.get(), effects);
            }
            auto softwareBitmap = winrt::capture<winrt::Windows::Graphics::Imaging::SoftwareBitmap>(
                bitmapFactory, &ISoftwareBitmapNativeFactory::CreateFromWICBitmap, bitmap.get(), false);
            monitors.push_back({ source.bounds, std::move(softwareBitmap) });
        }
        return monitors;
    }
}
