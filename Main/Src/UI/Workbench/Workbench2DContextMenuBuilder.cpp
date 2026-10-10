#include "Workbench2DContextMenuBuilder.h"

#include "UI2D/Operation/CommandActionHub.h"
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiContextMenuService.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Workbench/WorkbenchMenuManager.h"
#include "UI/ClientConfig/IUiCommandDispatcher.h"
#include "Engine2D/Interaction/LayerManager.h"

Workbench2DContextMenuBuilder::Workbench2DContextMenuBuilder(
    CommandActionHub* hub, LayerManager* layerManager, WorkbenchWindow* window)
    : m_hub(hub)
    , m_layerManager(layerManager)
    , m_window(window)
{
}

void Workbench2DContextMenuBuilder::onViewportContextMenu(QContextMenuEvent* event)
{
    if (!event || !m_hub || !m_layerManager)
    {
        return;
    }

    const CommandUiSnapshot snapshot = m_hub->captureSnapshot(m_hub->mainWindow());

    if (QMenu* configured = buildConfiguredContextMenu(QStringLiteral("canvas.2d"), snapshot.hasSelection))
    {
        CommandActionHub::applySnapshotToMenu(configured, snapshot);
        configured->exec(event->globalPos());
        delete configured;
        return;
    }

    QMenu menu;
    m_hub->populateContextMenu(&menu, snapshot, m_layerManager);
    if (menu.isEmpty())
    {
        return;
    }
    menu.exec(event->globalPos());
}

QMenu* Workbench2DContextMenuBuilder::buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection)
{
    const UiConfigData* config = UiConfigurationManager::shared().configData();
    if (!UiContextMenuService::hasConfigFor(config, contextMenuId))
    {
        return nullptr;
    }

    auto* dispatcher = m_window ? m_window->menuManager()->commandDispatcher()
                                 : nullptr;
    return UiContextMenuService::instance().buildMenu(config, contextMenuId, dispatcher, m_hub->mainWindow());
}
