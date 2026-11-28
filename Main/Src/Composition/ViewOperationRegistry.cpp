/**
 * @file ViewOperationRegistry.cpp
 * @brief 视图操作注册实现
 *
 * 注册和管理视图相关的操作命令。
 */
#include "ViewOperationRegistry.h"
#include "OpRegistryTypes.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "UI2D/Manager/UnitManager.h"

#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Service/ViewCaptureService.h"
#include "UI/Render/RenderViewport2D.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Geo/GeometryContext.h"
#include "Engine2D/Geometry/Geo2DQuery.h"
#include "UI/TestView/TestViewWindow.h"

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
        Ops::declare(reg, id, [hub, action] {
            if (hub)
                hub->handle(action);
        });
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

    Ops::declare(reg, OperationId::View_GridVisible, [toggleMetadata] {
        toggleMetadata("gridVisible");
    });
    Ops::declare(reg, OperationId::View_SnapEnabled, [toggleMetadata] {
        toggleMetadata("snapEnabled");
    });
    Ops::declare(reg, OperationId::View_OrthoMode, [toggleMetadata] {
        toggleMetadata("orthoMode");
    });
    Ops::declare(reg, OperationId::View_AngleSnap, [toggleMetadata] {
        toggleMetadata("angleSnap");
    });

    Ops::declare(reg, OperationId::View_LayerManager, [=] {
        if (layerEditService)
            LayerManagerDialog::showDialog(layerEditService, parentWidget);
    });

    Ops::declareParam(reg, OperationId::View_SetDisplayUnit, [unitManager](const QVariantMap& params) {
        if (!unitManager)
            return;
        const int unit = params.value(QStringLiteral("unit"), static_cast<int>(UnitManager::Unit::Millimeter)).toInt();
        unitManager->setDisplayUnit(static_cast<UnitManager::Unit>(unit));
    });

    Ops::declare(reg, OperationId::View_Capture, [captureService = m_captureService, hub] {
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
                    {
                        APR_DEBUGF("[View_Capture] Saved to %s", path.toStdString().c_str());
                        // 保存成功后打开图片所在目录
                        QString dir = QFileInfo(path).absolutePath();
                        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                    }
                }
            }
        }
    });

    Ops::declare(reg, OperationId::View_TestView, [hub, parentWidget] {
        if (!hub)
            return;
        if (auto* vp = hub->viewport())
        {
            if (auto* scene = vp->sceneManager())
            {
                auto* window = new TestViewWindow(scene, false, parentWidget);
                window->show();
            }
        }
    });

    Ops::declare(reg, OperationId::View_Measure, [hub, parentWidget] {
        if (!hub)
            return;
        auto* vp = hub->viewport();
        if (!vp)
            return;
        auto* scene = vp->sceneManager();
        if (!scene)
            return;
        const auto selected = scene->getSelectedEntities();
        if (selected.empty())
            return;

        const Eg::GeometryContext ctx;
        Ut::BBox2d bbox;
        double totalLength = 0.0;
        double totalArea = 0.0;
        int closedCount = 0;
        int openCount = 0;
        for (Eg::AprEntity* e : selected)
        {
            if (!e)
                continue;
            const Ut::BBox2d b = e->getBbox();
            if (b.isValid())
                bbox.expand(b);
            totalLength += Eg::Geo2DQuery::length(e, ctx);
            if (Eg::Geo2DQuery::isClosedByType(e))
            {
                ++closedCount;
                totalArea += Eg::Geo2DQuery::area(e, ctx);
            }
            else
            {
                ++openCount;
            }
        }

        QString msg = QObject::tr("Selected: %1\nClosed: %2  Open: %3\n")
                          .arg(selected.size())
                          .arg(closedCount)
                          .arg(openCount);
        if (bbox.isValid())
        {
            msg += QObject::tr("BBox: X [%1, %2]  Y [%3, %4]\nWidth: %5  Height: %6\n")
                       .arg(bbox.minPt.x(), 0, 'f', 3)
                       .arg(bbox.maxPt.x(), 0, 'f', 3)
                       .arg(bbox.minPt.y(), 0, 'f', 3)
                       .arg(bbox.maxPt.y(), 0, 'f', 3)
                       .arg(bbox.maxPt.x() - bbox.minPt.x(), 0, 'f', 3)
                       .arg(bbox.maxPt.y() - bbox.minPt.y(), 0, 'f', 3);
        }
        msg += QObject::tr("Total length: %1\nTotal area: %2").arg(totalLength, 0, 'f', 3).arg(totalArea, 0, 'f', 3);
        QMessageBox::information(parentWidget, QObject::tr("Measure"), msg);
    });
}