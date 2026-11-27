#pragma once

#include <QPalette>
#include <QString>

namespace RD
{
    // 主题控制器
    class ThemeController
    {
    public:
        // 浅色沿用系统标准调色板；深色给出固定对比度。
        static QPalette paletteFor(const QString& themeId);

        // 从 qrc 读取 <themeId>.qss 并连同调色板一起应用到 QApplication
        static bool apply(const QString& themeId, const QString& customPrefix = QString());
    };
}  // namespace RD
