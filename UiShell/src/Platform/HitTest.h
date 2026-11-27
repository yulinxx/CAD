// 边缘检测纯逻辑 — 不依赖 Win32 / Qt 类型。
// 放在 src/Platform 内部：生产代码在 WindowsWindowFrame.cpp 里
// 从 Win32 消息组装参数后调这里，再把平台无关的 HitTestZone 映射成 HT* 结果。
// 单元测试也直接 include 本头验证 edgeBandWidth / hitTest 的纯逻辑语义。

#pragma once

#include <algorithm>

namespace RD
{
    // 客户区命中的语义结果。平台层负责映射到自己的常量。
    enum class HitTestZone
    {
        Client,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    // 计算边缘带宽度：96 DPI 下 5px，高 DPI 等比放大，下限 4px
    // 避免在 150%/200% 缩放下用户无法精准点到边缘。
    inline int edgeBandWidth(int dpi)
    {
        if (dpi <= 0)
            dpi = 96;
        // 与 Win32 MulDiv 一致的 round-to-nearest；不引入 Win32 依赖。
        const int raw = (5 * dpi + 48) / 96;
        return (std::max)(4, raw);
    }

    // 纯边缘检测：给定客户区尺寸、客户坐标、最大化状态、边缘带宽。
    // 最大化时统一返回 Client，防止最大化窗口被误拉成缩放。
    inline HitTestZone hitTestZone(int x, int y, int width, int height, bool maximized, int band)
    {
        if (maximized)
            return HitTestZone::Client;

        const bool left = x < band;
        const bool right = x >= width - band;
        const bool top = y < band;
        const bool bottom = y >= height - band;

        if (top && left)
            return HitTestZone::TopLeft;
        if (top && right)
            return HitTestZone::TopRight;
        if (bottom && left)
            return HitTestZone::BottomLeft;
        if (bottom && right)
            return HitTestZone::BottomRight;
        if (left)
            return HitTestZone::Left;
        if (right)
            return HitTestZone::Right;
        if (top)
            return HitTestZone::Top;
        if (bottom)
            return HitTestZone::Bottom;

        return HitTestZone::Client;
    }
}  // namespace RD
