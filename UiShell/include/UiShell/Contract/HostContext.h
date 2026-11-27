#pragma once

#include <QString>

#include "UiShell/Contract/ITextProvider.h"

namespace RD
{
    namespace ThemeId
    {
        constexpr const char* Light = "light";
        constexpr const char* Dark = "dark";
    }  // namespace ThemeId

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
