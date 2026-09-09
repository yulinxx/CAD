#pragma once

#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "UI2D/Operation/OperationBus.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/EntityClipboard.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/IUndoRedoManager.h"
#include "Engine2D/Algorithm/EntityTransform.h"
#include "Engine2D/Geometry/BezierAlgorithms.h"
#include "Engine2D/Core/SceneChangeSet.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"

#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/UiStateCenter.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "UI2D/Manager/UnitManager.h"
#include "UI/Adapters/TransformDialogAdapter.h"
#include "UI/TransformParameters.h"
#include "Ut/BBox2d.h"
#include "Ut/GeomMath.h"
#include "Ut/Mat.h"
#include "Ut/Vec.h"

#include <QWidget>
#include <functional>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Eg
{
    class SceneManager;
    class SyEntity;
    class EntityClipboard;
}

class SceneEditService;
class IUndoRedoManager;
class AlgorithmRunner;
class ViewportActionHub;
class UiStateCenter;
class LayerEditService;
class UnitManager;
class OperationBus;
class QWidget;

// ==================== Lambda 操作类型 ====================

class SceneMutatingLambdaOperation : public LambdaOperation
{
public:
    using Fn = std::function<void()>;
    using CanExecFn = std::function<bool()>;

    SceneMutatingLambdaOperation(
        OperationId id,
        Fn fn,
        CanExecFn canExec = [] { return true; })
        : LambdaOperation(id, std::move(fn), std::move(canExec))
    {
    }

    bool mutatesScene() const override { return true; }
    bool isUndoable() const override { return true; }
};

class ParamSceneMutatingLambdaOperation : public ParamLambdaOperation
{
public:
    using ParamFn = std::function<void(const QVariantMap&)>;
    using CanExecFn = std::function<bool()>;

    ParamSceneMutatingLambdaOperation(
        OperationId id,
        ParamFn fn,
        CanExecFn canExec = [] { return true; })
        : ParamLambdaOperation(id, std::move(fn), std::move(canExec))
    {
    }

    bool mutatesScene() const override { return true; }
    bool isUndoable() const override { return true; }
};

// ==================== 工具函数 ====================

namespace OpRegistryHelpers
{
    inline std::vector<Eg::EntityId> collectIds(const Eg::VecSyEntityPtr& selected)
    {
        std::vector<Eg::EntityId> ids;
        ids.reserve(selected.size());
        for (Eg::SyEntity* e : selected)
        {
            if (e)
                ids.push_back(e->id);
        }
        return ids;
    }

    inline bool calcCombinedBounds(
        const Eg::VecSyEntityPtr& entities, double& outMinX, double& outMinY, double& outMaxX, double& outMaxY)
    {
        if (entities.empty())
            return false;
        double minX = (std::numeric_limits<double>::max)();
        double minY = (std::numeric_limits<double>::max)();
        double maxX = -(std::numeric_limits<double>::max)();
        double maxY = -(std::numeric_limits<double>::max)();
        bool valid = false;
        for (Eg::SyEntity* e : entities)
        {
            if (!e)
                continue;
            const Ut::BBox2d bb = e->getBbox();
            if (!bb.isValid())
                continue;
            minX = (std::min)(minX, static_cast<double>(bb.minPt.x()));
            minY = (std::min)(minY, static_cast<double>(bb.minPt.y()));
            maxX = (std::max)(maxX, static_cast<double>(bb.maxPt.x()));
            maxY = (std::max)(maxY, static_cast<double>(bb.maxPt.y()));
            valid = true;
        }
        if (!valid)
            return false;
        outMinX = minX;
        outMinY = minY;
        outMaxX = maxX;
        outMaxY = maxY;
        return true;
    }

    inline EntityTransform::AlignMode toEntityAlign(int mode)
    {
        switch (mode)
        {
        case 0: return EntityTransform::AlignMode::Left;
        case 1: return EntityTransform::AlignMode::Right;
        case 2: return EntityTransform::AlignMode::Top;
        case 3: return EntityTransform::AlignMode::Bottom;
        case 4: return EntityTransform::AlignMode::CenterH;
        case 5: return EntityTransform::AlignMode::CenterV;
        default: return EntityTransform::AlignMode::Left;
        }
    }

    inline TransformParameters collectDialogParams(TransformType type, QWidget* parent)
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

    inline bool extractLineSegment(Eg::SyEntity* entity, LineSegment2D& outSeg)
    {
        if (!entity || entity->eType != Eg::EType::LINE)
            return false;
        auto* line = static_cast<Eg::SyLine*>(entity);
        if (line->pointRef().size() < 2)
            return false;
        outSeg.x1 = line->pointRef()[0].x();
        outSeg.y1 = line->pointRef()[0].y();
        outSeg.x2 = line->pointRef()[1].x();
        outSeg.y2 = line->pointRef()[1].y();
        return true;
    }

    inline bool segmentIntersection(const LineSegment2D& a, const LineSegment2D& b, double& outX, double& outY)
    {
        const double x1 = a.x1, y1 = a.y1, x2 = a.x2, y2 = a.y2;
        const double x3 = b.x1, y3 = b.y1, x4 = b.x2, y4 = b.y2;
        const double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
        if (std::fabs(denom) < 1e-12)
            return false;
        const double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
        const double u = ((x1 - x3) * (y1 - y2) - (y1 - y3) * (x1 - x2)) / denom;
        if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9)
            return false;
        outX = x1 + t * (x2 - x1);
        outY = y1 + t * (y2 - y1);
        return true;
    }

    inline bool resolveTargetAndBoundary(SceneEditService* editService,
        const TransformParameters& params,
        Eg::SyEntity*& outTarget,
        Eg::SyEntity*& outBoundary)
    {
        outTarget = nullptr;
        outBoundary = nullptr;
        Eg::SceneManager* scene = editService ? editService->sceneManager() : nullptr;
        if (!scene)
            return false;
        const auto selected = scene->getSelectedEntities();
        if (params.trimTargetId != 0)
            outTarget = scene->findEntityById(params.trimTargetId);
        if (params.trimBoundaryId != 0)
            outBoundary = scene->findEntityById(params.trimBoundaryId);
        if (!outTarget || !outBoundary)
        {
            for (Eg::SyEntity* e : selected)
            {
                if (!e)
                    continue;
                if (!outTarget && e != outBoundary)
                    outTarget = e;
                else if (!outBoundary && e != outTarget)
                    outBoundary = e;
            }
        }
        return outTarget && outBoundary && outTarget != outBoundary;
    }

    inline int countSelectedBeziers(Eg::SceneManager* scene)
    {
        if (!scene)
            return 0;
        int count = 0;
        for (Eg::SyEntity* e : scene->getSelectedEntities())
        {
            if (e && (e->eType == Eg::EType::BEZIER || e->eType == Eg::EType::BEZIER2))
                ++count;
        }
        return count;
    }

    inline bool canMergeSelectedBeziers(Eg::SceneManager* scene)
    {
        if (!scene)
            return false;
        int cubicCount = 0;
        int quadCount = 0;
        for (Eg::SyEntity* e : scene->getSelectedEntities())
        {
            if (!e)
                continue;
            if (e->eType == Eg::EType::BEZIER)
                ++cubicCount;
            else if (e->eType == Eg::EType::BEZIER2)
                ++quadCount;
        }
        return cubicCount == 2 || quadCount == 2;
    }

    inline void collectBezierCandidates(
        Eg::SceneManager* scene, std::vector<Eg::SyEntity*>& outCubic, std::vector<Eg::SyEntity*>& outQuad)
    {
        if (!scene)
            return;
        const auto selected = scene->getSelectedEntities();
        const auto& source = selected.empty() ? scene->getAllEntities() : selected;
        for (Eg::SyEntity* e : source)
        {
            if (!e)
                continue;
            if (e->eType == Eg::EType::BEZIER2)
                outQuad.push_back(e);
            else if (e->eType == Eg::EType::BEZIER)
                outCubic.push_back(e);
        }
    }
}
