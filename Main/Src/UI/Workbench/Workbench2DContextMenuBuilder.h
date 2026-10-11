#pragma once

#include <QMenu>
#include <QContextMenuEvent>

class CommandActionHub;
class LayerManager;
class WorkbenchWindow;

class Workbench2DContextMenuBuilder
{
public:
    Workbench2DContextMenuBuilder(CommandActionHub* hub, LayerManager* layerManager, WorkbenchWindow* window);

    void onViewportContextMenu(QContextMenuEvent* event);
    QMenu* buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection);

private:
    CommandActionHub* m_hub;
    LayerManager* m_layerManager;
    WorkbenchWindow* m_window;
};
