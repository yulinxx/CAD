#include "ViewOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/UiStateCenter.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "UI2D/Manager/UnitManager.h"
#include "UI/Service/ViewCaptureService.h"
#include "UI/Render/RenderViewport2D.h"

ViewOperationRegistry::ViewOperationRegistry(OperationBus* bus,
    ViewportActionHub* viewportActionHub,
    UiStateCenter* stateCenter,
    LayerEditService* layerEditService,
    UnitManager* unitManager,
    Ui::ViewCaptureService* captureService,
    QWidget* parentWidget)
    : m_bus(bus)
    , m_viewportActionHub(viewportActionHub)
    , m_stateCenter(stateCenter)
    , m_layerEditService(layerEditService)
    , m_unitManager(unitManager)
    , m_captureService(captureService)
    , m_parentWidget(parentWidget)
{
}

void ViewOperationRegistry::registerAll()
{
    if (!m_bus)
        return;

    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();
    auto* hub = m_viewportActionHub;
    auto* stateCenter = m_stateCenter;
    auto* layerEditService = m_layerEditService;
    auto* unitManager = m_unitManager;

    const auto registerViewOp = [&reg, hub](OperationId id, const QString& action) {
        reg.registerOperation(std::make_unique<LambdaOperation>(id, [hub, action] {
            if (hub)
                hub->handle(action);
        }));
    };

    registerViewOp(OperationId::View_ZoomFit, QStringLiteral("zoom_fit"));
    registerViewOp(OperationId::View_ZoomIn, QStringLiteral("zoom_in"));
    registerViewOp(OperationId::View_ZoomOut, QStringLiteral("zoom_out"));
    registerViewOp(OperationId::View_ZoomSelection, QStringLiteral("zoom_selection"));
    registerViewOp(OperationId::View_Pan, QStringLiteral("pan"));
    registerViewOp(OperationId::View_Reset, QStringLiteral("reset"));

    const auto toggleMetadata = [stateCenter](const char* key) {
        if (!stateCenter)
            return;
        QVariantMap meta = stateCenter->metadata();
        const QString keyStr = QLatin1String(key);
        meta[keyStr] = !meta.value(keyStr).toBool();
        stateCenter->setMetadata(meta);
    };

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::View_GridVisible, [toggleMetadata] {
        toggleMetadata("gridVisible");
    }));
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::View_SnapEnabled, [toggleMetadata] {
        toggleMetadata("snapEnabled");
    }));
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::View_OrthoMode, [toggleMetadata] {
        toggleMetadata("orthoMode");
    }));
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::View_AngleSnap, [toggleMetadata] {
        toggleMetadata("angleSnap");
    }));

    reg.registerOperation(
        std::make_unique<LambdaOperation>(OperationId::View_LayerManager, [=] {
            if (layerEditService)
                LayerManagerDialog::showDialog(layerEditService, m_parentWidget);
        }));

    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::View_SetDisplayUnit, [unitManager](const QVariantMap& params) {
            if (!unitManager)
                return;
            const int unit = params.value(QStringLiteral("unit"), static_cast<int>(UnitManager::Unit::Millimeter)).toInt();
            unitManager->setDisplayUnit(static_cast<UnitManager::Unit>(unit));
        }));

    reg.registerOperation(
        std::make_unique<LambdaOperation>(OperationId::View_Capture, [captureService = m_captureService, hub] {
            if (!captureService || !hub)
                return;
            if (auto* vp = hub->viewport())
            {
                if (auto* widget = vp->renderWidget())
                {
                    Ui::CaptureRequest req;
                    req.scope = Ui::CaptureScope::CurrentView;
                    req.framing = Ui::FramingKind::UseCurrent;
                    req.autoSave = true;
                    QImage img = captureService->capture2D(widget, req);
                    if (!img.isNull())
                    {
                        QString path = captureService->saveImage(img, req, false);
                        if (!path.isEmpty())
                            SY_DEBUGF("[View_Capture] Saved to %s", path.toStdString().c_str());
                    }
                }
            }
        }));
}
