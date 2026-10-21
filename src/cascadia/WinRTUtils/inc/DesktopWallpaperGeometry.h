// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <algorithm>
#include <d2d1.h>
#include <shobjidl_core.h>

namespace Microsoft::Terminal::UI::DesktopWallpaper
{
    inline D2D1_RECT_F ImageRect(const float imageWidth, const float imageHeight, const RECT& bounds, const DESKTOP_WALLPAPER_POSITION position)
    {
        const auto width = static_cast<float>(bounds.right - bounds.left);
        const auto height = static_cast<float>(bounds.bottom - bounds.top);
        auto scaledWidth = imageWidth;
        auto scaledHeight = imageHeight;
        if (position == DWPOS_STRETCH)
        {
            scaledWidth = width;
            scaledHeight = height;
        }
        else if (position == DWPOS_FIT || position == DWPOS_FILL || position == DWPOS_SPAN)
        {
            const auto scale = position == DWPOS_FIT ?
                                   std::min(width / imageWidth, height / imageHeight) :
                                   std::max(width / imageWidth, height / imageHeight);
            scaledWidth *= scale;
            scaledHeight *= scale;
        }
        const auto x = static_cast<float>(bounds.left) + (width - scaledWidth) / 2.0f;
        const auto y = static_cast<float>(bounds.top) + (height - scaledHeight) / 2.0f;
        return { x, y, x + scaledWidth, y + scaledHeight };
    }

    inline D2D1_RECT_F MonitorRect(const RECT& monitor, const double originX, const double originY, const double scale)
    {
        return {
            static_cast<float>((monitor.left - originX) / scale),
            static_cast<float>((monitor.top - originY) / scale),
            static_cast<float>((monitor.right - originX) / scale),
            static_cast<float>((monitor.bottom - originY) / scale)
        };
    }
}
