#include "ViewportSelector.h"

#include "Camera2D.h"
#include "ISelectionService.h"
#include "RenderWidget.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Geometry/Geo2DQuery.h"
#include "Engine2D/Geo/GeometryContext.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/EntityIdUtils.h"

#include <cmath>
#include <limits>

namespace
{
    constexpr double kHitTolerancePx = 8.0;
}  // namespace

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
    {
        return false;
    }

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
    {
        return;
    }

    Render::BBox2d bbox(worldPos.x(), worldPos.y(), worldPos.x(), worldPos.y());
    m_renderWidget->setSelectionBox(&bbox, QColor(204, 102, 0, 200));
}

void ViewportSelector::updateBoxSelect(const QPointF& worldPos)
{
    m_boxSelectEnd = worldPos;

    if (!m_renderWidget)
    {
        return;
    }

    Render::BBox2d bbox(m_boxSelectStart.x(), m_boxSelectStart.y(), worldPos.x(), worldPos.y());
    m_renderWidget->setSelectionBox(&bbox, QColor(204, 102, 0, 200));
}

size_t ViewportSelector::endBoxSelect(const QPointF& worldPos)
{
    m_boxSelecting = false;

    if (m_renderWidget)
    {
        m_renderWidget->setSelectionBox(nullptr, QColor());
    }

    if (!m_camera || !m_sceneManager || !m_selectionService)
    {
        return 0;
    }

    double dx = std::abs(worldPos.x() - m_boxSelectStart.x());
    double dy = std::abs(worldPos.y() - m_boxSelectStart.y());
    double tol = kHitTolerancePx / m_camera->zoomX;

    if (dx < tol && dy < tol)
    {
        performHitTest(worldPos);
        return 0;
    }

    if (!m_sceneManager || !m_selectionService)
    {
        return 0;
    }

    double minX = std::min(m_boxSelectStart.x(), worldPos.x());
    double maxX = std::max(m_boxSelectStart.x(), worldPos.x());
    double minY = std::min(m_boxSelectStart.y(), worldPos.y());
    double maxY = std::max(m_boxSelectStart.y(), worldPos.y());

    std::vector<std::string> hitIds;

    // 框选判定收口到空间索引查询：用 queryByBox 替代全量遍历 getAllEntities()，
    // 候选图元的包围盒已缓存（SceneManager::queryByBox 复用索引缓存），不再逐图元重算。
    Ut::BBox2d box(Ut::Vec2d(minX, minY), Ut::Vec2d(maxX, maxY));

    const auto candidates = m_sceneManager->queryByBox(box, /*containedOnly=*/false);
    for (Eg::SyEntity* entity : candidates)
    {
        if (entity)
        {
            hitIds.push_back(std::to_string(entity->id));
        }
    }

    m_selectionService->clear();
    if (!hitIds.empty())
    {
        // P2 ABI 收口: const char* const* 替代 std::vector<std::string>
        std::vector<const char*> idPtrs;
        idPtrs.reserve(hitIds.size());
        for (const auto& id : hitIds)
        {
            idPtrs.push_back(id.c_str());
        }
        m_selectionService->selectMultiple(idPtrs.data(), idPtrs.size());
        if (m_statusCallback)
        {
            m_statusCallback(QStringLiteral("2D: %1 entity(ies) selected").arg(static_cast<int>(hitIds.size())));
        }
        if (m_selectionCallback)
        {
            m_selectionCallback(QStringLiteral("2D-Select"),
                QStringLiteral("2D: %1 entity(ies) selected").arg(static_cast<int>(hitIds.size())));
        }
    }
    else
    {
        if (m_statusCallback)
        {
            m_statusCallback(QStringLiteral("2D selection cleared"));
        }
        if (m_selectionCallback)
        {
            m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("none"));
        }
    }

    return hitIds.size();
}

// ==================== 选择查询/管理（P5 从 RenderViewport2D 下沉） ====================

QString ViewportSelector::selectedEntityId() const
{
    if (!m_selectionService)
    {
        return {};
    }

    QString firstId;
    m_selectionService->visitSelectedIds(
        [](const char* id, void* ctx) {
            auto* out = static_cast<QString*>(ctx);
            if (out->isEmpty())
            {
                *out = QString::fromUtf8(id);
            }
        },
        &firstId);
    return firstId;
}

void ViewportSelector::selectEntityById(const QString& entityId)
{
    if (!m_selectionService)
    {
        return;
    }

    m_selectionService->clear();
    auto utf8 = entityId.toUtf8();
    m_selectionService->select(utf8.constData());

    if (m_statusCallback)
    {
        m_statusCallback(QStringLiteral("2D entity selected"));
    }
    if (m_selectionCallback)
    {
        m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("2D entity: %1").arg(entityId));
    }
}

void ViewportSelector::clearSelection()
{
    if (m_selectionService)
    {
        m_selectionService->clear();
    }

    if (m_statusCallback)
    {
        m_statusCallback(QStringLiteral("2D selection cleared"));
    }
}

std::optional<Ut::BBox2d> ViewportSelector::selectionBBox() const
{
    if (!m_selectionService || !m_sceneManager)
    {
        return std::nullopt;
    }

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
            {
                return;
            }
            auto* bc = static_cast<BBoxContext*>(context);
            // ID 字符串 -> EntityId -> SyEntity*
            auto eid = Eg::parseEntityId(std::string(id));
            if (!eid)
            {
                return;
            }
            Eg::SyEntity* entity = bc->sceneManager->findEntityById(*eid);
            if (!entity)
            {
                return;
            }
            Ut::BBox2d bbox = entity->getBbox();
            if (bbox.isValid())
            {
                bc->combined.expand(bbox);
                bc->hasEntity = true;
            }
        },
        &ctx);

    if (!ctx.hasEntity || !ctx.combined.isValid())
    {
        return std::nullopt;
    }

    return ctx.combined;
}

void ViewportSelector::performHitTest(const QPointF& worldPos)
{
    if (!m_sceneManager || !m_selectionService)
    {
        return;
    }

    // 拾取容差：屏幕像素换算到世界坐标半径
    Eg::GeometryContext ctx;
    ctx.dPick = kHitTolerancePx / m_camera->zoomX;

    std::string hitId;
    double minDist = ctx.dPick;

    // 先经空间索引 queryByPoint 缩小候选范围，再对候选做精确距离判定，
    // 避免对全场图元逐个 distanceToPoint（O(N) 全量扫描）。
    const Ut::Vec2d pt(worldPos.x(), worldPos.y());
    const auto candidates = m_sceneManager->queryByPoint(pt, ctx.dPick);
    for (Eg::SyEntity* entity : candidates)
    {
        if (!entity)
        {
            continue;
        }

        // 命中判定收口到引擎 Geo2DQuery：统一处理线段/折线/圆/弧/椭圆/贝塞尔/样条/复合线
        const double dist = Eg::Geo2DQuery::distanceToPoint(entity, pt, ctx);

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
        {
            m_statusCallback(QStringLiteral("2D entity selected"));
        }
        if (m_selectionCallback)
        {
            m_selectionCallback(
                QStringLiteral("2D-Select"), QStringLiteral("2D entity: %1").arg(QString::fromStdString(hitId)));
        }
    }
    else
    {
        if (m_statusCallback)
        {
            m_statusCallback(QStringLiteral("2D selection cleared"));
        }
        if (m_selectionCallback)
        {
            m_selectionCallback(QStringLiteral("2D-Select"), QStringLiteral("none"));
        }
    }
}