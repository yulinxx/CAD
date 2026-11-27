#pragma once

#include <QString>
#include <QStringList>

namespace RD
{
    // 标题栏描述信息
    struct TitleBarSpec
    {
        bool bEnabled = true;     // 是否启用
        QString strTitleKey;      // 文本 id
        QString strTitle;         // 文字
        QString strIcon;          // 资源路径或字形
        QStringList listButtons;  // 按钮
        int nHeight = 36;         // 像素
        bool bShowTitle = true;
        QString strBkgColor;
        QString strTextColor;

        TitleBarSpec()
            : listButtons{ QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close") }
        {
        }
    };
}  // namespace RD
