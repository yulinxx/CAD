#pragma once

#include <QObject>
#include <vector>

class QShortcut;
class RenderViewport2D;
class OperationBus;
class WorkbenchWindow;

namespace Eg
{
    class SceneManager;
}

class ISelectionService;

class Workbench2DShortcutManager : public QObject
{
    Q_OBJECT

public:
    Workbench2DShortcutManager(QObject* parent = nullptr);
    ~Workbench2DShortcutManager() override;

    void registerGlobalShortcuts(WorkbenchWindow& window, RenderViewport2D* viewport,
        OperationBus* bus, ISelectionService* selectionService);
    void unregisterGlobalShortcuts();

private:
    std::vector<QShortcut*> m_shortcuts;
};
