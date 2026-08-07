#include "ViewportSelector.h"

#include "Camera2D.h"
#include "ISelectionService.h"
#include "RenderWidget.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/EntityIdUtils.h"

#include <cmath>
#include <limits>

namespace
{
    constexpr double kHitTolerancePx = 8.0;

    double distPointToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
    {
        double abx = b.x() - a.x();
        double aby = b.y() - a.y();
        double apx = p.x() - a.x();
        double apy = p.y() - a.y();

        double lenSq = abx * abx + aby * aby;
        if (lenSq < 1e-12)
            return std::hypot(apx, apy);

        double t = (apx * abx + apy * aby) / lenSq;
        t = std::max(0.0, std::min(1.0, t));

        double closestX = a.x() + t * abx;
        double closestY = a.y() + t * aby;

        return std::hypot(p.x() - closestX, p.y() - closestY);
    }
}

ViewportSelector::ViewportSelector(Eg::SceneManager* sceneManager,
    ISelectionService* selectionService,
    const Camera2D* camera,
    RenderWidget* renderWidget)
    : m_sceneManager(sceneManager)
    , m_selectionService(selectionService)
    , m_camera(camera)
    , m_renderWidget(renderWidget)
{
}

bool ViewportSelector::handleClick(const QPointF& worldPos)
{
    if (!m_sceneManager || !m_selectionService)
        return false;

    performHitTest(worldPos);
    return true;
}

void ViewportSelector::beginBoxSelect(const QPointF& worldPos)
{
    // 状态跟踪不依赖渲染控件：无 RenderWidget 时仅跳过预览绘制
    m_boxSelecting = true;
    m_boxSelectStart = worldPos;
    m_boxSelectEnd = worldPos;

    if (!m_renderWidget)
        return;

    Render::BBox2d bbox(
        worldPos.x(), worldPos.y(),
        worldPos.x(), worldPos.y());
    m_renderWidget->setSelectionBox(&bbox, QColor(204, 102, 0, 200));
}

void ViewportSelector::updateBoxSelect(const QPointF& worldPos)
{
    m_boxSelectEnd = worldPos;

    if (!m_renderWidget)
        return;

    Render::BBox2d bbox(
        m_boxSelectStart.x(), m_boxSelectStart.y(),
        worldPos.x(), worldPos.y());
    m_renderWidget->setSelectionBox(&bbox, QColor(204, 102, 0, 200));
}

size_t ViewportSelector::endBoxSelect(const QPointF& worldPos)
{
    m_boxSelecting = false;

    if (m_renderWidget)
        m_renderWidget->setSelectionBox(nullptr, QColor());

    if (!m_camera || !m_sceneManager || !m_selectionService)
        return 0;

    double dx = std::abs(worldPos.x() - m_boxSelectStart.x());
    double dy = std::abs(worldPos.y() - m_boxSelectStart.y());
    double tol = kHitTolerancePx / m_camera->zoomX;

    if (dx < tol && dy < tol)
    {
        performHitTest(worldPos);
        return 0;
    }

    if (!m_sceneManager || !m_selectionService)
        return 0;

    double minX = std::min(m_boxSelectStart.x(), worldPos.x());
    double maxX = std::max(m_boxSelectStart.x(), worldPos.x());
    double minY = std::min(m_boxSelectStart.y(), worldPos.y());
    double maxY = std::max(m_boxSelectStart.y(), worldPos.y());

    std::vector<std::string> hitIds;

    for (const auto* entity : m_sceneManager->getAllEntities())
    {
        if (!entity) continue;

        bool inside = false;

        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            for (const auto& pt : line->pointRef())
            {
                if (pt.x() >= minX && pt.x() <= maxX &&
                    pt.y() >= minY && pt.y() <= maxY)
                {
                    inside = true;
                    break;
                }
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE || entity->eType == Eg::EType::ARC)
        {
            const auto& c = entity->basePoint;
            if (c.x() >= minX && c.x() <= maxX &&
                c.y() >= minY && c.y() <= maxY)
            {
                inside = true;
            }
        }
        else if (entity->eType == Eg::EType::POLYGON)
        {
            auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
            for (const auto& v : polygon->vertices())
            {
                if (v.x() >= minX && v.x() <= maxX &&
                    v.y() >= minY && v.y() <= maxY)
                {
                    inside = true;
                    break;
                }
            }
        }

        if (inside)
            hitIds.push_back(std::to_string(entity->id));
    }

    m_selectionService->clear();
    if (!hitIds.empty())
    {
        // P2 ABI 收口: const char* const* 替代 std::vector<std::string>
        std::vector<const char*> idPtrs;
        idPtrs.reserve(hitIds.size());
        for (const auto& id : hitIds)
            idPtrs.push_back(id.c_str());
        m_selectionService->selectMultiple(idPtrs.data(), idPtrs.size());
        if (m_statusCallback)
            m_statusCallback(QStringLiteral("2D %1 entities selected").arg(static_cast<int>(hitIds.size())));
        if (m_selectionCallback)
            m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("2D %1 entities selected").arg(static_cast<int>(hitIds.size())));
    }
    else
    {
        if (m_statusCallback)
            m_statusCallback(QStringLiteral("2D selection cleared"));
        if (m_selectionCallback)
            m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("none"));
    }

    return hitIds.size();
}

// ==================== 选择查询/管理（P5 从 RenderViewport2D 下沉） ====================

QString ViewportSelector::selectedEntityId() const
{
    if (!m_selectionService)
        return {};

    QString firstId;
    m_selectionService->visitSelectedIds(
        [](const char* id, void* ctx) {
            auto* out = static_cast<QString*>(ctx);
            if (out->isEmpty())
                *out = QString::fromUtf8(id);
        },
        &firstId);
    return firstId;
}

void ViewportSelector::selectEntityById(const QString& entityId)
{
    if (!m_selectionService)
        return;

    m_selectionService->clear();
    auto utf8 = entityId.toUtf8();
    m_selectionService->select(utf8.constData());

    if (m_statusCallback)
        m_statusCallback(QStringLiteral("2D entity selected"));
    if (m_selectionCallback)
        m_selectionCallback(QStringLiteral("2D-Select"),
            QStringLiteral("2D entity: %1").arg(entityId));
}

void ViewportSelector::clearSelection()
{
    if (m_selectionService)
        m_selectionService->clear();

    if (m_statusCallback)
        m_statusCallback(QStringLiteral("2D selection cleared"));
}

std::optional<Ut::BBox2d> ViewportSelector::selectionBBox() const
{
    if (!m_selectionService || !m_sceneManager)
        return std::nullopt;

    // 通过 ID 遍历选中项，再用 SceneManager 查询实体指针合并 BBox
    // 这样 ISelectionService 保持纯 ID 接口，不泄漏 SyEntity*
    struct BBoxContext
    {
        Ut::BBox2d combined;
        bool hasEntity = false;
        Eg::SceneManager* sceneManager = nullptr;
    } ctx;
    ctx.sceneManager = m_sceneManager;

    m_selectionService->visitSelectedIds(
        [](const char* id, void* context) {
            if (!id)
                return;
            auto* bc = static_cast<BBoxContext*>(context);
            // ID 字符串 -> EntityId -> SyEntity*
            auto eid = Eg::parseEntityId(std::string(id));
            if (!eid)
                return;
            Eg::SyEntity* entity = bc->sceneManager->findEntityById(*eid);
            if (!entity)
                return;
            Ut::BBox2d bbox = entity->getBbox();
            if (bbox.isValid())
            {
                bc->combined.expand(bbox);
                bc->hasEntity = true;
            }
        },
        &ctx);

    if (!ctx.hasEntity || !ctx.combined.isValid())
        return std::nullopt;

    return ctx.combined;
}

void ViewportSelector::performHitTest(const QPointF& worldPos)
{
    if (!m_sceneManager || !m_selectionService)
        return;

    double tol = kHitTolerancePx / m_camera->zoomX;

    std::string hitId;
    double minDist = tol;

    for (const auto* entity : m_sceneManager->getAllEntities())
    {
        if (!entity) continue;

        double dist = std::numeric_limits<double>::max();

        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            if (line->pointRef().size() >= 2)
            {
                for (size_t i = 1; i < line->pointRef().size(); ++i)
                {
                    const auto& p0 = line->pointRef()[i - 1];
                    const auto& p1 = line->pointRef()[i];
                    double d = distPointToSegment(worldPos,
                        QPointF(p0.x(), p0.y()),
                        QPointF(p1.x(), p1.y()));
                    if (d < dist) dist = d;
                }
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<const Eg::SyCircle*>(entity);
            const auto& c = entity->basePoint;
            double d = std::abs(std::hypot(worldPos.x() - c.x(), worldPos.y() - c.y()) - circle->dRadius);
            if (d < dist) dist = d;
        }
        else if (entity->eType == Eg::EType::ARC)
        {
            auto* arc = static_cast<const Eg::SyArc*>(entity);
            const auto& c = entity->basePoint;
            double d = std::abs(std::hypot(worldPos.x() - c.x(), worldPos.y() - c.y()) - arc->dRadius);
            if (d < dist) dist = d;
        }

        if (dist < minDist)
        {
            minDist = dist;
            hitId = std::to_string(entity->id);
        }
    }

    m_selectionService->clear();
    if (!hitId.empty())
    {
        m_selectionService->select(hitId.c_str());  // P2 ABI 收口: const char* 替代 std::string
        if (m_statusCallback)
            m_statusCallback(QStringLiteral("2D entity selected"));
        if (m_selectionCallback)
            m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("2D entity: %1").arg(QString::fromStdString(hitId)));
    }
    else
    {
        if (m_statusCallback)
            m_statusCallback(QStringLiteral("2D selection cleared"));
        if (m_selectionCallback)
            m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("none"));
    }
}