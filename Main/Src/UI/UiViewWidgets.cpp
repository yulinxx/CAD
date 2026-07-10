#include "UiViewWidgets.h"

#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include "SceneDocument2D.h"
#include "UiCommandDispatcher.h"
#include "UiInteractionDispatcher.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/Core/SceneManager.h"
#include "UiSelectionTools.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

namespace
{
    constexpr double kGridStep = 50.0;
    constexpr double kHitRadius = 10.0;
    constexpr double kLineHitTolerance = 8.0;

    bool forwardActiveCommand(IInteractionDispatcher* dispatcher, QMouseEvent* event,
        bool (IInteractionDispatcher::*forwardFn)(int, int))
    {
        if (!dispatcher || !dispatcher->hasActiveCommand() || !event)
            return false;

        return (dispatcher->*forwardFn)(event->x(), event->y());
    }
}

Viewport2D::Viewport2D(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-5000, -5000, 10000, 10000);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::RubberBandDrag);
    ensureGrid();
    ensureAxes();
}

void Viewport2D::setStatusCallback(std::function<void(const QString&)>&& callback)
{
    m_statusCallback = std::move(callback);
}

void Viewport2D::setDocument(SceneDocument2D* document)
{
    m_document = document;
    refreshFromDocument();
}

void Viewport2D::setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback)
{
    m_selectionCallback = std::move(callback);
}

void Viewport2D::setCommandStageCallback(std::function<void(const QString&)>&& callback)
{
    m_commandStageCallback = std::move(callback);
}

void Viewport2D::setCommandDispatcher(UiCommandDispatcher* dispatcher)
{
    m_commandDispatcher = dispatcher;
}

void Viewport2D::setInteractionDispatcher(IInteractionDispatcher* dispatcher)
{
    m_interactionDispatcher = dispatcher;
}

// 设置操作总线引用
void Viewport2D::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

// 旧状态桥 — 后续迁移到 OperationBus 后删除
void Viewport2D::setDrawingEnabled(bool enabled)
{
    m_toolContext.drawing = enabled;
    m_toolContext.measureMode = false;
    m_toolContext.tool = enabled ? ToolContext::DrawTool::Line : ToolContext::DrawTool::None;
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle")); // 等待第一点 / 空闲
    updateStatus(enabled ? tr("2D draw mode") : tr("2D select mode")); // 2D 绘图模式 / 2D 选择模式
}

// 旧状态桥 — 后续迁移到 OperationBus 后删除
void Viewport2D::setMeasureMode(bool enabled)
{
    m_toolContext.measureMode = enabled;
    m_toolContext.drawing = false;
    m_toolContext.tool = ToolContext::DrawTool::None;
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle")); // 等待第一点 / 空闲
    updateStatus(enabled ? tr("2D measure mode") : tr("2D select mode")); // 2D 测量模式 / 2D 选择模式
}

void Viewport2D::resetView()
{
    resetTransform();
    centerOn(0, 0);
    updateStatus(tr("2D view reset")); // 2D 视图重置
}

void Viewport2D::ensureGrid()
{
    // 绘制网格线，便于捕捉和定位
    for (int i = -100; i <= 100; ++i)
    {
        const double x = i * kGridStep;
        m_scene->addLine(x, -5000, x, 5000, QPen(QColor(60, 60, 66), 0));
        const double y = i * kGridStep;
        m_scene->addLine(-5000, y, 5000, y, QPen(QColor(60, 60, 66), 0));
    }
}

void Viewport2D::ensureAxes()
{
    // 坐标轴帮助用户确认当前画布方向
    m_scene->addLine(-5000, 0, 5000, 0, QPen(QColor(220, 80, 80), 2));
    m_scene->addLine(0, -5000, 0, 5000, QPen(QColor(80, 220, 120), 2));
}

void Viewport2D::addPreviewLine(const QPointF& start, const QPointF& end)
{
    if (!m_previewLine)
    {
        m_previewLine = m_scene->addLine(QLineF(start, end), QPen(QColor(80, 180, 255), 2, Qt::DashLine));
        return;
    }
    m_previewLine->setLine(QLineF(start, end));
}

void Viewport2D::commitLine(const QPointF& start, const QPointF& end)
{
    m_scene->addLine(QLineF(start, end), QPen(QColor(255, 220, 120), 2));
    if (m_document)
    {
        auto id = m_document->createLine(start, end);
        m_document->clearSelection();
        m_document->selectEntity(id);
    }
    refreshSelectionStyle();
    if (m_operationBus)
        m_operationBus->run(OperationId::Tool_Line, {}, OperationSource::DrawTool);
}

void Viewport2D::commitPolylinePoint(const QPointF& pt)
{
    // 折线逐点输入，至少两点以后才会形成有效线段
    m_polylinePoints.push_back(pt);
    if (m_polylinePoints.size() >= 2)
    {
        const auto& a = m_polylinePoints[m_polylinePoints.size() - 2];
        const auto& b = m_polylinePoints[m_polylinePoints.size() - 1];
        m_scene->addLine(QLineF(a, b), QPen(QColor(255, 180, 100), 2));
    }
}

void Viewport2D::finishPolyline(const QPointF& pt)
{
    // 双击或右键收尾时调用，把当前折线真正写回文档
    if (!m_document)
        return;

    m_polylinePoints.push_back(pt);
    if (m_polylinePoints.size() < 2)
    {
        m_polylinePoints.clear();
        updateStatus(tr("2D polyline cancelled")); // 2D 折线取消
        return;
    }

    // TODO: use Eg::SyLine with multiple points when SceneDocument2D supports createPolyline
    auto id = m_document->createLine(m_polylinePoints.front(), m_polylinePoints.back());
    m_document->clearSelection();
    m_document->selectEntity(id);
    m_polylinePoints.clear();
    refreshSelectionStyle();

    if (m_selectionCallback)
        m_selectionCallback(tr("2D-Commit"), tr("polyline")); // 2D 提交 / 折线

    updateStatus(tr("2D polyline committed")); // 2D 折线已提交
    // 通知 OperationBus 实体已创建（过渡期兼容）
    if (m_operationBus)
        m_operationBus->run(OperationId::Tool_Polyline, {}, OperationSource::DrawTool);
}

void Viewport2D::commitCircle(const QPointF& center, double radius)
{
    m_scene->addEllipse(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0), QPen(QColor(255, 220, 120), 2));
    if (m_document)
    {
        auto id = m_document->createCircle(center, radius);
        m_document->clearSelection();
        m_document->selectEntity(id);
    }
    refreshSelectionStyle();
    if (m_operationBus)
        m_operationBus->run(OperationId::Tool_Circle, {}, OperationSource::DrawTool);
}

void Viewport2D::commitArc(const QPointF& center, double radius, double startDeg, double spanDeg)
{
    Q_UNUSED(startDeg);
    Q_UNUSED(spanDeg);
    m_scene->addEllipse(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0), QPen(QColor(255, 170, 120), 2, Qt::DashLine));
    if (m_document)
    {
        auto id = m_document->createArc(center, radius, startDeg, spanDeg);
        m_document->clearSelection();
        m_document->selectEntity(id);
    }
    refreshSelectionStyle();
    if (m_operationBus)
        m_operationBus->run(OperationId::Tool_Arc, {}, OperationSource::DrawTool);
}

void Viewport2D::refreshCopiedSelection()
{
    // 复制完成后，把新对象重新设为当前选择，方便继续编辑
    if (!m_document)
        return;

    if (m_copiedEntityIds.isEmpty())
        return;

    auto* sm = m_document->sceneManager();
    sm->clearSelection();
    for (const auto& id : m_copiedEntityIds)
    {
        bool ok = false;
        Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
        if (ok)
        {
            auto* entity = sm->findEntityById(eid);
            if (entity)
                sm->selectEntity(entity);
        }
    }

    refreshSelectionStyle();
    syncSelectionDetails();
    m_copiedEntityIds.clear();
}

QPointF Viewport2D::snapPoint(const QPointF& scenePos) const
{
    constexpr double snapStep = 10.0;
    return QPointF(std::round(scenePos.x() / snapStep) * snapStep,
        std::round(scenePos.y() / snapStep) * snapStep);
}

void Viewport2D::updateStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}

void Viewport2D::refreshFromDocument()
{
    // 确认只依赖文档，不依赖视口缓存
    if (!m_document)
        return;

    m_scene->clear();
    ensureGrid();
    ensureAxes();

    auto* sm = m_document->sceneManager();
    if (!sm) return;

    auto selected = sm->getSelectedEntities();
    auto isSelected = [&](const Eg::SyEntity* e) {
        return std::find(selected.begin(), selected.end(), e) != selected.end();
    };

    for (const auto* entity : sm->getAllEntities())
    {
        const bool sel = isSelected(entity);
        const QColor lineColor = sel ? QColor(80, 180, 255) : QColor(255, 220, 120);
        const int lineWidth = sel ? 3 : 2;

        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            if (line->vPoints.size() >= 2)
            {
                const auto& p0 = line->vPoints[0];
                const auto& p1 = line->vPoints[1];
                m_scene->addLine(QLineF(QPointF(p0.x(), p0.y()), QPointF(p1.x(), p1.y())), QPen(lineColor, lineWidth));
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<const Eg::SyCircle*>(entity);
            const auto& c = entity->basePoint;
            const double r = circle->dRadius;
            m_scene->addEllipse(QRectF(c.x() - r, c.y() - r, r * 2.0, r * 2.0), QPen(lineColor, lineWidth));
        }
        else if (entity->eType == Eg::EType::ARC)
        {
            auto* arc = static_cast<const Eg::SyArc*>(entity);
            const auto& c = entity->basePoint;
            const double r = arc->dRadius;
            m_scene->addEllipse(QRectF(c.x() - r, c.y() - r, r * 2.0, r * 2.0), QPen(QColor(255, 170, 120), 2, Qt::DashLine));
        }
    }
}

void Viewport2D::refreshSelectionStyle()
{
    if (!m_document)
        return;
    refreshFromDocument();
}

QString Viewport2D::selectedEntityId() const
{
    if (!m_document)
        return {};
    auto ids = m_document->selectedIds();
    return ids.isEmpty() ? QString() : ids.first();
}

void Viewport2D::deleteSelectedEntity()
{
    // 从文档 selection 读取选中实体
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;
    m_document->removeEntity(selectedId);
    clearSelection();
    updateStatus(tr("2D entity deleted")); // 2D 实体已删除
    refreshFromDocument();
    // 通知 OperationBus 实体已删除（过渡期兼容）
    if (m_operationBus)
        m_operationBus->run(OperationId::Edit_Delete, {}, OperationSource::DrawTool);
}

void Viewport2D::clearSelection()
{
    if (m_document)
        m_document->clearSelection();
    m_copiedEntityIds.clear();
}

void Viewport2D::nudgeSelectedEndpoint(const QPointF& delta)
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;
    auto* sm = m_document->sceneManager();
    if (!sm) return;
    bool ok = false;
    auto* entity = sm->findEntityById(static_cast<Eg::EntityId>(selectedId.toULongLong(&ok)));
    if (!ok || !entity || entity->eType != Eg::EType::LINE)
        return;
    auto* line = static_cast<Eg::SyLine*>(entity);
    for (auto& pt : line->vPoints)
    {
        pt.x() += delta.x();
        pt.y() += delta.y();
    }
    sm->clearSelection();
    sm->selectEntity(line);
    syncSelectionDetails();
    updateStatus(tr("2D endpoint moved")); // 2D 端点已移动
    refreshFromDocument();
}

void Viewport2D::selectEntityById(const QString& entityId)
{
    if (!m_document)
        return;
    m_document->clearSelection();
    m_document->selectEntity(entityId);
    syncSelectionDetails();
    refreshSelectionStyle();
    updateStatus(tr("2D entity selected")); // 2D 实体已选中
    if (m_selectionCallback)
        m_selectionCallback(tr("2D-Select"), tr("2D entity: %1").arg(entityId)); // 2D 选择 / 2D 实体: %1
    if (m_operationBus)
        m_operationBus->run(OperationId::Tool_Select, {}, OperationSource::DrawTool);
}

void Viewport2D::syncSelectionDetails()
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;
    auto* sm = m_document->sceneManager();
    if (!sm) return;
    bool ok = false;
    auto* entity = sm->findEntityById(static_cast<Eg::EntityId>(selectedId.toULongLong(&ok)));
    if (!ok || !entity)
        return;

    auto qid = QString::number(entity->id);
    if (entity->eType == Eg::EType::LINE)
    {
        auto* line = static_cast<Eg::SyLine*>(entity);
        if (line->vPoints.size() >= 2)
        {
            const auto& p0 = line->vPoints[0];
            const auto& p1 = line->vPoints[1];
            const double len = QLineF(QPointF(p0.x(), p0.y()), QPointF(p1.x(), p1.y())).length();
            updateStatus(tr("2D line %1 len=%2 start=(%3,%4) end=(%5,%6)").arg(qid).arg(len, 0, 'f', 2).arg(p0.x(), 0, 'f', 1).arg(p0.y(), 0, 'f', 1).arg(p1.x(), 0, 'f', 1).arg(p1.y(), 0, 'f', 1)); // 2D 线 %1 长度=%2 起点=(%3,%4) 终点=(%5,%6)
            return;
        }
    }
    if (entity->eType == Eg::EType::CIRCLE)
    {
        auto* circle = static_cast<Eg::SyCircle*>(entity);
        const auto& c = entity->basePoint;
        updateStatus(tr("2D circle %1 center=(%2,%3) r=%4").arg(qid).arg(c.x(), 0, 'f', 1).arg(c.y(), 0, 'f', 1).arg(circle->dRadius, 0, 'f', 1)); // 2D 圆 %1 中心=(%2,%3) 半径=%4
        return;
    }
    if (entity->eType == Eg::EType::ARC)
    {
        auto* arc = static_cast<Eg::SyArc*>(entity);
        const auto& c = entity->basePoint;
        updateStatus(tr("2D arc %1 center=(%2,%3) r=%4 start=%5 end=%6").arg(qid).arg(c.x(), 0, 'f', 1).arg(c.y(), 0, 'f', 1).arg(arc->dRadius, 0, 'f', 1).arg(arc->dStartAngle, 0, 'f', 1).arg(arc->dEndAngle, 0, 'f', 1)); // 2D 弧 %1 中心=(%2,%3) 半径=%4 起始=%5 结束=%6
    }
}

void Viewport2D::setSelectedFromHitTest(const QPointF& scenePos)
{
    if (!m_document)
        return;
    double bestScore = 1e18;
    QString bestLineId;
    int bestEndpoint = -1;
    bool bestIsEndpoint = false;
    auto* sm = m_document->sceneManager();
    if (!sm) return;
    for (const auto* entity : sm->getAllEntities())
    {
        if (entity->eType != Eg::EType::LINE)
            continue;
        auto* line = static_cast<const Eg::SyLine*>(entity);
        if (line->vPoints.size() < 2)
            continue;
        const QPointF p0(line->vPoints[0].x(), line->vPoints[0].y());
        const QPointF p1(line->vPoints[1].x(), line->vPoints[1].y());
        const double ds = QLineF(scenePos, p0).length();
        const double de = QLineF(scenePos, p1).length();
        const auto p = Ut::Vec2d(scenePos.x(), scenePos.y());
        const auto& a = line->vPoints[0];
        const auto& b = line->vPoints[1];
        const double segLen = (b - a).length();
        const double dline = (segLen < 1e-12)
            ? (p - a).length()
            : std::abs((b.x() - a.x()) * (a.y() - p.y()) - (a.x() - p.x()) * (b.y() - a.y())) / segLen;
        const double endpointBest = std::min(ds, de);
        if (endpointBest <= kHitRadius && (!bestIsEndpoint || endpointBest < bestScore))
        {
            bestScore = endpointBest;
            bestLineId = QString::number(entity->id);
            bestEndpoint = (ds <= de) ? 0 : 1;
            bestIsEndpoint = true;
        }
        else if (!bestIsEndpoint && dline <= kLineHitTolerance && dline < bestScore)
        {
            bestScore = dline;
            bestLineId = QString::number(entity->id);
            bestEndpoint = -1;
        }
    }
    if (!bestLineId.isEmpty())
    {
        selectEntityById(bestLineId);
        // 端点信息不再存储于视口，后续通过命令或工具管理
        syncSelectionDetails();
        updateStatus(tr("2D hit %1").arg(bestLineId)); // 2D 命中 %1
    }
}

void Viewport2D::startCommand(const QString& commandId)
{
    // 过渡兼容层 — 旧命令系统入口
    // 新操作应优先通过 OperationBus::run() 执行
    // 保留此方法仅用于兼容旧工具桥
    if (m_commandDispatcher)
        m_commandDispatcher->execute(commandId);
}

void Viewport2D::finishCommand(bool committed)
{
    // 过渡兼容层 — 旧命令系统入口
    // 新操作应优先通过 OperationBus::run() 执行
    // 保留此方法仅用于兼容旧工具桥
    if (m_interactionDispatcher)
    {
        if (committed)
            m_interactionDispatcher->submit();
        else
            m_interactionDispatcher->cancel();
    }

    // 过渡期：重置旧工具状态（后续各命令自己管理状态后移除）
    m_toolContext.tool = ToolContext::DrawTool::None;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    setCommandStage(tr("Idle")); // 空闲
    if (m_selectionCallback)
        m_selectionCallback(committed ? tr("2D-Commit") : tr("2D-Cancel"), QString()); // 2D 提交 / 2D 取消
}

void Viewport2D::setCommandStage(const QString& stage)
{
    if (m_commandStageCallback)
        m_commandStageCallback(stage);
}


void Viewport2D::activateDrawTool(ToolContext::DrawTool tool, const QString& commandId, const QString& statusText)
{
    m_toolContext.tool = tool;
    m_toolContext.drawing = true;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(commandId);
    updateStatus(statusText);
}

void Viewport2D::activateTransformTool(ToolContext::DrawTool tool, const QString& commandId, const QString& statusText)
{
    m_toolContext.tool = tool;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = (tool == ToolContext::DrawTool::Copy);
    startCommand(commandId);
    updateStatus(statusText);
}

// 进入 polyline 绘制模式
// TODO: 迁移时替换为 OperationBus::run(PolylineDraw, {})
void Viewport2D::enterPolylineMode()
{
    m_polylinePoints.clear();
    activateDrawTool(ToolContext::DrawTool::Polyline, QStringLiteral("2d.draw_polyline"), QStringLiteral("2D polyline start"));
}

// 进入 circle 绘制模式
// TODO: 迁移时替换为 OperationBus::run(CircleDraw, {})
void Viewport2D::enterCircleMode()
{
    activateDrawTool(ToolContext::DrawTool::Circle, QStringLiteral("2d.draw_circle"), QStringLiteral("2D circle start"));
}

// 进入 arc 绘制模式
// TODO: 迁移时替换为 OperationBus::run(ArcDraw, {})
void Viewport2D::enterArcMode()
{
    activateDrawTool(ToolContext::DrawTool::Arc, QStringLiteral("2d.draw_arc"), QStringLiteral("2D arc start"));
}

void Viewport2D::enterSelectMode()
{
    m_toolContext.tool = ToolContext::DrawTool::None;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    setDrawingEnabled(false);
    setMeasureMode(false);
    updateStatus(tr("2D select mode"));
}

// TODO: 迁移时替换为 OperationBus::run(Edit_Move, {})
void Viewport2D::enterMoveMode()
{
    activateTransformTool(ToolContext::DrawTool::Move, QStringLiteral("2d.move"), tr("2D move start"));
}

// TODO: 迁移时替换为 OperationBus::run(Edit_Copy, {})
void Viewport2D::enterCopyMode()
{
    activateTransformTool(ToolContext::DrawTool::Copy, QStringLiteral("2d.copy"), tr("2D copy start"));
}

// TODO: 迁移时替换为 OperationBus::run(Edit_Rotate, {})
void Viewport2D::enterRotateMode()
{
    activateTransformTool(ToolContext::DrawTool::Rotate, QStringLiteral("2d.rotate"), tr("2D rotate start"));
}

// TODO: 迁移时替换为 OperationBus::run(Edit_Mirror, {})
void Viewport2D::enterMirrorMode()
{
    activateTransformTool(ToolContext::DrawTool::Mirror, QStringLiteral("2d.mirror"), tr("2D mirror start"));
}

// TODO: 迁移时替换为 OperationBus::run(Edit_Trim, {})
void Viewport2D::enterTrimMode()
{
    activateTransformTool(ToolContext::DrawTool::Trim, QStringLiteral("2d.trim"), tr("2D trim start"));
}

// TODO: 迁移时替换为 OperationBus::run(Edit_Extend, {})
void Viewport2D::enterExtendMode()
{
    activateTransformTool(ToolContext::DrawTool::Extend, QStringLiteral("2d.extend"), tr("2D extend start"));
}

// TODO: 迁移时替换为 OperationBus::run(Select_Box, {})
void Viewport2D::enterBoxSelectMode()
{
    m_toolContext.tool = ToolContext::DrawTool::BoxSelect;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.box_select"));
    updateStatus(tr("2D box select start"));
}

// TODO: 迁移时替换为 OperationBus::run(Select_Box, {}) 的开始阶段
void Viewport2D::beginBoxSelect(const QPointF& scenePos)
{
    m_toolContext.boxSelecting = true;
    m_boxSelectStart = scenePos;
}

// TODO: 迁移时替换为 OperationBus::run(Select_Box, {}) 的更新阶段
void Viewport2D::updateBoxSelect(const QPointF& scenePos)
{
    if (!m_toolContext.boxSelecting)
        return;
    const QRectF rect(m_boxSelectStart, scenePos);
    m_scene->addRect(rect.normalized(), QPen(QColor(80, 180, 255), 1, Qt::DashLine));
}

// TODO: 迁移时替换为 OperationBus::run(Select_Box, {}) 的结束阶段
void Viewport2D::endBoxSelect(const QPointF& scenePos)
{
    if (!m_document || !m_toolContext.boxSelecting)
        return;

    const QRectF rect(m_boxSelectStart, scenePos);
    auto* sm = m_document->sceneManager();
    if (!sm) return;
    sm->clearSelection();

    const QRectF normalizedRect = rect.normalized();
    for (const auto* entity : sm->getAllEntities())
    {
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            if (line->vPoints.size() >= 2)
            {
                const QPointF p0(line->vPoints[0].x(), line->vPoints[0].y());
                const QPointF p1(line->vPoints[1].x(), line->vPoints[1].y());
                if (normalizedRect.intersects(QRectF(p0, p1).normalized()))
                    sm->selectEntity(const_cast<Eg::SyLine*>(line));
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<const Eg::SyCircle*>(entity);
            const double x = entity->basePoint.x() - circle->dRadius;
            const double y = entity->basePoint.y() - circle->dRadius;
            const double s = circle->dRadius * 2.0;
            if (normalizedRect.intersects(QRectF(x, y, s, s)))
                sm->selectEntity(const_cast<Eg::SyCircle*>(circle));
        }
        else if (entity->eType == Eg::EType::ARC)
        {
            auto* arc = static_cast<const Eg::SyArc*>(entity);
            const double x = entity->basePoint.x() - arc->dRadius;
            const double y = entity->basePoint.y() - arc->dRadius;
            const double s = arc->dRadius * 2.0;
            if (normalizedRect.intersects(QRectF(x, y, s, s)))
                sm->selectEntity(const_cast<Eg::SyArc*>(arc));
        }
    }

    m_toolContext.boxSelecting = false;
    finishCommand(true);
    updateStatus(tr("2D box select end"));
    refreshSelectionStyle();
}

void Viewport2D::trimSelectedByPoint(const QPointF& point)
{
    UiSelectionTools::trimSelectedByPoint(m_document, point, nullptr);
    updateStatus(tr("2D trim applied"));
    refreshFromDocument();
}

void Viewport2D::extendSelectedByPoint(const QPointF& point)
{
    UiSelectionTools::extendSelectedByPoint(m_document, point, nullptr);
    updateStatus(tr("2D extend applied"));
    refreshFromDocument();
}

void Viewport2D::applySelectionTransform(const QPointF& anchor, const QPointF& target, const QString& mode)
{
    UiSelectionTools::applySelectionTransform(m_document, anchor, target, m_toolContext.transformCopy, mode, nullptr, QStringLiteral("2D"));
    refreshCopiedSelection();
    updateStatus(tr("2D %1 applied").arg(mode));
    refreshFromDocument();
}

void Viewport2D::wheelEvent(QWheelEvent* event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 0.87;
    scale(factor, factor);
    updateStatus(tr("2D zoom"));
}

void Viewport2D::mouseDoubleClickEvent(QMouseEvent* event)
{
    // 双击用于结束折线输入，符合常见 CAD 交互习惯
    if (m_toolContext.tool == ToolContext::DrawTool::Polyline && !m_polylinePoints.isEmpty())
    {
        finishPolyline(snapPoint(mapToScene(event->pos())));
        finishCommand(true);
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void Viewport2D::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    auto* drawLine = menu.addAction(tr("Draw Line")); // 绘制直线
    auto* drawPolyline = menu.addAction(tr("Draw Polyline")); // 绘制折线
    auto* drawCircle = menu.addAction(tr("Draw Circle")); // 绘制圆
    auto* drawArc = menu.addAction(tr("Draw Arc")); // 绘制弧
    auto* move = menu.addAction(tr("Move")); // 移动
    auto* copy = menu.addAction(tr("Copy")); // 复制
    auto* rotate = menu.addAction(tr("Rotate")); // 旋转
    auto* mirror = menu.addAction(tr("Mirror")); // 镜像
    auto* trim = menu.addAction(tr("Trim")); // 修剪
    auto* extend = menu.addAction(tr("Extend")); // 延伸
    auto* boxSelect = menu.addAction(tr("Box Select")); // 框选
    auto* measure = menu.addAction(tr("Measure")); // 测量
    auto* deleteEntity = menu.addAction(tr("Delete")); // 删除
    auto* editEntity = menu.addAction(tr("Edit")); // 编辑
    auto* selectEntity = menu.addAction(tr("Select")); // 选择

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == drawLine)
    {
        setDrawingEnabled(true); m_toolContext.tool = ToolContext::DrawTool::Line; startCommand(QStringLiteral("2d.draw_line"));
    }
    else if (chosen == drawPolyline) enterPolylineMode();
    else if (chosen == drawCircle) enterCircleMode();
    else if (chosen == drawArc) enterArcMode();
    else if (chosen == move) enterMoveMode();
    else if (chosen == copy) enterCopyMode();
    else if (chosen == rotate) enterRotateMode();
    else if (chosen == mirror) enterMirrorMode();
    else if (chosen == trim) enterTrimMode();
    else if (chosen == extend) enterExtendMode();
    else if (chosen == boxSelect) enterSelectMode();
    else if (chosen == measure)
    {
        setMeasureMode(true); startCommand(QStringLiteral("2d.measure"));
    }
    else if (chosen == deleteEntity) deleteSelectedEntity();
    else if (chosen == editEntity) updateStatus(tr("2D edit mode"));
    else if (chosen == selectEntity) enterSelectMode();
}

void Viewport2D::updateCommandPreview()
{
    // 通过 ICommandHandler::preview() 通用接口获取预览数据
    // 视口不再依赖具体命令类（如 DrawLineCommand）
    // 后续 OperationBus 中的 IOperation 也可通过此接口提供预览
    if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
    {
        if (m_previewLine)
        {
            m_scene->removeItem(m_previewLine);
            m_previewLine = nullptr;
        }
        return;
    }

    auto handler = m_interactionDispatcher->currentHandler();
    if (!handler)
        return;

    CommandPreview preview = handler->preview();
    if (preview.valid)
    {
        addPreviewLine(preview.previewStart, preview.previewEnd);
    }
    else
    {
        // 预览无效时清除预览线
        if (m_previewLine)
        {
            m_scene->removeItem(m_previewLine);
            m_previewLine = nullptr;
        }
    }
}

void Viewport2D::mousePressEvent(QMouseEvent* event)
{
    // 事件路由优先级（OperationBus 为主线）
    // 1. 活动操作 → 转发给 IInteractionDispatcher（过渡期兼容层，后续统一到 OperationBus）
    // 2. 空闲态基础交互 → 中键平移、左键选择
    // 3. 旧工具路径 → 过渡期兜底，后续逐步迁移到 OperationBus

    // 优先级1：活动操作优先（过渡期通过 IInteractionDispatcher 转发）
    if (forwardActiveCommand(m_interactionDispatcher, event, &IInteractionDispatcher::forwardMouseDown))
    {
        updateCommandPreview();
        return;
    }

    const QPointF scenePos = snapPoint(mapToScene(event->pos()));

    // 优先级2：空闲态基础交互
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        updateStatus(tr("2D pan start")); // 2D 平移开始
        return;
    }

    // 空闲态左键选择（无活动命令且无旧工具激活时触发）
    if (event->button() == Qt::LeftButton
        && (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
        && m_toolContext.tool == ToolContext::DrawTool::None)
    {
        setSelectedFromHitTest(scenePos);
        updateStatus(tr("2D select")); // 2D 选择
        return;
    }

    // 优先级3：旧工具路径（过渡期，仅当无活动命令时触发）
    // 已迁移命令（Line/Move/Rotate）的旧分支已删除，统一走命令系统
    if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
    {
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Polyline)
        {
            commitPolylinePoint(scenePos);
            setCommandStage(tr("Polyline point input")); // 折线点输入
            updateStatus(tr("2D polyline point")); // 2D 折线点
            return;
        }
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Circle)
        {
            m_toolContext.hasDrawStart = true;
            m_drawStartPoint = scenePos;
            addPreviewLine(scenePos, scenePos);
            setCommandStage(tr("Waiting for radius point")); // 等待半径点
            updateStatus(tr("2D circle center")); // 2D 圆心
            return;
        }
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Arc)
        {
            m_toolContext.hasDrawStart = true;
            m_drawStartPoint = scenePos;
            addPreviewLine(scenePos, scenePos);
            setCommandStage(tr("Waiting for arc radius point")); // 等待弧半径点
            updateStatus(tr("2D arc center")); // 2D 弧心
            return;
        }
        
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Copy)
        {
            m_toolContext.hasDrawStart = true;
            m_drawStartPoint = scenePos;
            setCommandStage(tr("Select copy target point")); // 选择复制目标点
            updateStatus(tr("2D copy anchor")); // 2D 复制锚点
            return;
        }
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Mirror)
        {
            m_toolContext.hasDrawStart = true;
            m_drawStartPoint = scenePos;
            setCommandStage(tr("Select mirror base point")); // 选择镜像基准点
            updateStatus(tr("2D mirror anchor")); // 2D 镜像锚点
            return;
        }
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Trim)
        {
            trimSelectedByPoint(scenePos);
            setCommandStage(tr("Select trim target")); // 选择修剪目标
            return;
        }
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Extend)
        {
            extendSelectedByPoint(scenePos);
            setCommandStage(tr("Select extend target")); // 选择延伸目标
            return;
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void Viewport2D::mouseMoveEvent(QMouseEvent* event)
{
    // 事件路由优先级同 mousePressEvent（OperationBus 为主线）
    if (forwardActiveCommand(m_interactionDispatcher, event, &IInteractionDispatcher::forwardMouseMove))
    {
        updateCommandPreview();
        return;
    }

    const QPointF scenePos = snapPoint(mapToScene(event->pos()));

    // 优先级2：中键平移
    if (m_panning)
    {
        const QPointF delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - static_cast<int>(delta.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - static_cast<int>(delta.y()));
        return;
    }

    // 优先级3：旧工具路径（过渡期，仅当无活动命令时触发）
    // Line/Move/Rotate 已迁移到命令系统，旧分支已删除
    if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
    {
        if (m_toolContext.tool == ToolContext::DrawTool::Circle && m_toolContext.hasDrawStart) { addPreviewLine(m_drawStartPoint, scenePos); setCommandStage(tr("Circle preview")); updateStatus(tr("2D circle preview")); return; } // 圆预览 / 2D 圆预览
        if (m_toolContext.tool == ToolContext::DrawTool::Arc && m_toolContext.hasDrawStart) { addPreviewLine(m_drawStartPoint, scenePos); setCommandStage(tr("Arc preview")); updateStatus(tr("2D arc preview")); return; } // 弧预览 / 2D 弧预览
        if ((m_toolContext.tool == ToolContext::DrawTool::Copy || m_toolContext.tool == ToolContext::DrawTool::Mirror) && m_toolContext.hasDrawStart) { setCommandStage(tr("Transform preview")); applySelectionTransform(m_drawStartPoint, scenePos, QStringLiteral("transform")); return; } // 变换预览
    }

    QGraphicsView::mouseMoveEvent(event);
}

void Viewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    // 事件路由优先级（OperationBus 为主线）
    if (forwardActiveCommand(m_interactionDispatcher, event, &IInteractionDispatcher::forwardMouseUp))
        return;

    // 优先级2：中键平移结束
    if (event->button() == Qt::MiddleButton && m_panning)
    {
        m_panning = false;
        unsetCursor();
        updateStatus(tr("2D pan end")); // 2D 平移结束
        return;
    }

    // 优先级3：旧工具路径（过渡期，仅当无活动命令时触发）
    // Line/Move/Rotate 已迁移到命令系统，旧分支已删除
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));
    if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
    {
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Circle && m_toolContext.hasDrawStart)
        {
            const double radius = QLineF(m_drawStartPoint, scenePos).length();
            commitCircle(m_drawStartPoint, radius);
            m_toolContext.hasDrawStart = false;
            setCommandStage(tr("Commit complete")); // 提交完成
            finishCommand(true);
            updateStatus(tr("2D circle committed")); // 2D 圆已提交
            return;
        }
        if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Arc && m_toolContext.hasDrawStart)
        {
            const double radius = QLineF(m_drawStartPoint, scenePos).length();
            commitArc(m_drawStartPoint, radius, 0.0, 90.0);
            m_toolContext.hasDrawStart = false;
            setCommandStage(tr("Commit complete")); // 提交完成
            finishCommand(true);
            updateStatus(tr("2D arc committed")); // 2D 弧已提交
            return;
        }
        
        if (event->button() == Qt::LeftButton && (m_toolContext.tool == ToolContext::DrawTool::Copy || m_toolContext.tool == ToolContext::DrawTool::Mirror) && m_toolContext.hasDrawStart)
        {
            applySelectionTransform(m_drawStartPoint, scenePos, QStringLiteral("transform"));
            m_toolContext.hasDrawStart = false;
            setCommandStage(tr("Commit complete")); // 提交完成
            finishCommand(true);
            m_toolContext.transformCopy = false;
            return;
        }
    }

    QGraphicsView::mouseReleaseEvent(event);
}




