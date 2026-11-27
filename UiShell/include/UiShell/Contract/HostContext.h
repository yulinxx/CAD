#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include "UiShell/Contract/ITextProvider.h"

namespace RD
{
    namespace ThemeId
    {
        constexpr const char* Light = "light";
        constexpr const char* Dark = "dark";
        constexpr const char* Blue = "blue";
        constexpr const char* Slate = "slate";
        constexpr const char* HighContrast = "high_contrast";
        constexpr const char* System = "system";
    }  // namespace ThemeId

    // 六套皮肤的稳定 id 与菜单显示文本源；core 之外的菜单/运行时共用。
    namespace ThemeCatalog
    {
        struct Entry
        {
            QString strId;
            const char* label;  // QCoreApplication::translate("ThemeMenu", label)
        };

        inline const QList<Entry>& entries()
        {
            // QT_TRANSLATE_NOOP 让 lupdate 能提取这六个菜单文案。
            static const QList<Entry> list{
                { QString::fromLatin1(ThemeId::Light), QT_TRANSLATE_NOOP("ThemeMenu", "Light") },
                { QString::fromLatin1(ThemeId::Dark), QT_TRANSLATE_NOOP("ThemeMenu", "Dark") },
                { QString::fromLatin1(ThemeId::Blue), QT_TRANSLATE_NOOP("ThemeMenu", "Blue") },
                { QString::fromLatin1(ThemeId::Slate), QT_TRANSLATE_NOOP("ThemeMenu", "Slate") },
                { QString::fromLatin1(ThemeId::HighContrast), QT_TRANSLATE_NOOP("ThemeMenu", "High Contrast") },
                { QString::fromLatin1(ThemeId::System), QT_TRANSLATE_NOOP("ThemeMenu", "System") },
            };
            return list;
        }

        // 派生皮肤没有独立的全量 QSS：先加载基础皮肤，再叠加自己的差异规则。
        inline QString baseId(const QString& themeId)
        {
            if (themeId == QLatin1String(ThemeId::Blue) || themeId == QLatin1String(ThemeId::Slate)
                || themeId == QLatin1String(ThemeId::System))
            {
                return QString::fromLatin1(ThemeId::Light);
            }
            if (themeId == QLatin1String(ThemeId::HighContrast))
                return QString::fromLatin1(ThemeId::Dark);
            return themeId;
        }
    }  // namespace ThemeCatalog

    // 宿主上下文：当前皮肤、语言、功能开关 revision 与文本提供者
    struct HostContext
    {
        QString strThemeId = QStringLiteral("light");
        QString strLanguageId;
        quint64 nFeatureRevision = 0;
        const ITextProvider* text = nullptr;
    };

    // 需要感知皮肤/语言的控件实现它，Shell 在控件创建后及切换皮肤/语言时调用。
    class IHostContextAware
    {
    public:
        virtual ~IHostContextAware() = default;

    public:
        virtual void setHostContext(const HostContext& context) = 0;
    };

    // 需要重算或缓存文案的控件实现它，语言切换后由 Shell 统一调用。
    class IRetranslatable
    {
    public:
        virtual ~IRetranslatable() = default;

    public:
        virtual void retranslate() = 0;
    };

    // 能显示提示消息的控件（通常是状态栏面板）实现它，面板或命令调用 IShellHost::showMessage() 时由 Shell 推送。
    class IStatusMessageView
    {
    public:
        virtual ~IStatusMessageView() = default;

    public:
        virtual void showStatusMessage(const QString& text) = 0;
    };
}  // namespace RD
