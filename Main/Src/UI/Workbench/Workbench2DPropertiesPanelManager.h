#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>

class WorkbenchWindow;
class CommandActionHub;
class QWidget;
class SceneEditService;

class EntityPropertyEditSession2D;

class Workbench2DPropertiesPanelManager : public QObject
{
    Q_OBJECT
public:
    Workbench2DPropertiesPanelManager();
    ~Workbench2DPropertiesPanelManager() override;

    void setup(WorkbenchWindow* window, SceneEditService* editService, CommandActionHub* hub);

    void scheduleRefresh();
    void refresh();

    void shutdown();

private:
    WorkbenchWindow* m_window{ nullptr };
    SceneEditService* m_editService{ nullptr };
    CommandActionHub* m_hub{ nullptr };
    QTimer* m_refreshTimer{ nullptr };
};
