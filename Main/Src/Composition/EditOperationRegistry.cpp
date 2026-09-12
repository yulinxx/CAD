#include "EditOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/EntityClipboard.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/IUndoRedoManager.h"
#include "Engine2D/Edit/FilletChamfer.h"
#include "Engine2D/Algorithm/EntityTransform.h"
#include "Engine2D/Algorithm/Discretizer/EntityDiscretizer.h"
#include "Engine2D/Geometry/BezierAlgorithms.h"
#include "Engine2D/Core/SceneChangeSet.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"

#include "UI/Services/HelpDialogService.h"
#include "UI/Adapters/TransformDialogAdapter.h"
#include "UI/TransformParameters.h"
#include "UI2D/Operation/AlgorithmRunner.h"
#include "Option/TextPasteService.h"
#include "Option/ImagePasteService.h"
#include "Option/TextInputSettingsStore.h"
#include "Operation/ReliefEngravingOperation2D.h"
#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/UiStateCenter.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "UI2D/Manager/UnitManager.h"
#include "Ut/BBox2d.h"
#include "Ut/GeomMath.h"
#include "Ut/Mat.h"
#include "Ut/Vec.h"
#include "Log/SyLogger.h"
#include "UI/Service/ViewCaptureService.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Workbench/UiWorkbench.h"
#include "UI/Render/RenderViewport2D.h"

#include <QObject>
#include <QWidget>
#include <QCursor>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <memory>
#include <functional>

EditOperationRegistry::EditOperationRegistry(OperationBus* bus,
    SceneEditService* editService,
    IUndoRedoManager* undoManager,
    Eg::EntityClipboard* clipboard,
    AlgorithmRunner* algorithmRunner,
    ViewportActionHub* viewportActionHub,
    UiStateCenter* stateCenter,
    LayerEditService* layerEditService,
    UnitManager* unitManager,
    QWidget* parentWidget,
    Ui::ViewCaptureService* captureService)
    : m_bus(bus)
    , m_editService(editService)
    , m_undoManager(undoManager)
    , m_clipboard(clipboard)
    , m_algorithmRunner(algorithmRunner)
    , m_viewportActionHub(viewportActionHub)
    , m_stateCenter(stateCenter)
    , m_layerEditService(layerEditService)
    , m_unitManager(unitManager)
    , m_parentWidget(parentWidget)
    , m_captureService(captureService)
{
}

void EditOperationRegistry::registerAll()
{
    if (!m_bus || !m_editService || !m_undoManager)
        return;

    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* undoManager = m_undoManager;
    auto* hub = m_viewportActionHub;

    // ---- 撤销/重做/删除/全选/清除/反选 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Undo, [undoManager, hub] {
        if (hub && hub->viewport() && hub->viewport()->handleTextUndoRequest(false))
            return;
        if (undoManager && undoManager->canUndo())
            undoManager->undo();
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Redo, [undoManager, hub] {
        if (hub && hub->viewport() && hub->viewport()->handleTextUndoRequest(true))
            return;
        if (undoManager && undoManager->canRedo())
            undoManager->redo();
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Delete, [editService] {
        if (editService)
            editService->deleteSelected("Delete");
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_SelectAll, [editService] {
        if (editService && editService->sceneManager())
        {
            auto* scene = editService->sceneManager();
            Eg::VecSyEntityPtr visibleEntities;
            const auto allEntities = scene->getAllEntities();
            for (auto* entity : allEntities)
            {
                if (entity && entity->visible())
                {
                    if (auto* layer = entity->layer())
                    {
                        if (!layer->isVisible())
                            continue;
                    }
                    visibleEntities.push_back(entity);
                }
            }
            scene->selectEntities(visibleEntities);
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_ClearSelection, [editService] {
        if (editService && editService->sceneManager())
            editService->sceneManager()->clearSelection();
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_InvertSelection, [editService] {
        if (editService && editService->sceneManager())
            editService->sceneManager()->invertSelection();
    }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Nudge, [editService](const QVariantMap& params) {
            if (!editService)
                return;
            const double dx = params.value(QStringLiteral("dx")).toDouble();
            const double dy = params.value(QStringLiteral("dy")).toDouble();
            if (dx == 0.0 && dy == 0.0)
                return;
            editService->nudgeSelected(dx, dy, "Nudge");
        }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Fillet, [editService](const QVariantMap& params) {
            double radius = params.value("radius", -1.0).toDouble();
            if (radius < 0.0)
            {
                auto* scene = editService->sceneManager();
                if (!scene)
                    return;
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                    return;
                bool ok = false;
                radius = HelpDialogService::getDouble(
                    nullptr, QObject::tr("Fillet Radius"), QObject::tr("Radius:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || radius < 0.1)
                    return;
            }
            Eg::FilletChamfer::applyFillet(*editService, radius);
        }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Chamfer, [editService](const QVariantMap& params) {
            double distance = params.value("distance", -1.0).toDouble();
            if (distance < 0.0)
            {
                auto* scene = editService->sceneManager();
                if (!scene)
                    return;
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                    return;
                bool ok = false;
                distance = HelpDialogService::getDouble(
                    nullptr, QObject::tr("Chamfer Distance"), QObject::tr("Distance:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || distance < 0.1)
                    return;
            }
            Eg::FilletChamfer::applyChamfer(*editService, distance);
        }));

    registerClipboardOps();
    registerTransformOps();
    registerGroupOps();
    registerTrimExtendOps();
    registerBboxOps();
    registerDiscretizeOp();
    registerBezierOps();
    registerArrayOp();
}

void EditOperationRegistry::registerClipboardOps()
{
    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* clipboard = m_clipboard;
    auto* hub = m_viewportActionHub;

    auto pasteAnchor = [hub]() -> Ut::Vec2d {
        Ut::Vec2d anchor(0, 0);
        if (hub && hub->viewport())
        {
            const QPoint cursorLocal = hub->viewport()->mapFromGlobal(QCursor::pos());
            const QRect viewRect = hub->viewport()->rect();
            if (viewRect.contains(cursorLocal))
            {
                const QPointF world = hub->viewport()->widgetToWorld(cursorLocal);
                anchor = Ut::Vec2d(world.x(), world.y());
            }
            else
            {
                const QPointF centerWorld = hub->viewport()->viewportCenterWorld();
                anchor = Ut::Vec2d(centerWorld.x(), centerWorld.y());
            }
        }
        if (anchor.x() == 0.0 && anchor.y() == 0.0)
            anchor = Ut::Vec2d(600.0, 400.0);
        return anchor;
    };

    auto pasteText = [editService, pasteAnchor](bool selectResult) -> bool {
        if (!editService)
            return false;
        auto* scene = editService->sceneManager();
        if (!scene)
            return false;
        const Ut::Vec2d anchor = pasteAnchor();
        QString err;
        Eg::SyGroup* group = TextPasteService::pasteClipboardText(scene, anchor, err);
        if (!group)
        {
            if (!err.isEmpty())
                SY_WARNF("[PasteText] %s", err.toStdString().c_str());
            return false;
        }
        if (selectResult)
        {
            scene->clearSelection();
            std::vector<Eg::SyEntity*> leaves = group->flatten();
            if (!leaves.empty())
                scene->selectEntities(leaves);
        }
        return true;
    };

    auto pasteImage = [editService, pasteAnchor](bool selectResult) {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        const Ut::Vec2d anchor = pasteAnchor();
        QString err;
        Eg::SyEntity* image = ImagePasteService::pasteClipboardImage(scene, anchor, err, nullptr, editService);
        if (!image)
        {
            if (!err.isEmpty())
                SY_WARNF("[PasteImage] %s", err.toStdString().c_str());
            return;
        }
        SY_DEBUG("[PasteImage] Clipboard image pasted");
        if (selectResult)
        {
            scene->clearSelection();
            Eg::VecSyEntityPtr imgSel;
            imgSel.push_back(image);
            scene->selectEntities(imgSel);
        }
    };

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Copy, [editService, clipboard] {
            if (!editService || !clipboard)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;
            std::vector<const Eg::SyEntity*> sources;
            sources.reserve(selected.size());
            for (Eg::SyEntity* e : selected)
            {
                if (e)
                    sources.push_back(e);
            }
            clipboard->copy(sources);
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Cut, [editService, clipboard] {
        if (!editService || !clipboard)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
            return;
        std::vector<const Eg::SyEntity*> sources;
        sources.reserve(selected.size());
        for (Eg::SyEntity* e : selected)
            sources.push_back(e);
        clipboard->copy(sources);
        editService->deleteSelected("Cut");
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Paste, [editService, clipboard, pasteAnchor, pasteText, pasteImage] {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            if (clipboard && clipboard->hasContent())
            {
                const Ut::Vec2d pastePos = pasteAnchor();
                auto pasted = clipboard->paste(pastePos);
                if (pasted.empty())
                    return;
                scene->clearSelection();
                const std::vector<Eg::EntityId> insertedIds = editService->addEntities(std::move(pasted), "Paste");
                Eg::VecSyEntityPtr pastedEntities;
                pastedEntities.reserve(insertedIds.size());
                for (Eg::EntityId id : insertedIds)
                {
                    if (auto* ent = scene->findEntityById(id))
                        pastedEntities.push_back(ent);
                }
                if (!pastedEntities.empty())
                    scene->selectEntities(pastedEntities);
                return;
            }
            if (pasteText(true))
                return;
            pasteImage(true);
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_PasteText, [pasteText] {
        pasteText(true);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_PasteImage, [pasteImage] {
        pasteImage(true);
    }));

    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Duplicate, [=](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;

            TransformParameters tp;
            if (params.contains(QStringLiteral("moveX")))
            {
                tp.type = TransformType::Copy;
                tp.copyCount = params.value(QStringLiteral("copyCount"), 2).toInt();
                tp.moveX = params.value(QStringLiteral("moveX")).toDouble();
                tp.moveY = params.value(QStringLiteral("moveY")).toDouble();
            }
            else
            {
                tp = OpRegistryHelpers::collectDialogParams(TransformType::Copy, m_parentWidget);
            }
            if (tp.type != TransformType::Copy)
                return;

            const int copyCount = tp.copyCount >= 1 ? tp.copyCount : 2;
            const double dx = tp.moveX;
            const double dy = tp.moveY;

            std::vector<const Eg::SyEntity*> sources;
            sources.reserve(selected.size());
            for (Eg::SyEntity* e : selected)
            {
                if (e)
                    sources.push_back(e);
            }

            std::vector<std::unique_ptr<Eg::SyEntity>> added;
            for (int i = 1; i < copyCount; ++i)
            {
                const double offX = dx * static_cast<double>(i);
                const double offY = dy * static_cast<double>(i);
                for (const Eg::SyEntity* src : sources)
                {
                    auto snap = std::unique_ptr<Eg::SyEntity>(src->clone());
                    if (!snap)
                        continue;
                    snap->transform(Ut::Mat3d::translate(offX, offY));
                    snap->id = 0;
                    snap->setModified();
                    added.emplace_back(std::move(snap));
                }
            }

            if (!added.empty())
                editService->addEntities(std::move(added), "Duplicate");
        }));
}

void EditOperationRegistry::registerTransformOps()
{
    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* clipboard = m_clipboard;
    auto* hub = m_viewportActionHub;
    auto* undoManager = m_undoManager;

    reg.registerOperation(std::make_unique<ParamSceneMutatingLambdaOperation>(
        OperationId::Edit_Move, [=](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;

            TransformParameters tp;
            if (params.contains(QStringLiteral("moveX")))
            {
                tp.type = TransformType::Move;
                tp.moveX = params.value(QStringLiteral("moveX")).toDouble();
                tp.moveY = params.value(QStringLiteral("moveY")).toDouble();
            }
            else
            {
                tp = OpRegistryHelpers::collectDialogParams(TransformType::Move, m_parentWidget);
            }
            if (tp.type != TransformType::Move)
                return;

            const auto ids = OpRegistryHelpers::collectIds(selected);
            editService->transformEntities(
                ids,
                [&]() {
                    EntityTransform transform(scene);
                    transform.moveByIds(ids, tp.moveX, tp.moveY);
                },
                "Move",
                false);
        }));

    reg.registerOperation(std::make_unique<ParamSceneMutatingLambdaOperation>(
        OperationId::Edit_Rotate, [=](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;

            const auto ids = OpRegistryHelpers::collectIds(selected);
            double angleRad = 0.0;
            double centerX = 0.0;
            double centerY = 0.0;

            if (params.contains(QStringLiteral("angle")))
            {
                const double angleDeg = params.value(QStringLiteral("angle")).toDouble();
                angleRad = Ut::GeomMath::degToRad(-angleDeg);
                double minX, minY, maxX, maxY;
                if (OpRegistryHelpers::calcCombinedBounds(selected, minX, minY, maxX, maxY))
                {
                    centerX = (minX + maxX) * 0.5;
                    centerY = (minY + maxY) * 0.5;
                }
            }
            else
            {
                const TransformParameters tp = OpRegistryHelpers::collectDialogParams(TransformType::Rotate, m_parentWidget);
                if (tp.type != TransformType::Rotate || tp.rotateAngle == 0.0)
                    return;
                angleRad = Ut::GeomMath::degToRad(tp.rotateAngle);
                if (tp.hasAnchor)
                {
                    centerX = tp.anchorX;
                    centerY = tp.anchorY;
                }
                else
                {
                    double minX, minY, maxX, maxY;
                    if (OpRegistryHelpers::calcCombinedBounds(selected, minX, minY, maxX, maxY))
                    {
                        centerX = (minX + maxX) * 0.5;
                        centerY = (minY + maxY) * 0.5;
                    }
                }
            }

            editService->transformEntities(
                ids,
                [&]() {
                    EntityTransform transform(scene);
                    transform.rotateByIds(ids, angleRad, centerX, centerY);
                },
                "Rotate",
                false);
        }));

    reg.registerOperation(std::make_unique<ParamSceneMutatingLambdaOperation>(
        OperationId::Edit_Mirror, [=](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;

            const auto ids = OpRegistryHelpers::collectIds(selected);
            int axis = 0;
            double centerX = 0.0;
            double centerY = 0.0;
            bool useLine = false;
            double lineX1 = 0.0, lineY1 = 0.0, lineX2 = 0.0, lineY2 = 0.0;

            if (params.contains(QStringLiteral("mirrorAxis")))
            {
                axis = params.value(QStringLiteral("mirrorAxis")).toInt();
                centerX = params.value(QStringLiteral("mirrorCenterX")).toDouble();
                centerY = params.value(QStringLiteral("mirrorCenterY")).toDouble();
                if (axis == 2)
                {
                    useLine = true;
                    lineX1 = params.value(QStringLiteral("mirrorLineX1")).toDouble();
                    lineY1 = params.value(QStringLiteral("mirrorLineY1")).toDouble();
                    lineX2 = params.value(QStringLiteral("mirrorLineX2")).toDouble();
                    lineY2 = params.value(QStringLiteral("mirrorLineY2")).toDouble();
                }
            }
            else
            {
                const TransformParameters tp = OpRegistryHelpers::collectDialogParams(TransformType::Mirror, m_parentWidget);
                if (tp.type != TransformType::Mirror)
                    return;
                axis = tp.mirrorAxis;
                centerX = tp.mirrorCenterX;
                centerY = tp.mirrorCenterY;
                if (axis == 2)
                {
                    useLine = true;
                    lineX1 = tp.mirrorLineX1;
                    lineY1 = tp.mirrorLineY1;
                    lineX2 = tp.mirrorLineX2;
                    lineY2 = tp.mirrorLineY2;
                }
            }

            editService->transformEntities(
                ids,
                [&]() {
                    EntityTransform transform(scene);
                    if (useLine)
                        transform.mirrorByLineIds(ids, lineX1, lineY1, lineX2, lineY2);
                    else
                        transform.mirrorByIds(ids, axis, centerX, centerY);
                },
                "Mirror",
                false);
        }));

    reg.registerOperation(std::make_unique<SceneMutatingLambdaOperation>(OperationId::Edit_MirrorH, [editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
            return;
        const auto ids = OpRegistryHelpers::collectIds(selected);
        double minX, minY, maxX, maxY;
        const double centerX = OpRegistryHelpers::calcCombinedBounds(selected, minX, minY, maxX, maxY) ? (minX + maxX) * 0.5 : 0.0;
        editService->transformEntities(
            ids,
            [&]() {
                EntityTransform transform(scene);
                transform.mirrorByIds(ids, 1, centerX, 0.0);
            },
            "Mirror Horizontal",
            false);
    }));

    reg.registerOperation(std::make_unique<SceneMutatingLambdaOperation>(OperationId::Edit_MirrorV, [editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
            return;
        const auto ids = OpRegistryHelpers::collectIds(selected);
        double minX, minY, maxX, maxY;
        const double centerY = OpRegistryHelpers::calcCombinedBounds(selected, minX, minY, maxX, maxY) ? (minY + maxY) * 0.5 : 0.0;
        editService->transformEntities(
            ids,
            [&]() {
                EntityTransform transform(scene);
                transform.mirrorByIds(ids, 0, 0.0, centerY);
            },
            "Mirror Vertical",
            false);
    }));

    reg.registerOperation(
        std::make_unique<ParamSceneMutatingLambdaOperation>(OperationId::Edit_Align, [editService](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;
            const int mode = params.value(QStringLiteral("mode"), 0).toInt();
            const auto ids = OpRegistryHelpers::collectIds(selected);
            editService->transformEntities(
                ids,
                [&]() {
                    EntityTransform transform(scene);
                    transform.alignByIds(ids, OpRegistryHelpers::toEntityAlign(mode));
                },
                "Align",
                false);
        }));
}

void EditOperationRegistry::registerGroupOps()
{
    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();
    auto* editService = m_editService;

    reg.registerOperation(std::make_unique<SceneMutatingLambdaOperation>(OperationId::Edit_GroupToggle, [editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        bool hasGroup = false;
        for (Eg::SyEntity* e : scene->getSelectedEntities())
        {
            if (e && e->group())
            {
                hasGroup = true;
                break;
            }
        }
        if (hasGroup)
            editService->ungroupSelectedEntities();
        else
        {
            auto selected = scene->getSelectedEntities();
            if (selected.size() >= 2)
            {
                std::vector<Eg::EntityId> ids;
                ids.reserve(selected.size());
                for (Eg::SyEntity* e : selected)
                {
                    if (e)
                        ids.push_back(e->id);
                }
                editService->groupEntities(ids, "Group");
            }
        }
    }));

    reg.registerOperation(std::make_unique<SceneMutatingLambdaOperation>(OperationId::Edit_Ungroup, [editService] {
        if (editService)
            editService->ungroupSelectedEntities();
    }));
}

void EditOperationRegistry::registerTrimExtendOps()
{
    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();
    auto* editService = m_editService;

    reg.registerOperation(
        std::make_unique<ParamSceneMutatingLambdaOperation>(OperationId::Edit_Trim, [editService](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            TransformParameters tp = TransformParameters::createTrim(
                params.value(QStringLiteral("targetId")).toULongLong(),
                params.value(QStringLiteral("boundaryId")).toULongLong());
            Eg::SyEntity* target = nullptr;
            Eg::SyEntity* boundary = nullptr;
            if (!OpRegistryHelpers::resolveTargetAndBoundary(editService, tp, target, boundary))
                return;

            OpRegistryHelpers::LineSegment2D targetSeg, boundarySeg;
            if (!OpRegistryHelpers::extractLineSegment(target, targetSeg) || !extractLineSegment(boundary, boundarySeg))
                return;

            double hitX = 0.0, hitY = 0.0;
            if (!OpRegistryHelpers::segmentIntersection(targetSeg, boundarySeg, hitX, hitY))
                return;

            const double d1 = (hitX - targetSeg.x1) * (hitX - targetSeg.x1) + (hitY - targetSeg.y1) * (hitY - targetSeg.y1);
            const double d2 = (hitX - targetSeg.x2) * (hitX - targetSeg.x2) + (hitY - targetSeg.y2) * (hitY - targetSeg.y2);
            const double newX1 = (d1 <= d2) ? hitX : targetSeg.x1;
            const double newY1 = (d1 <= d2) ? hitY : targetSeg.y1;
            const double newX2 = (d1 <= d2) ? targetSeg.x2 : hitX;
            const double newY2 = (d1 <= d2) ? targetSeg.y2 : hitY;
            const double newLenSq = (newX2 - newX1) * (newX2 - newX1) + (newY2 - newY1) * (newY2 - newY1);
            if (newLenSq < 1e-12)
                return;

            const std::vector<Eg::EntityId> ids{ target->id };
            editService->transformEntities(
                ids,
                [&]() {
                    auto* line = static_cast<Eg::SyLine*>(target);
                    line->setPointAt(0, Ut::Vec2d(newX1, newY1));
                    line->setPointAt(1, Ut::Vec2d(newX2, newY2));
                    line->basePoint = line->pointRef()[0];
                },
                "Trim",
                false);
        }));

    reg.registerOperation(
        std::make_unique<ParamSceneMutatingLambdaOperation>(OperationId::Edit_Extend, [editService](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            TransformParameters tp = TransformParameters::createExtend(
                params.value(QStringLiteral("targetId")).toULongLong(),
                params.value(QStringLiteral("boundaryId")).toULongLong());
            Eg::SyEntity* target = nullptr;
            Eg::SyEntity* boundary = nullptr;
            if (!OpRegistryHelpers::resolveTargetAndBoundary(editService, tp, target, boundary))
                return;

            OpRegistryHelpers::LineSegment2D targetSeg, boundarySeg;
            if (!OpRegistryHelpers::extractLineSegment(target, targetSeg) || !extractLineSegment(boundary, boundarySeg))
                return;

            const double tx = targetSeg.x2 - targetSeg.x1;
            const double ty = targetSeg.y2 - targetSeg.y1;
            const double tlen = std::sqrt(tx * tx + ty * ty);
            if (tlen < 1e-12)
                return;
            const double ux = tx / tlen, uy = ty / tlen;
            const double ext = tlen * 100.0;

            OpRegistryHelpers::LineSegment2D rayFromP1, rayFromP2;
            rayFromP1.x1 = targetSeg.x1 - ux * ext; rayFromP1.y1 = targetSeg.y1 - uy * ext;
            rayFromP1.x2 = targetSeg.x1; rayFromP1.y2 = targetSeg.y1;
            rayFromP2.x1 = targetSeg.x2; rayFromP2.y1 = targetSeg.y2;
            rayFromP2.x2 = targetSeg.x2 + ux * ext; rayFromP2.y2 = targetSeg.y2 + uy * ext;

            double hitX1 = 0.0, hitY1 = 0.0, hitX2 = 0.0, hitY2 = 0.0;
            const bool ok1 = segmentIntersection(rayFromP1, boundarySeg, hitX1, hitY1);
            const bool ok2 = segmentIntersection(rayFromP2, boundarySeg, hitX2, hitY2);
            if (!ok1 && !ok2)
                return;

            const double d1 = (hitX1 - targetSeg.x1) * (hitX1 - targetSeg.x1) + (hitY1 - targetSeg.y1) * (hitY1 - targetSeg.y1);
            const double d2 = (hitX2 - targetSeg.x2) * (hitX2 - targetSeg.x2) + (hitY2 - targetSeg.y2) * (hitY2 - targetSeg.y2);
            double newX1 = targetSeg.x1, newY1 = targetSeg.y1;
            double newX2 = targetSeg.x2, newY2 = targetSeg.y2;
            if (ok1 && (!ok2 || d1 >= d2))
            {
                newX1 = hitX1; newY1 = hitY1;
            }
            else if (ok2)
            {
                newX2 = hitX2; newY2 = hitY2;
            }

            const double newLenSq = (newX2 - newX1) * (newX2 - newX1) + (newY2 - newY1) * (newY2 - newY1);
            if (newLenSq <= tlen * tlen)
                return;

            const std::vector<Eg::EntityId> ids{ target->id };
            editService->transformEntities(
                ids,
                [&]() {
                    auto* line = static_cast<Eg::SyLine*>(target);
                    line->setPointAt(0, Ut::Vec2d(newX1, newY1));
                    line->setPointAt(1, Ut::Vec2d(newX2, newY2));
                    line->basePoint = line->pointRef()[0];
                },
                "Extend",
                false);
        }));
}

void EditOperationRegistry::registerBboxOps()
{
    auto& reg = m_bus->registry();
    auto* editService = m_editService;

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_GetBbox, [editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        Eg::SyEntity* entity = scene->getSelectedEntity();
        if (!entity)
            return;
        const Ut::BBox2d bbox = entity->getBbox();
        SY_DEBUGF("[GetBbox] entity=%llu bbox=(%.3f, %.3f)-(%.3f, %.3f)",
            static_cast<unsigned long long>(entity->id),
            bbox.minPt.x(), bbox.minPt.y(),
            bbox.maxPt.x(), bbox.maxPt.y());
    }));
}

void EditOperationRegistry::registerDiscretizeOp()
{
    auto& reg = m_bus->registry();
    auto* editService = m_editService;

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Discretize, [editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        Eg::SyEntity* entity = scene->getSelectedEntity();
        if (!entity)
        {
            auto all = scene->getAllEntities();
            if (all.empty())
                return;
            entity = all.front();
        }

        Eg::EntityDiscretizer discretizer;
        std::vector<Ut::Vec2dVector> vOutLines;
        if (!discretizer.doDiscretize(entity, vOutLines) || vOutLines.empty())
            return;

        SceneChangeSet changeSet;
        changeSet.toRemove.push_back(entity->id);
        for (const auto& line : vOutLines)
        {
            if (line.size() < 2)
                continue;
            changeSet.toAdd.push_back(std::make_unique<Eg::SyLine>(line));
        }
        if (!changeSet.toAdd.empty())
            editService->applyChangeSet(std::move(changeSet), "Discretize");
    }));
}

void EditOperationRegistry::registerBezierOps()
{
    auto& reg = m_bus->registry();
    auto* editService = m_editService;

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_BezierToggle, [this, editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        const OperationId target = OpRegistryHelpers::canMergeSelectedBeziers(scene) ? OperationId::Edit_MergeBezier : OperationId::Edit_SplitBezier;
        if (m_bus)
            m_bus->run(target);
    }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_SplitBezier, [editService](const QVariantMap& params) {
            if (!editService)
                return;
            auto* scene = editService->sceneManager();
            if (!scene)
                return;
            const double dT = params.value(QStringLiteral("t"), 0.5).toDouble();
            if (dT <= 0.0 || dT >= 1.0)
                return;
            const auto selected = scene->getSelectedEntities();
            if (selected.empty())
                return;

            SceneChangeSet changeSet;
            int nSplit = 0;
            for (Eg::SyEntity* e : selected)
            {
                if (!e || e->eType != Eg::EType::BEZIER)
                    continue;
                auto* bezier = static_cast<Eg::SyBezier*>(e);
                auto pair = Eg::BezierAlgorithms::splitBezier(bezier, dT);
                changeSet.toRemove.push_back(e->id);
                changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier>(pair.first));
                changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier>(pair.second));
                ++nSplit;
            }
            if (nSplit > 0)
                editService->applyChangeSet(std::move(changeSet), "Split Bezier");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_MergeBezier, [editService] {
        if (!editService)
            return;
        auto* scene = editService->sceneManager();
        if (!scene)
            return;
        std::vector<Eg::SyEntity*> vCubic;
        std::vector<Eg::SyEntity*> vQuad;
        OpRegistryHelpers::collectBezierCandidates(scene, vCubic, vQuad);

        SceneChangeSet changeSet;
        if (vCubic.size() == 2)
        {
            auto* b1 = static_cast<Eg::SyBezier*>(vCubic[0]);
            auto* b2 = static_cast<Eg::SyBezier*>(vCubic[1]);
            auto merged = Eg::BezierAlgorithms::mergeBeziers(b1, b2);
            if (merged)
            {
                changeSet.toRemove.push_back(vCubic[0]->id);
                changeSet.toRemove.push_back(vCubic[1]->id);
                changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier>(*merged));
            }
        }
        else if (vQuad.size() == 2)
        {
            auto* b1 = static_cast<Eg::SyBezier2*>(vQuad[0]);
            auto* b2 = static_cast<Eg::SyBezier2*>(vQuad[1]);
            auto merged = Eg::BezierAlgorithms::mergeBeziers(b1, b2);
            if (merged)
            {
                changeSet.toRemove.push_back(vQuad[0]->id);
                changeSet.toRemove.push_back(vQuad[1]->id);
                changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier2>(*merged));
            }
        }
        if (!changeSet.empty())
            editService->applyChangeSet(std::move(changeSet), "Merge Bezier");
    }));
}

void EditOperationRegistry::registerArrayOp()
{
    auto& reg = m_bus->registry();
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Array, [m_algorithmRunner = m_algorithmRunner] {
        if (!m_algorithmRunner)
            return;
        OperationRequest req;
        req.id = OperationId::Algo_Array;
        req.source = OperationSource::Menu;
        m_algorithmRunner->runForOperation(OperationId::Algo_Array, req);
    }));
}
