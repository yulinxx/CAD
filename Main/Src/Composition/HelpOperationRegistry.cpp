/**
 * @file HelpOperationRegistry.cpp
 * @brief 帮助操作注册实现
 *
 * 注册帮助相关的操作命令。
 */
#include "HelpOperationRegistry.h"
#include "OpRegistryTypes.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

#include "UI/Services/HelpDialogService.h"

#include <QWidget>

HelpOperationRegistry::HelpOperationRegistry(OperationBus* bus, QWidget* parentWidget)
    : m_bus(bus)
    , m_parentWidget(parentWidget)
{
}

void HelpOperationRegistry::registerAll()
{
    if (!m_bus)
        return;

    auto& reg = m_bus->registry();
    QWidget* parentWidget = m_parentWidget;

    Ops::declare(reg, OperationId::Help_About, [parentWidget] {
        HelpDialogService::showAboutDialog(parentWidget);
    });

    Ops::declare(reg, OperationId::Help_Settings, [parentWidget] {
        // M6：旧工作台设置对话框随 Workbench 移入测试专用目录；新外壳的设置入口
        // 由 UiShell 自身的对话框绑定提供，这里只保留 OperationBus 上的稳定命令 id。
        Q_UNUSED(parentWidget);
        APR_WARNF("[HelpOperationRegistry] Settings dialog is provided by the shell host (M6)");
    });

    Ops::declare(reg, OperationId::Help_Docs, [parentWidget] {
        HelpDialogService::showDocumentationDialog(parentWidget);
    });
}