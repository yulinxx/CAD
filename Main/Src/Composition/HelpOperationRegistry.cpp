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
        auto* window = qobject_cast<WorkbenchWindow*>(parentWidget);
        UiWorkbench* activeWb = window ? window->currentWorkbench() : nullptr;
        if (activeWb)
            activeWb->showSettingsDialog(parentWidget);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Docs, [parentWidget] {
        HelpDialogService::showDocumentationDialog(parentWidget);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Shortcut, [parentWidget] {
        HelpDialogService::showShortcutsDialog(parentWidget);
    }));
}
