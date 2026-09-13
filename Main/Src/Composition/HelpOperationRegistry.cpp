#include "HelpOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

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

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_About, [parentWidget] {
        HelpDialogService::showAboutDialog(parentWidget);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Settings, [parentWidget] {
        // Help > Settings / 设置对话框入口
        SY_DEBUGF("[HelpOperationRegistry] Help_Settings triggered, parentWidget=%p", static_cast<void*>(parentWidget));
        auto* window = qobject_cast<WorkbenchWindow*>(parentWidget);
        SY_DEBUGF("[HelpOperationRegistry] window=%p", static_cast<void*>(window));
        UiWorkbench* activeWb = window ? window->currentWorkbench() : nullptr;
        SY_DEBUGF("[HelpOperationRegistry] activeWb=%p", static_cast<void*>(activeWb));
        if (activeWb)
        {
            SY_DEBUGF("[HelpOperationRegistry] calling showSettingsDialog");
            activeWb->showSettingsDialog(parentWidget);
        }
        else
        {
            SY_WARNF("[HelpOperationRegistry] Cannot show settings: no active workbench");
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Docs, [parentWidget] {
        HelpDialogService::showDocumentationDialog(parentWidget);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Shortcut, [parentWidget] {
        HelpDialogService::showShortcutsDialog(parentWidget);
    }));
}
