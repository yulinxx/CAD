#include "Workbench2DViewportSetup.h"
#include "Workbench2D.h"
#include "RenderViewport2D.h"
#include "WorkbenchWindow.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/ISelectionService.h"
#include "UI2D/Service/SceneMonitor.h"
#include "UI/Documents/SceneDocument2D.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Import/ImportService.h"
#include "FileDropHandler.h"
#include <QTimer>
#include <optional>

Workbench2DViewportSetup::Workbench2DViewportSetup() = default;

QWidget* Workbench2DViewportSetup::createCentralViewport(WorkbenchWindow& window, QWidget* properties)
{
    Q_UNUSED(window);
    Q_UNUSED(properties);
    auto* viewport = new RenderViewport2D();
    m_viewport = viewport;
    return viewport;
}

void Workbench2DViewportSetup::setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window,
    OperationBus* bus, UiStateCenter* stateCenter,
    ISelectionService* selectionService, SceneDocument2D* document,
    SceneEditService* editService, LayerManager* layerManager, Eg::SceneManager* scene)
{
    vp->setSelectionService(selectionService);
    vp->setOperationBus(bus);
    vp->setLayerManager(layerManager);

    if (editService)
    {
        QObject::connect(
            vp, &RenderViewport2D::entitySubmitRequested, [service = editService](Eg::SyEntity* e) {
                if (e)
                {
                    service->addEntityFromPointer(e, "Draw");
                }
            });

        if (scene)
        {
            // 本管理器不是 QObject：监视器生命周期由 shutdown() 显式回收（deleteLater）
            m_sceneMonitor = new SceneMonitor();
            m_sceneMonitor->watch(scene);
        }
    }

    vp->setDocument(document);

    if (stateCenter)
    {
        vp->setStatusCallback([stateCenter](const QString& text) {
            stateCenter->setStatusPrompt(text);
        });

        vp->setPositionCallback([&window](double x, double y) {
            window.updatePositionLabel(x, y);
        });
    }
}

void Workbench2DViewportSetup::setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window, ImportService* importService)
{
    if (!importService)
    {
        return;
    }

    if (auto* fdh = window.fileDropHandler())
    {
        fdh->setScreenToWorldConverter([vp](const QPoint& globalPos) -> std::optional<QPointF> {
            if (!vp)
            {
                return std::nullopt;
            }
            return vp->mapGlobalToScene(globalPos);
        });
    }

    importService->setViewportFitCallback([vp]() {
        QTimer::singleShot(0, vp, [vp]() {
            vp->zoomToFit();
        });
    });
}

void Workbench2DViewportSetup::shutdown()
{
    if (m_sceneMonitor)
    {
        m_sceneMonitor->deleteLater();
        m_sceneMonitor = nullptr;
    }
    m_viewport = nullptr;
}
