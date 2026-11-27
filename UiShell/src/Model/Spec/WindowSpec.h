#pragma once

#include <optional>

namespace RD
{
    // 顶层窗口的最小尺寸与边框模式。
    // 标题栏形态由平台编译期固定：Windows 自绘，macOS/Linux 原生；
    // 菜单栏形态可由 profile 的 nativeMenuBar 配置（macOS 屏幕顶部 vs 窗口内）。
    struct WindowSpec
    {
        int minWidth = 800;
        int minHeight = 600;

        // true：菜单栏交给系统原生菜单栏（macOS 上位于屏幕顶部）；
        // false：菜单栏作为窗口内控件绘制，菜单项图标可见、可用 QSS 皮肤；
        // std::nullopt：未配置，默认 false（窗口内菜单栏）。
        // 注意：macOS 的原生菜单不画 QAction 图标（Qt 的 Cocoa 集成不设置 NSMenuItem.image），
        // 原生模式下菜单项只有文字。
        std::optional<bool> nativeMenuBar;
    };
}  // namespace RD
