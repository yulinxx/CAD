#pragma once

#include <QWidget>
#include <memory>

class RenderViewport2D;
class WorkbenchWindow;
class OperationBus;
class UiStateCenter;
class SceneMonitor;
class ImportService;
class SceneDocument2D;
class SceneEditService;
class LayerManager;

namespace Eg
{
    class SceneManager;
}

class ISelectionService;

class Workbench2DViewportSetup
{
public:
    Workbench2DViewportSetup();

    QWidget* createCentralViewport(WorkbenchWindow& window, QWidget* properties);
    void setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window,
        OperationBus* bus, UiStateCenter* stateCenter,
        ISelectionService* selectionService, SceneDocument2D* document,
        SceneEditService* editService, LayerManager* layerManager, Eg::SceneManager* scene);
    void setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window, ImportService* importService);

    RenderViewport2D* viewport() const { return m_viewport; }
    SceneMonitor* sceneMonitor() const { return m_sceneMonitor; }

    void shutdown();

private:
    RenderViewport2D* m_viewport{ nullptr };
    SceneMonitor* m_sceneMonitor{ nullptr };
};
