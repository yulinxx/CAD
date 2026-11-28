#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include "UiShell/Export.h"
#include "UiShell/Contract/DialogRegistry.h"
#include "UiShell/Contract/IApiObject.h"
#include "UiShell/Contract/WidgetFactory.h"

namespace AA
{
    class ICommandCatalog;
    class ICommandInvoker;
    class ICommandState;
    class ICommandProfileRegistrar;
    class IRuntimeBinding;

    struct WidgetFactoryBinding
    {
        QString strFactoryId;
        IWidgetFactory* factory = nullptr;
    };

    struct DlgFactoryBinding
    {
        QString strDialogId;
        IDialogFactory* factory = nullptr;
    };

    // 命令触发对话框后，是否继续执行
    enum class DialogContinuePolicy
    {
        // 默认：返回 Accepted 才继续；Cancelled / Rejected 终止
        OnAccepted,
        // 打开对话框后不再执行命令
        Never
    };

    // 命令→对话框绑定
    struct DialogCmdBinding
    {
        QString strCmd;
        QString strDlgId;
        QJsonObject params;
        DialogContinuePolicy continuePolicy = DialogContinuePolicy::OnAccepted;
    };

    // 客户模块
    struct CustomerModule
    {
        QString strId;
        QString strDisplayName;

        // 按当前语言重新计算 displayName
        QString(*displayNameFn)() = nullptr;

        QString strProfilePath;

        // 皮肤样式
        QString strStyleSeries;

        ICenterViewProvider* centerView = nullptr;
        ITitleBarFactory* titleBar = nullptr;
        const ICommandCatalog* cmdCatalog = nullptr;
        ICommandInvoker* cmdInvoker = nullptr;

        // 可选：由实际加载的 profile 补充登记菜单命令（外置 ui.json 场景）。
        ICommandProfileRegistrar* cmdProfileRegistrar = nullptr;

        // 命令状态
        const ICommandState* cmdState = nullptr;

        // 可选：宿主创建外壳运行时后回绑；客户不必再查进程级全局。
        IRuntimeBinding* runtimeBinding = nullptr;

        QList<WidgetFactoryBinding> listWidgetFactoriesBind;
        QList<DlgFactoryBinding> listDlgFactoriesBind;

        // 命令→对话框绑定表
        QList<DialogCmdBinding> listDialogBind;

        // 持有者
        IApiObject* owner = nullptr;
    };

    UI_CUSTOMER_API CustomerModule createCustomerModule();
}  // namespace AA
