#pragma once

#include <QJsonObject>
#include <QString>

#include "UiShell/Contract/CustomerModule.h"
#include "UiShell/Contract/DialogRegistry.h"
#include "UiShell/Contract/FeatureFlag.h"
#include "UiShell/Contract/Command.h"
#include "UiShell/Contract/ITextProvider.h"
#include "UiShell/Contract/WidgetFactory.h"

namespace AA
{
    class ITranslationService;

    // Shell事件回调接口
    class IShellListener
    {
    public:
        // 语言切换完成
        virtual void onLanguageChanged(const QString& languageId) = 0;

    protected:
        virtual ~IShellListener() = default;
    };

    struct ShellServices
    {
        ITitleBarFactory* titleBarFactory = nullptr;
        ICenterViewProvider* centerView = nullptr;

        // 功能开关来源
        IFeatureFlagStore* featureFlags = nullptr;

        const ICommandCatalog* cmdCatalog = nullptr;
        ICommandInvoker* cmdInvoker = nullptr;

        // 可选：加载 profile 时补充登记菜单命令（外置 ui.json 场景）。
        ICommandProfileRegistrar* cmdProfileRegistrar = nullptr;

        const ICommandState* cmdState = nullptr;

        const ITextProvider* text = nullptr;

        IWidgetFactoryRegistry* widgetReg = nullptr;

        // 对话框注册
        IDialogFactoryRegistry* dlgReg = nullptr;

        // 命令→对话框绑定表
        QList<DialogCmdBinding> dialogBindings;

        // 翻译服务
        ITranslationService* transService = nullptr;

        // Shell事件回调
        IShellListener* shellListener = nullptr;

        // 当前皮肤 id
        QString strThemeId;

        // 客户样式表（qrc 前缀）
        QString strCustomStyle;

        // M5：外置 profile 的 styles/ 目录（可为空）
        QString strStyleDir;

        // M5：profile 的 brand 段转成的对话框参数；显式 binding 优先。
        QJsonObject brandParams;

        // 皮肤样式
        QString strStyleSeries;
    };
}  // namespace AA
