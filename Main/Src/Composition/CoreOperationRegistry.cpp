#include "CoreOperationRegistry.h"

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
#include "Option/TextInputSettingsStore.h"
#include "Operation/ReliefEngravingOperation2D.h"
#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Dlg/LayerManagerDialog.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "Manager/UnitManager/UnitManager.h"
#include "Ut/BBox2d.h"
#include "Ut/GeomMath.h"
#include "Ut/Mat.h"
#include "Ut/Vec.h"
#include "Log/SyLogger.h"
#include "UiWorkbench.h"
#include "WorkbenchWindow.h"
#include "RenderViewport2D.h"

#include <QObject>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    std::vector<Eg::EntityId> collectIds(const Eg::VecSyEntityPtr& selected)
    {
        std::vector<Eg::EntityId> ids;
        ids.reserve(selected.size());
        for (Eg::SyEntity* e : selected)
        {
            if (e)
            {
                ids.push_back(e->id);
            }
        }
        return ids;
    }

    bool calcCombinedBounds(
        const Eg::VecSyEntityPtr& entities, double& outMinX, double& outMinY, double& outMaxX, double& outMaxY)
    {
        if (entities.empty())
        {
            return false;
        }

        double minX = (std::numeric_limits<double>::max)();
        double minY = (std::numeric_limits<double>::max)();
        double maxX = -(std::numeric_limits<double>::max)();
        double maxY = -(std::numeric_limits<double>::max)();
        bool valid = false;

        for (Eg::SyEntity* e : entities)
        {
            if (!e)
            {
                continue;
            }
            const Ut::BBox2d bb = e->getBbox();
            if (!bb.isValid())
            {
                continue;
            }
            minX = (std::min)(minX, static_cast<double>(bb.minPt.x()));
            minY = (std::min)(minY, static_cast<double>(bb.minPt.y()));
            maxX = (std::max)(maxX, static_cast<double>(bb.maxPt.x()));
            maxY = (std::max)(maxY, static_cast<double>(bb.maxPt.y()));
            valid = true;
        }

        if (!valid)
        {
            return false;
        }
        outMinX = minX;
        outMinY = minY;
        outMaxX = maxX;
        outMaxY = maxY;
        return true;
    }

    EntityTransform::AlignMode toEntityAlign(int mode)
    {
        switch (mode)
        {
        case 0:
            return EntityTransform::AlignMode::Left;
        case 1:
            return EntityTransform::AlignMode::Right;
        case 2:
            return EntityTransform::AlignMode::Top;
        case 3:
            return EntityTransform::AlignMode::Bottom;
        case 4:
            return EntityTransform::AlignMode::CenterH;
        case 5:
            return EntityTransform::AlignMode::CenterV;
        default:
            return EntityTransform::AlignMode::Left;
        }
    }

    TransformParameters collectDialogParams(TransformType type, QWidget* parent)
    {
        TransformDialogAdapter adapter(nullptr, parent);
        adapter.setTransformType(type);
        return adapter.getParameters();
    }

    struct LineSegment2D
    {
        double x1{ 0.0 };
        double y1{ 0.0 };
        double x2{ 0.0 };
        double y2{ 0.0 };
    };

    bool extractLineSegment(Eg::SyEntity* entity, LineSegment2D& outSeg)
    {
        if (!entity || entity->eType != Eg::EType::LINE)
        {
            return false;
        }
        auto* line = static_cast<Eg::SyLine*>(entity);
        if (line->pointRef().size() < 2)
        {
            return false;
        }
        outSeg.x1 = line->pointRef()[0].x();
        outSeg.y1 = line->pointRef()[0].y();
        outSeg.x2 = line->pointRef()[1].x();
        outSeg.y2 = line->pointRef()[1].y();
        return true;
    }

    bool segmentIntersection(const LineSegment2D& a, const LineSegment2D& b, double& outX, double& outY)
    {
        const double x1 = a.x1, y1 = a.y1, x2 = a.x2, y2 = a.y2;
        const double x3 = b.x1, y3 = b.y1, x4 = b.x2, y4 = b.y2;
        const double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
        if (std::fabs(denom) < 1e-12)
        {
            return false;
        }
        const double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
        const double u = ((x1 - x3) * (y1 - y2) - (y1 - y3) * (x1 - x2)) / denom;
        if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9)
        {
            return false;
        }
        outX = x1 + t * (x2 - x1);
        outY = y1 + t * (y2 - y1);
        return true;
    }

    bool resolveTargetAndBoundary(SceneEditService* editService,
        const TransformParameters& params,
        Eg::SyEntity*& outTarget,
        Eg::SyEntity*& outBoundary)
    {
        outTarget = nullptr;
        outBoundary = nullptr;
        Eg::SceneManager* scene = editService ? editService->sceneManager() : nullptr;
        if (!scene)
        {
            return false;
        }

        const auto selected = scene->getSelectedEntities();
        if (params.trimTargetId != 0)
        {
            outTarget = scene->findEntityById(params.trimTargetId);
        }
        if (params.trimBoundaryId != 0)
        {
            outBoundary = scene->findEntityById(params.trimBoundaryId);
        }

        if (!outTarget || !outBoundary)
        {
            for (Eg::SyEntity* e : selected)
            {
                if (!e)
                {
                    continue;
                }
                if (!outTarget && e != outBoundary)
                {
                    outTarget = e;
                }
                else if (!outBoundary && e != outTarget)
                {
                    outBoundary = e;
                }
            }
        }
        return outTarget && outBoundary && outTarget != outBoundary;
    }

    int countSelectedBeziers(Eg::SceneManager* scene)
    {
        if (!scene)
        {
            return 0;
        }
        int count = 0;
        for (Eg::SyEntity* e : scene->getSelectedEntities())
        {
            if (e && (e->eType == Eg::EType::BEZIER || e->eType == Eg::EType::BEZIER2))
            {
                ++count;
            }
        }
        return count;
    }

    bool canMergeSelectedBeziers(Eg::SceneManager* scene)
    {
        if (!scene)
        {
            return false;
        }
        int cubicCount = 0;
        int quadCount = 0;
        for (Eg::SyEntity* e : scene->getSelectedEntities())
        {
            if (!e)
            {
                continue;
            }
            if (e->eType == Eg::EType::BEZIER)
            {
                ++cubicCount;
            }
            else if (e->eType == Eg::EType::BEZIER2)
            {
                ++quadCount;
            }
        }
        return cubicCount == 2 || quadCount == 2;
    }

    void collectBezierCandidates(Eg::SceneManager* scene,
        std::vector<Eg::SyEntity*>& outCubic,
        std::vector<Eg::SyEntity*>& outQuad)
    {
        if (!scene)
        {
            return;
        }
        const auto selected = scene->getSelectedEntities();
        const auto& source = selected.empty() ? scene->getAllEntities() : selected;
        for (Eg::SyEntity* e : source)
        {
            if (!e)
            {
                continue;
            }
            if (e->eType == Eg::EType::BEZIER2)
            {
                outQuad.push_back(e);
            }
            else if (e->eType == Eg::EType::BEZIER)
            {
                outCubic.push_back(e);
            }
        }
    }
}  // namespace

CoreOperationRegistry::CoreOperationRegistry(OperationBus* bus,
    SceneEditService* editService,
    IUndoRedoManager* undoManager,
    Eg::EntityClipboard* clipboard,
    AlgorithmRunner* algorithmRunner,
    ViewportActionHub* viewportActionHub,
    UiStateCenter* stateCenter,
    LayerEditService* layerEditService,
    UnitManager* unitManager,
    QWidget* parentWidget)
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
{
}

void CoreOperationRegistry::registerAll()
{
    if (!m_bus || !m_editService || !m_undoManager)
    {
        return;
    }

    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* undoManager = m_undoManager;

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Undo, [undoManager] {
        if (undoManager && undoManager->canUndo())
        {
            undoManager->undo();
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Redo, [undoManager] {
        if (undoManager && undoManager->canRedo())
        {
            undoManager->redo();
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Delete, [editService] {
        if (editService)
        {
            editService->deleteSelected("Delete");
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_SelectAll, [editService] {
        if (editService && editService->sceneManager())
        {
            editService->sceneManager()->selectAll();
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_ClearSelection, [editService] {
        if (editService && editService->sceneManager())
        {
            editService->sceneManager()->clearSelection();
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_InvertSelection, [editService] {
        if (editService && editService->sceneManager())
        {
            editService->sceneManager()->invertSelection();
        }
    }));

    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Nudge, [editService](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            const double dx = params.value(QStringLiteral("dx")).toDouble();
            const double dy = params.value(QStringLiteral("dy")).toDouble();
            if (dx == 0.0 && dy == 0.0)
            {
                return;
            }
            editService->nudgeSelected(dx, dy, "Nudge");
        }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Fillet, [editService](const QVariantMap& params) {
            double radius = params.value("radius", -1.0).toDouble();
            if (radius < 0.0)
            {
                auto* scene = editService->sceneManager();
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                {
                    return;
                }

                bool ok = false;
                radius = HelpDialogService::getDouble(
                    nullptr, QObject::tr("Fillet Radius"), QObject::tr("Radius:"), 5.0, 0.1, 10000.0, 2, &ok);

                if (!ok || radius < 0.1)
                {
                    return;
                }
            }
            Eg::FilletChamfer::applyFillet(*editService, radius);
        }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Chamfer, [editService](const QVariantMap& params) {
            double distance = params.value("distance", -1.0).toDouble();
            if (distance < 0.0)
            {
                auto* scene = editService->sceneManager();
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                {
                    return;
                }
                bool ok = false;
                distance = HelpDialogService::getDouble(
                    nullptr, QObject::tr("Chamfer Distance"), QObject::tr("Distance:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || distance < 0.1)
                {
                    return;
                }
            }
            Eg::FilletChamfer::applyChamfer(*editService, distance);
        }));

    registerEditOperations();
    registerAlgorithmOperations();
    registerViewOperations();
    registerHelpOperations();
}

void CoreOperationRegistry::registerHelpOperations()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();
    QWidget* parentWidget = m_parentWidget;

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_About, [parentWidget] {
        HelpDialogService::showAboutDialog(parentWidget);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Settings, [parentWidget] {
        auto* window = qobject_cast<WorkbenchWindow*>(parentWidget);
        UiWorkbench* activeWb = window ? window->currentWorkbench() : nullptr;
        if (activeWb)
        {
            activeWb->showSettingsDialog(parentWidget);
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Docs, [parentWidget] {
        HelpDialogService::showDocumentationDialog(parentWidget);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Shortcut, [parentWidget] {
        HelpDialogService::showShortcutsDialog(parentWidget);
    }));
}

void CoreOperationRegistry::registerEditOperations()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* clipboard = m_clipboard;
    QWidget* parentWidget = m_parentWidget;

    // ---- 剪贴板 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Copy, [editService, clipboard] {
        if (!editService || !clipboard)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
        {
            return;
        }
        std::vector<const Eg::SyEntity*> sources;
        sources.reserve(selected.size());
        for (Eg::SyEntity* e : selected)
        {
            if (e)
            {
                sources.push_back(e);
            }
        }
        clipboard->copy(sources);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Cut, [editService, clipboard] {
        if (!editService || !clipboard)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
        {
            return;
        }
        std::vector<const Eg::SyEntity*> sources;
        sources.reserve(selected.size());
        for (Eg::SyEntity* e : selected)
        {
            if (e)
            {
                sources.push_back(e);
            }
        }
        clipboard->copy(sources);
        editService->deleteSelected("Cut");
    }));

    // 粘贴锚点：鼠标在视口内取鼠标世界坐标，否则取视口中心；
    // 无有效锚点时回退默认设计台面中心 (600,400)
    auto pasteAnchor = [viewportHub = m_viewportActionHub]() -> Ut::Vec2d {
        Ut::Vec2d anchor(0, 0);
        if (viewportHub)
        {
            if (auto* vp = viewportHub->viewport())
            {
                const QPointF world = vp->pasteAnchorWorld();
                anchor = Ut::Vec2d(world.x(), world.y());
            }
        }
        if (anchor.x() == 0.0 && anchor.y() == 0.0)
        {
            anchor = Ut::Vec2d(600.0, 400.0);
        }
        return anchor;
    };

    // 将剪贴板纯文本按最近字体设置转矢量并居中粘贴（成功则选中新文字）
    auto pasteText = [editService, pasteAnchor](bool selectResult) {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        const Ut::Vec2d anchor = pasteAnchor();
        QString err;
        Eg::SyGroup* group = TextPasteService::pasteClipboardText(scene, anchor, err);
        if (!group)
        {
            if (!err.isEmpty())
            {
                SY_WARNF("[PasteText] %s", err.toStdString().c_str());
            }
            return;
        }
        if (selectResult)
        {
            scene->clearSelection();
            std::vector<Eg::SyEntity*> leaves = group->flatten();
            if (!leaves.empty())
            {
                scene->selectEntities(leaves);
            }
        }
    };

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Paste,
        [editService, clipboard, pasteAnchor, pasteText] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();

        // 图元剪贴板有内容 → 粘贴图元；否则若系统剪贴板有纯文本 → 粘贴为矢量文字
        if (clipboard && clipboard->hasContent())
        {
            const Ut::Vec2d pastePos = pasteAnchor();
            auto pasted = clipboard->paste(pastePos);
            if (pasted.empty())
            {
                return;
            }

            std::vector<Eg::EntityId> pastedIds;
            pastedIds.reserve(pasted.size());
            for (const auto& e : pasted)
            {
                if (e)
                {
                    pastedIds.push_back(e->id);
                }
            }

            scene->clearSelection();
            editService->addEntities(std::move(pasted), "Paste");

            Eg::VecSyEntityPtr pastedEntities;
            pastedEntities.reserve(pastedIds.size());
            for (Eg::EntityId id : pastedIds)
            {
                if (auto* ent = scene->findEntityById(id))
                {
                    pastedEntities.push_back(ent);
                }
            }
            if (!pastedEntities.empty())
            {
                scene->selectEntities(pastedEntities);
            }
            return;
        }

        // 回退：外部复制的文字 → 矢量文字
        pasteText(true);
    }));

    // 显式"粘贴为文字"：无论图元剪贴板是否有内容，都把系统剪贴板纯文本转为矢量
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_PasteText,
        [pasteText] { pasteText(true); }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Duplicate, [editService, parentWidget](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return;
            }

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
                tp = collectDialogParams(TransformType::Copy, parentWidget);
            }
            if (tp.type != TransformType::Copy)
            {
                return;
            }

            const int copyCount = tp.copyCount >= 1 ? tp.copyCount : 2;
            const double dx = tp.moveX;
            const double dy = tp.moveY;

            std::vector<const Eg::SyEntity*> sources;
            sources.reserve(selected.size());
            for (Eg::SyEntity* e : selected)
            {
                if (e)
                {
                    sources.push_back(e);
                }
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
                    {
                        continue;
                    }
                    snap->transform(Ut::Mat3d::translate(offX, offY));
                    snap->id = 0;
                    snap->setModified();
                    added.emplace_back(std::move(snap));
                }
            }

            if (!added.empty())
            {
                editService->addEntities(std::move(added), "Duplicate");
            }
        }));

    // ---- 变换 ----
    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Move, [editService, parentWidget](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return;
            }

            TransformParameters tp;
            if (params.contains(QStringLiteral("moveX")))
            {
                tp.type = TransformType::Move;
                tp.moveX = params.value(QStringLiteral("moveX")).toDouble();
                tp.moveY = params.value(QStringLiteral("moveY")).toDouble();
            }
            else
            {
                tp = collectDialogParams(TransformType::Move, parentWidget);
            }
            if (tp.type != TransformType::Move)
            {
                return;
            }

            const auto ids = collectIds(selected);
            editService->transformEntities(
                ids,
                [&]() {
                    EntityTransform transform(scene);
                    transform.moveByIds(ids, tp.moveX, tp.moveY);
                },
                "Move",
                false);
        }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Rotate, [editService, parentWidget](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return;
            }

            const auto ids = collectIds(selected);

            double angleRad = 0.0;
            double centerX = 0.0;
            double centerY = 0.0;

            if (params.contains(QStringLiteral("angle")))
            {
                const double angleDeg = params.value(QStringLiteral("angle")).toDouble();
                angleRad = Ut::GeomMath::degToRad(-angleDeg);
                double minX, minY, maxX, maxY;
                if (calcCombinedBounds(selected, minX, minY, maxX, maxY))
                {
                    centerX = (minX + maxX) * 0.5;
                    centerY = (minY + maxY) * 0.5;
                }
            }
            else
            {
                const TransformParameters tp = collectDialogParams(TransformType::Rotate, parentWidget);
                if (tp.type != TransformType::Rotate || tp.rotateAngle == 0.0)
                {
                    return;
                }
                angleRad = Ut::GeomMath::degToRad(tp.rotateAngle);
                if (tp.hasAnchor)
                {
                    centerX = tp.anchorX;
                    centerY = tp.anchorY;
                }
                else
                {
                    double minX, minY, maxX, maxY;
                    if (calcCombinedBounds(selected, minX, minY, maxX, maxY))
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

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Mirror, [editService, parentWidget](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return;
            }

            const auto ids = collectIds(selected);

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
                const TransformParameters tp = collectDialogParams(TransformType::Mirror, parentWidget);
                if (tp.type != TransformType::Mirror)
                {
                    return;
                }
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
                    {
                        transform.mirrorByLineIds(ids, lineX1, lineY1, lineX2, lineY2);
                    }
                    else
                    {
                        transform.mirrorByIds(ids, axis, centerX, centerY);
                    }
                },
                "Mirror",
                false);
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_MirrorH, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
        {
            return;
        }
        const auto ids = collectIds(selected);
        double minX, minY, maxX, maxY;
        const double axisX = calcCombinedBounds(selected, minX, minY, maxX, maxY) ? (minX + maxX) * 0.5 : 0.0;
        editService->transformEntities(
            ids,
            [&]() {
                EntityTransform transform(scene);
                transform.mirrorByIds(ids, 0, axisX, 0.0);
            },
            "Mirror Horizontal",
            false);
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_MirrorV, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        auto selected = scene->getSelectedEntities();
        if (selected.empty())
        {
            return;
        }
        const auto ids = collectIds(selected);
        double minX, minY, maxX, maxY;
        const double axisY = calcCombinedBounds(selected, minX, minY, maxX, maxY) ? (minY + maxY) * 0.5 : 0.0;
        editService->transformEntities(
            ids,
            [&]() {
                EntityTransform transform(scene);
                transform.mirrorByIds(ids, 1, 0.0, axisY);
            },
            "Mirror Vertical",
            false);
    }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Align, [editService](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();
            auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return;
            }
            const int mode = params.value(QStringLiteral("mode"), 0).toInt();
            const auto ids = collectIds(selected);
            editService->transformEntities(
                ids,
                [&]() {
                    EntityTransform transform(scene);
                    transform.alignByIds(ids, toEntityAlign(mode));
                },
                "Align",
                false);
        }));

    // ---- 群组 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_GroupToggle, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
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
        {
            scene->ungroupSelected();
        }
        else
        {
            auto selected = scene->getSelectedEntities();
            if (selected.size() >= 2)
            {
                scene->createGroup(selected, "Group");
            }
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Ungroup, [editService] {
        if (editService)
        {
            editService->sceneManager()->ungroupSelected();
        }
    }));

    // ---- 修剪 / 延伸 ----
    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Trim, [editService](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();

            TransformParameters tp = TransformParameters::createTrim(
                params.value(QStringLiteral("targetId"), 0).toULongLong(),
                params.value(QStringLiteral("boundaryId"), 0).toULongLong());

            Eg::SyEntity* target = nullptr;
            Eg::SyEntity* boundary = nullptr;
            if (!resolveTargetAndBoundary(editService, tp, target, boundary))
            {
                return;
            }

            LineSegment2D targetSeg, boundarySeg;
            if (!extractLineSegment(target, targetSeg) || !extractLineSegment(boundary, boundarySeg))
            {
                return;
            }

            double hitX = 0.0, hitY = 0.0;
            if (!segmentIntersection(targetSeg, boundarySeg, hitX, hitY))
            {
                return;
            }

            const double d1 = (hitX - targetSeg.x1) * (hitX - targetSeg.x1) + (hitY - targetSeg.y1) * (hitY - targetSeg.y1);
            const double d2 = (hitX - targetSeg.x2) * (hitX - targetSeg.x2) + (hitY - targetSeg.y2) * (hitY - targetSeg.y2);
            const double newX1 = (d1 <= d2) ? hitX : targetSeg.x1;
            const double newY1 = (d1 <= d2) ? hitY : targetSeg.y1;
            const double newX2 = (d1 <= d2) ? targetSeg.x2 : hitX;
            const double newY2 = (d1 <= d2) ? targetSeg.y2 : hitY;

            const double newLenSq = (newX2 - newX1) * (newX2 - newX1) + (newY2 - newY1) * (newY2 - newY1);
            if (newLenSq < 1e-12)
            {
                return;
            }

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
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Extend, [editService](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }

            TransformParameters tp = TransformParameters::createExtend(
                params.value(QStringLiteral("targetId"), 0).toULongLong(),
                params.value(QStringLiteral("boundaryId"), 0).toULongLong());

            Eg::SyEntity* target = nullptr;
            Eg::SyEntity* boundary = nullptr;
            if (!resolveTargetAndBoundary(editService, tp, target, boundary))
            {
                return;
            }

            LineSegment2D targetSeg, boundarySeg;
            if (!extractLineSegment(target, targetSeg) || !extractLineSegment(boundary, boundarySeg))
            {
                return;
            }

            const double tx = targetSeg.x2 - targetSeg.x1;
            const double ty = targetSeg.y2 - targetSeg.y1;
            const double tlen = std::sqrt(tx * tx + ty * ty);
            if (tlen < 1e-12)
            {
                return;
            }
            const double ux = tx / tlen, uy = ty / tlen;
            const double ext = tlen * 100.0;

            LineSegment2D rayFromP1;
            rayFromP1.x1 = targetSeg.x1 - ux * ext;
            rayFromP1.y1 = targetSeg.y1 - uy * ext;
            rayFromP1.x2 = targetSeg.x1;
            rayFromP1.y2 = targetSeg.y1;

            LineSegment2D rayFromP2;
            rayFromP2.x1 = targetSeg.x2;
            rayFromP2.y1 = targetSeg.y2;
            rayFromP2.x2 = targetSeg.x2 + ux * ext;
            rayFromP2.y2 = targetSeg.y2 + uy * ext;

            double hitX1 = 0.0, hitY1 = 0.0, hitX2 = 0.0, hitY2 = 0.0;
            const bool ok1 = segmentIntersection(rayFromP1, boundarySeg, hitX1, hitY1);
            const bool ok2 = segmentIntersection(rayFromP2, boundarySeg, hitX2, hitY2);

            if (!ok1 && !ok2)
            {
                return;
            }

            const double d1 =
                (hitX1 - targetSeg.x1) * (hitX1 - targetSeg.x1) + (hitY1 - targetSeg.y1) * (hitY1 - targetSeg.y1);
            const double d2 =
                (hitX2 - targetSeg.x2) * (hitX2 - targetSeg.x2) + (hitY2 - targetSeg.y2) * (hitY2 - targetSeg.y2);

            double newX1 = targetSeg.x1, newY1 = targetSeg.y1;
            double newX2 = targetSeg.x2, newY2 = targetSeg.y2;
            if (ok1 && (!ok2 || d1 >= d2))
            {
                newX1 = hitX1;
                newY1 = hitY1;
            }
            else if (ok2)
            {
                newX2 = hitX2;
                newY2 = hitY2;
            }

            const double newLenSq = (newX2 - newX1) * (newX2 - newX1) + (newY2 - newY1) * (newY2 - newY1);
            if (newLenSq <= tlen * tlen)
            {
                return;
            }

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

    // ---- 包围盒 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_GetBbox, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        Eg::SyEntity* entity = scene->getSelectedEntity();
        if (!entity)
        {
            return;
        }
        const Ut::BBox2d bbox = entity->getBbox();
        SY_INFOF("[GetBbox] entity=%llu bbox=(%.3f, %.3f)-(%.3f, %.3f)",
            static_cast<unsigned long long>(entity->id),
            bbox.minPt.x(),
            bbox.minPt.y(),
            bbox.maxPt.x(),
            bbox.maxPt.y());
    }));

    // ---- 离散化 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Discretize, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        Eg::SyEntity* entity = scene->getSelectedEntity();
        if (!entity)
        {
            auto all = scene->getAllEntities();
            if (all.empty())
            {
                return;
            }
            entity = all.front();
        }

        Eg::EntityDiscretizer discretizer;
        std::vector<Ut::Vec2dVector> vOutLines;
        if (!discretizer.doDiscretize(entity, vOutLines) || vOutLines.empty())
        {
            return;
        }

        SceneChangeSet changeSet;
        changeSet.toRemove.push_back(entity->id);
        for (const auto& line : vOutLines)
        {
            if (line.size() < 2)
            {
                continue;
            }
            changeSet.toAdd.push_back(std::make_unique<Eg::SyLine>(line));
        }
        if (!changeSet.toAdd.empty())
        {
            editService->applyChangeSet(std::move(changeSet), "Discretize");
        }
    }));

    // ---- 贝塞尔拆分/合并 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_BezierToggle, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();
        const OperationId target =
            canMergeSelectedBeziers(scene) ? OperationId::Edit_MergeBezier : OperationId::Edit_SplitBezier;
        if (OperationBus* bus = OperationBus::instance())
        {
            bus->run(target);
        }
    }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_SplitBezier, [editService](const QVariantMap& params) {
            if (!editService)
            {
                return;
            }
            auto* scene = editService->sceneManager();
            const double dT = params.value(QStringLiteral("t"), 0.5).toDouble();
            if (dT <= 0.0 || dT >= 1.0)
            {
                return;
            }

            const auto selected = scene->getSelectedEntities();
            if (selected.empty())
            {
                return;
            }

            SceneChangeSet changeSet;
            int nSplit = 0;
            for (Eg::SyEntity* e : selected)
            {
                if (!e || e->eType != Eg::EType::BEZIER)
                {
                    continue;
                }
                auto* bezier = dynamic_cast<Eg::SyBezier*>(e);
                if (!bezier)
                {
                    continue;
                }
                auto pair = Eg::BezierAlgorithms::splitBezier(bezier, dT);
                changeSet.toRemove.push_back(e->id);
                changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier>(pair.first));
                changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier>(pair.second));
                ++nSplit;
            }

            if (nSplit > 0)
            {
                editService->applyChangeSet(std::move(changeSet), "Split Bezier");
            }
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_MergeBezier, [editService] {
        if (!editService)
        {
            return;
        }
        auto* scene = editService->sceneManager();

        std::vector<Eg::SyEntity*> vCubic;
        std::vector<Eg::SyEntity*> vQuad;
        collectBezierCandidates(scene, vCubic, vQuad);

        SceneChangeSet changeSet;
        if (vCubic.size() == 2)
        {
            auto* b1 = dynamic_cast<Eg::SyBezier*>(vCubic[0]);
            auto* b2 = dynamic_cast<Eg::SyBezier*>(vCubic[1]);
            if (b1 && b2)
            {
                auto merged = Eg::BezierAlgorithms::mergeBeziers(b1, b2);
                if (merged)
                {
                    changeSet.toRemove.push_back(vCubic[0]->id);
                    changeSet.toRemove.push_back(vCubic[1]->id);
                    changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier>(*merged));
                }
            }
        }
        else if (vQuad.size() == 2)
        {
            auto* b1 = dynamic_cast<Eg::SyBezier2*>(vQuad[0]);
            auto* b2 = dynamic_cast<Eg::SyBezier2*>(vQuad[1]);
            if (b1 && b2)
            {
                auto merged = Eg::BezierAlgorithms::mergeBeziers(b1, b2);
                if (merged)
                {
                    changeSet.toRemove.push_back(vQuad[0]->id);
                    changeSet.toRemove.push_back(vQuad[1]->id);
                    changeSet.toAdd.push_back(std::make_unique<Eg::SyBezier2>(*merged));
                }
            }
        }

        if (!changeSet.empty())
        {
            editService->applyChangeSet(std::move(changeSet), "Merge Bezier");
        }
    }));

    // ---- 阵列（复用 Algo_Array 的对话框与处理器） ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Array, [runner = m_algorithmRunner] {
        if (!runner)
        {
            return;
        }
        OperationRequest req;
        req.id = OperationId::Algo_Array;
        req.source = OperationSource::Menu;
        runner->runForOperation(OperationId::Algo_Array, req);
    }));
}

void CoreOperationRegistry::registerAlgorithmOperations()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();
    AlgorithmRunner* runner = m_algorithmRunner;

    const auto registerAlgoOp = [&reg, runner](OperationId id) {
        reg.registerOperation(std::make_unique<ParamLambdaOperation>(id, [runner, id](const QVariantMap& params) {
            if (!runner)
            {
                return;
            }
            OperationRequest req;
            req.id = id;
            req.params = params;
            req.source = OperationSource::Menu;
            runner->runForOperation(id, req);
        }));
    };

    registerAlgoOp(OperationId::Algo_Fill);
#ifdef ENABLE_NESTING
    registerAlgoOp(OperationId::Algo_Nesting);
#endif
    registerAlgoOp(OperationId::Algo_Offset);
    registerAlgoOp(OperationId::Algo_Array);
    registerAlgoOp(OperationId::Algo_BooleanUnion);
    registerAlgoOp(OperationId::Algo_BooleanIntersection);
    registerAlgoOp(OperationId::Algo_BooleanDifference);
    registerAlgoOp(OperationId::Algo_BooleanXor);

    // ---- 位图浮雕 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Algo_ReliefEngravingFromImage, [parentWidget = m_parentWidget] {
            ReliefEngravingOperation2D::run(parentWidget);
        }));
}

void CoreOperationRegistry::registerViewOperations()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();
    ViewportActionHub* hub = m_viewportActionHub;
    UiStateCenter* stateCenter = m_stateCenter;
    LayerEditService* layerEditService = m_layerEditService;
    UnitManager* unitManager = m_unitManager;
    QWidget* parentWidget = m_parentWidget;

    const auto registerViewOp = [&reg, hub](OperationId id, const QString& action) {
        reg.registerOperation(std::make_unique<LambdaOperation>(id, [hub, action] {
            if (hub)
            {
                hub->handle(action);
            }
        }));
    };

    registerViewOp(OperationId::View_ZoomFit, QStringLiteral("zoom_fit"));
    registerViewOp(OperationId::View_ZoomIn, QStringLiteral("zoom_in"));
    registerViewOp(OperationId::View_ZoomOut, QStringLiteral("zoom_out"));
    registerViewOp(OperationId::View_ZoomSelection, QStringLiteral("zoom_selection"));
    registerViewOp(OperationId::View_Pan, QStringLiteral("pan"));
    registerViewOp(OperationId::View_Reset, QStringLiteral("reset"));

    // ---- 视图开关（网格/捕捉/正交/角度捕捉 → 状态中心元数据） ----
    const auto toggleMetadata = [stateCenter](const char* key) {
        if (!stateCenter)
        {
            return;
        }
        QVariantMap meta = stateCenter->metadata();
        const QString keyStr = QLatin1String(key);
        meta[keyStr] = !meta.value(keyStr).toBool();
        stateCenter->setMetadata(meta);
    };

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_GridVisible, [toggleMetadata] { toggleMetadata("gridVisible"); }));
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_SnapEnabled, [toggleMetadata] { toggleMetadata("snapEnabled"); }));
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_OrthoMode, [toggleMetadata] { toggleMetadata("orthoMode"); }));
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_AngleSnap, [toggleMetadata] { toggleMetadata("angleSnap"); }));

    // ---- 图层 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_LayerManager, [layerEditService, parentWidget] {
            if (layerEditService)
            {
                LayerManagerDialog::showDialog(layerEditService, parentWidget);
            }
        }));

    // ---- 显示单位 ----
    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::View_SetDisplayUnit, [unitManager](const QVariantMap& params) {
            if (!unitManager)
            {
                return;
            }
            const int unit = params.value(
                QStringLiteral("unit"), static_cast<int>(UnitManager::Unit::Millimeter)).toInt();
            unitManager->setDisplayUnit(static_cast<UnitManager::Unit>(unit));
        }));
}
