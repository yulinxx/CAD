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
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Workbench/UiWorkbench.h"

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
        auto* window = qobject_cast<WorkbenchWindow*>(parentWidget);
        if (UiWorkbench* activeWb = window ? window->currentWorkbench() : nullptr)
        {
            activeWb->showSettingsDialog(parentWidget);
        }
        else
        {
            APR_WARNF("[HelpOperationRegistry] Cannot show settings: no active workbench");
        }
    });

    Ops::declare(reg, OperationId::Help_Docs, [parentWidget] {
        HelpDialogService::showDocumentationDialog(parentWidget);
    });
}