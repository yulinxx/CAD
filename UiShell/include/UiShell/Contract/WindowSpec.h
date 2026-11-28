#pragma once

namespace AA
{
    // 顶层窗口的最小尺寸与边框模式。
    // 标题栏形态由平台编译期固定（Windows 自绘、macOS/Linux 原生），不通过配置接口暴露。
    struct WindowSpec
    {
        int minWidth = 800;
        int minHeight = 600;
    };
}  // namespace AA
