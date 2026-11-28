#pragma once

#include <QPalette>
#include <QString>

namespace AA
{
    // 主题控制器
    class ThemeController
    {
    public:
        // 浅色沿用系统标准调色板；深色给出固定对比度。
        static QPalette paletteFor(const QString& themeId);

        // 从 qrc 读取 <themeId>.qss 并连同调色板一起应用到 QApplication
        // profileDir 非空时额外读取 <profileDir>/styles/<themeId>.qss，
        // 让外置 ui.json 无需重编即可换皮肤。
        static bool apply(const QString& themeId,
                          const QString& customPrefix = QString(),
                          const QString& profileDir = QString());
    };
}  // namespace AA
