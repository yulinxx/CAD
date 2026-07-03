/**
 * @file UiViewWidgets.cpp
 * @brief 2D/3D 视口与辅助面板实现
 */

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
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "UiCommandDispatcher.h"
#include "UiEntities.h"
#include "UiGeometryAlgorithms.h"
#include "UiSelectionTools.h"
#include "UiStateCenter.h"

namespace
{
    constexpr double kGridStep = 50.0;
    constexpr double kHitRadius = 10.0;
    constexpr double kLineHitTolerance = 8.0;
}

CanvasViewport2D::CanvasViewport2D(QWidget* parent)
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

void CanvasViewport2D::setStatusCallback(std::function<void(const QString&)>&& callback)
{
    m_statusCallback = std::move(callback);
}

void CanvasViewport2D::setDocument(EntityDocument2D* document)
{
    m_document = document;
    refreshFromDocument();
}

void CanvasViewport2D::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void CanvasViewport2D::setCommandDispatcher(UiCommandDispatcher* dispatcher)
{
    m_commandDispatcher = dispatcher;
}

void CanvasViewport2D::setDrawingEnabled(bool enabled)
{
    m_toolContext.drawing = enabled;
    m_toolContext.measureMode = false;
    m_toolContext.tool = enabled ? ToolContext::DrawTool::Line : ToolContext::DrawTool::None;
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandPhase(enabled ? QStringLiteral("Waiting for first point") : QStringLiteral("Idle")); // 等待第一点 / 空闲
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("2D"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("2D"));
    }
    updateStatus(enabled ? QStringLiteral("2D draw mode") : QStringLiteral("2D select mode"));
}

void CanvasViewport2D::setMeasureMode(bool enabled)
{
    m_toolContext.measureMode = enabled;
    m_toolContext.drawing = false;
    m_toolContext.tool = ToolContext::DrawTool::None;
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandPhase(enabled ? QStringLiteral("Waiting for first point") : QStringLiteral("Idle")); // 等待第一点 / 空闲
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("2D"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("2D"));
    }
    updateStatus(enabled ? QStringLiteral("2D measure mode") : QStringLiteral("2D select mode"));
}

void CanvasViewport2D::resetView()
{
    resetTransform();
    centerOn(0, 0);
    updateStatus(QStringLiteral("2D view reset"));
}

void CanvasViewport2D::ensureGrid()
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

void CanvasViewport2D::ensureAxes()
{
    // 坐标轴帮助用户确认当前画布方向
    m_scene->addLine(-5000, 0, 5000, 0, QPen(QColor(220, 80, 80), 2));
    m_scene->addLine(0, -5000, 0, 5000, QPen(QColor(80, 220, 120), 2));
}

void CanvasViewport2D::addPreviewLine(const QPointF& start, const QPointF& end)
{
    if (!m_previewLine)
    {
        m_previewLine = m_scene->addLine(QLineF(start, end), QPen(QColor(80, 180, 255), 2, Qt::DashLine));
        return;
    }
    m_previewLine->setLine(QLineF(start, end));
}

void CanvasViewport2D::commitLine(const QPointF& start, const QPointF& end)
{
    m_scene->addLine(QLineF(start, end), QPen(QColor(255, 220, 120), 2));
    if (m_document)
    {
        auto entity = m_document->createLine(start, end);
        m_document->selection().clear();
        m_document->selection().add(entity);
        m_selectedEntityId = entity->id();
        m_selectedEndpoint = -1;
    }
    refreshSelectionStyle();
}

void CanvasViewport2D::commitPolylinePoint(const QPointF& pt)
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

void CanvasViewport2D::finishPolyline(const QPointF& pt)
{
    // 双击或右键收尾时调用，把当前折线真正写回文档
    if (!m_document)
        return;

    m_polylinePoints.push_back(pt);
    if (m_polylinePoints.size() < 2)
    {
        m_polylinePoints.clear();
        updateStatus(QStringLiteral("2D polyline cancelled"));
        return;
    }

    auto entity = m_document->createPolyline(m_polylinePoints);
    m_document->selection().clear();
    m_document->selection().add(entity);
    m_selectedEntityId = entity->id();
    m_selectedEndpoint = -1;
    m_polylinePoints.clear();
    refreshSelectionStyle();

    if (m_stateCenter)
        m_stateCenter->setDirty(true);

    updateStatus(QStringLiteral("2D polyline committed"));
}

void CanvasViewport2D::commitCircle(const QPointF& center, double radius)
{
    m_scene->addEllipse(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0), QPen(QColor(255, 220, 120), 2));
    if (m_document)
    {
        auto entity = m_document->createCircle(center, radius);
        m_document->selection().clear();
        m_document->selection().add(entity);
        m_selectedEntityId = entity->id();
    }
    refreshSelectionStyle();
}

void CanvasViewport2D::commitArc(const QPointF& center, double radius, double startDeg, double spanDeg)
{
    Q_UNUSED(startDeg);
    Q_UNUSED(spanDeg);
    m_scene->addEllipse(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0), QPen(QColor(255, 170, 120), 2, Qt::DashLine));
    if (m_document)
    {
        auto entity = m_document->createArc(center, radius, startDeg, spanDeg);
        m_document->selection().clear();
        m_document->selection().add(entity);
        m_selectedEntityId = entity->id();
    }
    refreshSelectionStyle();
}

void CanvasViewport2D::refreshCopiedSelection()
{
    // 复制完成后，把新对象重新设为当前选择，方便继续编辑
    if (!m_document)
        return;

    if (m_copiedEntityIds.isEmpty())
        return;

    m_document->selection().clear();
    for (const auto& id : m_copiedEntityIds)
    {
        if (auto entity = m_document->entityById(id))
            m_document->selection().add(entity);
    }

    if (!m_copiedEntityIds.isEmpty())
        m_selectedEntityId = m_copiedEntityIds.first();

    refreshSelectionStyle();
    syncSelectionDetails();
    m_copiedEntityIds.clear();
}

QPointF CanvasViewport2D::snapPoint(const QPointF& scenePos) const
{
    constexpr double snapStep = 10.0;
    return QPointF(std::round(scenePos.x() / snapStep) * snapStep,
        std::round(scenePos.y() / snapStep) * snapStep);
}

void CanvasViewport2D::updateStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
    if (m_stateCenter)
    {
        m_stateCenter->setMetadata({
            { QStringLiteral("viewportStatus"), text },
            { QStringLiteral("viewportType"), QStringLiteral("2D") }
            });
    }
}

void CanvasViewport2D::refreshFromDocument()
{
    if (!m_document)
        return;

    m_scene->clear();
    ensureGrid();
    ensureAxes();

    for (const auto& line : m_document->lines())
        if (line) m_scene->addLine(QLineF(line->start(), line->end()), QPen(line->selected() ? QColor(80, 180, 255) : QColor(255, 220, 120), line->selected() ? 3 : 2));

    for (const auto& polyline : m_document->polylines())
    {
        if (!polyline) continue;
        const auto pts = polyline->points();
        for (int i = 1; i < pts.size(); ++i)
            m_scene->addLine(QLineF(pts[i - 1], pts[i]), QPen(QColor(255, 180, 100), 2));
    }

    for (const auto& circle : m_document->circles())
        if (circle) m_scene->addEllipse(circle->bounds(), QPen(QColor(255, 220, 120), 2));

    for (const auto& arc : m_document->arcs())
    {
        if (!arc) continue;
        const auto b = QRectF(arc->center().x() - arc->radius(), arc->center().y() - arc->radius(), arc->radius() * 2.0, arc->radius() * 2.0);
        m_scene->addEllipse(b, QPen(QColor(255, 170, 120), 2, Qt::DashLine));
    }
}

void CanvasViewport2D::refreshSelectionStyle()
{
    if (!m_document)
        return;
    for (const auto& entity : m_document->selection().items())
    {
        if (!entity) continue;
        entity->setHighlighted(true);
        entity->setSelected(true);
    }
    refreshFromDocument();
}

QString CanvasViewport2D::selectedEntityId() const
{
    return m_selectedEntityId;
}

void CanvasViewport2D::deleteSelectedEntity()
{
    if (!m_document || m_selectedEntityId.isEmpty())
        return;
    m_document->removeEntity(m_selectedEntityId);
    clearSelection();
    updateStatus(QStringLiteral("2D entity deleted"));
    refreshFromDocument();
    if (m_stateCenter) m_stateCenter->setDirty(true);
}

void CanvasViewport2D::clearSelection()
{
    // 清空当前选中对象，避免删除/复制/框选后状态残留
    if (m_document)
        m_document->selection().clear();
    m_selectedEntityId.clear();
    m_selectedEndpoint = -1;
    m_copiedEntityIds.clear();
}

void CanvasViewport2D::nudgeSelectedEndpoint(const QPointF& delta)
{
    if (!m_document || m_selectedEntityId.isEmpty())
        return;
    auto line = m_document->lineById(m_selectedEntityId);
    if (!line)
        return;
    if (m_selectedEndpoint == 0) line->setStart(line->start() + delta);
    else line->setEnd(line->end() + delta);
    m_document->selection().clear();
    m_document->selection().add(line);
    syncSelectionDetails();
    updateStatus(QStringLiteral("2D endpoint moved"));
    refreshFromDocument();
    if (m_stateCenter) m_stateCenter->setDirty(true);
}

void CanvasViewport2D::selectEntityById(const QString& entityId)
{
    if (!m_document)
        return;
    m_selectedEntityId = entityId;
    m_selectedEndpoint = -1;
    m_document->selection().clear();
    if (auto entity = m_document->entityById(entityId)) m_document->selection().add(entity);
    syncSelectionDetails();
    refreshSelectionStyle();
    updateStatus(QStringLiteral("2D entity selected"));
    if (m_stateCenter) m_stateCenter->setSelectionContext(QStringLiteral("2D-Select"), QStringLiteral("2D entity: %1").arg(entityId));
}

void CanvasViewport2D::syncSelectionDetails()
{
    if (!m_document || m_selectedEntityId.isEmpty())
        return;
    if (auto entity = m_document->entityById(m_selectedEntityId))
    {
        if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
        {
            const double len = QLineF(line->start(), line->end()).length();
            updateStatus(QStringLiteral("2D line %1 len=%2 start=(%3,%4) end=(%5,%6)").arg(line->id()).arg(len, 0, 'f', 2).arg(line->start().x(), 0, 'f', 1).arg(line->start().y(), 0, 'f', 1).arg(line->end().x(), 0, 'f', 1).arg(line->end().y(), 0, 'f', 1));
            return;
        }
        if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
        {
            updateStatus(QStringLiteral("2D circle %1 center=(%2,%3) r=%4").arg(circle->id()).arg(circle->center().x(), 0, 'f', 1).arg(circle->center().y(), 0, 'f', 1).arg(circle->radius(), 0, 'f', 1));
            return;
        }
        if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
        {
            updateStatus(QStringLiteral("2D arc %1 center=(%2,%3) r=%4 start=%5 span=%6").arg(arc->id()).arg(arc->center().x(), 0, 'f', 1).arg(arc->center().y(), 0, 'f', 1).arg(arc->radius(), 0, 'f', 1).arg(arc->startAngleDeg(), 0, 'f', 1).arg(arc->spanDeg(), 0, 'f', 1));
        }
    }
}

void CanvasViewport2D::setSelectedFromHitTest(const QPointF& scenePos)
{
    if (!m_document)
        return;
    double bestScore = 1e18;
    QString bestLineId;
    int bestEndpoint = -1;
    bool bestIsEndpoint = false;
    for (const auto& line : m_document->lines())
    {
        if (!line) continue;
        const double ds = line->distanceToStart(scenePos);
        const double de = line->distanceToEnd(scenePos);
        const double dline = line->distanceToPoint(scenePos);
        const double endpointBest = std::min(ds, de);
        if (endpointBest <= kHitRadius && (!bestIsEndpoint || endpointBest < bestScore))
        {
            bestScore = endpointBest;
            bestLineId = line->id();
            bestEndpoint = (ds <= de) ? 0 : 1;
            bestIsEndpoint = true;
        }
        else if (!bestIsEndpoint && dline <= kLineHitTolerance && dline < bestScore)
        {
            bestScore = dline;
            bestLineId = line->id();
            bestEndpoint = -1;
        }
    }
    if (!bestLineId.isEmpty())
    {
        selectEntityById(bestLineId);
        m_selectedEndpoint = bestEndpoint;
        syncSelectionDetails();
        updateStatus(QStringLiteral("2D hit %1").arg(bestLineId));
    }
}

void CanvasViewport2D::startCommand(const QString& commandId)
{
    if (m_commandDispatcher) m_commandDispatcher->begin(commandId);
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandId(commandId);
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("2D"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("2D"));
        m_stateCenter->setBusy(true);
        m_stateCenter->setMetadata({
            { QStringLiteral("commandOwner"), QStringLiteral("2D") },
            { QStringLiteral("commandId"), commandId },
            { QStringLiteral("commandState"), QStringLiteral("begin") },
            { QStringLiteral("commandType"), QStringLiteral("2D") }
            });
    }
}

void CanvasViewport2D::finishCommand(bool committed)
{
    if (m_commandDispatcher)
    {
        if (committed) m_commandDispatcher->submit();
        else m_commandDispatcher->cancel();
    }
    m_toolContext.tool = ToolContext::DrawTool::None;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    setCommandStage(QStringLiteral("Idle")); // 空闲
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
        m_stateCenter->setBusy(false);
        m_stateCenter->setMetadata({
            { QStringLiteral("commandState"), committed ? QStringLiteral("submit") : QStringLiteral("cancel") },
            { QStringLiteral("commandId"), m_commandDispatcher ? m_commandDispatcher->activeCommandId() : QString() },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandType"), QStringLiteral("none") }
            });
        if (!committed)
            m_stateCenter->setSelectionContext(QStringLiteral("2D-Cancel"), m_stateCenter->currentSelectionText());
        else
            m_stateCenter->setSelectionContext(QStringLiteral("2D-Commit"), m_stateCenter->currentSelectionText());
    }
}

void CanvasViewport2D::setCommandStage(const QString& stage)
{
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandPhase(stage);
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("2D"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("2D"));
        m_stateCenter->setMetadata({
            { QStringLiteral("commandPhase"), stage },
            { QStringLiteral("commandOwner"), QStringLiteral("2D") },
            { QStringLiteral("commandType"), QStringLiteral("2D") }
            });
    }
}

void CanvasViewport2D::enterPolylineMode()
{
    // 折线进入点输入阶段，双击后收尾
    m_toolContext.tool = ToolContext::DrawTool::Polyline;
    m_toolContext.drawing = true;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    m_polylinePoints.clear();
    startCommand(QStringLiteral("2d.draw_polyline"));
    updateStatus(QStringLiteral("2D polyline start"));
}

void CanvasViewport2D::enterCircleMode()
{
    m_toolContext.tool = ToolContext::DrawTool::Circle;
    m_toolContext.drawing = true;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.draw_circle"));
    updateStatus(QStringLiteral("2D circle start"));
}

void CanvasViewport2D::enterArcMode()
{
    m_toolContext.tool = ToolContext::DrawTool::Arc;
    m_toolContext.drawing = true;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.draw_arc"));
    updateStatus(QStringLiteral("2D arc start"));
}

void CanvasViewport2D::enterSelectMode()
{
    m_toolContext.tool = ToolContext::DrawTool::None;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    setDrawingEnabled(false);
    setMeasureMode(false);
    updateStatus(QStringLiteral("2D select mode"));
}

void CanvasViewport2D::enterMoveMode()
{
    // 进入移动命令前，先清理复制标记，避免影响下一次操作
    m_toolContext.tool = ToolContext::DrawTool::Move;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.move"));
    updateStatus(QStringLiteral("2D move start"));
}

void CanvasViewport2D::enterCopyMode()
{
    // 复制命令要求保留原对象，因此先打开复制标记
    m_toolContext.tool = ToolContext::DrawTool::Copy;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = true;
    startCommand(QStringLiteral("2d.copy"));
    updateStatus(QStringLiteral("2D copy start"));
}

void CanvasViewport2D::enterRotateMode()
{
    // 旋转命令先进入等待基点阶段，后面再接目标点形成角度
    m_toolContext.tool = ToolContext::DrawTool::Rotate;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.rotate"));
    updateStatus(QStringLiteral("2D rotate start"));
}

void CanvasViewport2D::enterMirrorMode()
{
    m_toolContext.tool = ToolContext::DrawTool::Mirror;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.mirror"));
    updateStatus(QStringLiteral("2D mirror start"));
}

void CanvasViewport2D::enterTrimMode()
{
    m_toolContext.tool = ToolContext::DrawTool::Trim;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.trim"));
    updateStatus(QStringLiteral("2D trim start"));
}

void CanvasViewport2D::enterExtendMode()
{
    m_toolContext.tool = ToolContext::DrawTool::Extend;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = false;
    m_toolContext.transformCopy = false;
    startCommand(QStringLiteral("2d.extend"));
    updateStatus(QStringLiteral("2D extend start"));
}

void CanvasViewport2D::beginBoxSelect(const QPointF& scenePos)
{
    m_toolContext.tool = ToolContext::DrawTool::BoxSelect;
    m_toolContext.drawing = false;
    m_toolContext.measureMode = false;
    m_toolContext.hasDrawStart = false;
    m_toolContext.boxSelecting = true;
    m_toolContext.transformCopy = false;
    m_boxSelectStart = scenePos;
    startCommand(QStringLiteral("2d.box_select"));
    updateStatus(QStringLiteral("2D box select start"));
}

void CanvasViewport2D::updateBoxSelect(const QPointF& scenePos)
{
    if (!m_toolContext.boxSelecting)
        return;
    const QRectF rect(m_boxSelectStart, scenePos);
    m_scene->addRect(rect.normalized(), QPen(QColor(80, 180, 255), 1, Qt::DashLine));
}

void CanvasViewport2D::endBoxSelect(const QPointF& scenePos)
{
    if (!m_document || !m_toolContext.boxSelecting)
        return;

    const QRectF rect(m_boxSelectStart, scenePos);
    m_document->selection().clear();
    m_selectedEntityId.clear();
    m_selectedEndpoint = -1;

    // 框选时同时考虑对象边界和几何主体，尽量贴近 CAD 的选择体验
    for (const auto& line : m_document->lines())
        if (line && rect.normalized().intersects(line->bounds())) m_document->selection().add(line);
    for (const auto& polyline : m_document->polylines())
        if (polyline && rect.normalized().intersects(polyline->bounds())) m_document->selection().add(polyline);
    for (const auto& circle : m_document->circles())
        if (circle && rect.normalized().intersects(circle->bounds())) m_document->selection().add(circle);
    for (const auto& arc : m_document->arcs())
        if (arc && rect.normalized().intersects(QRectF(arc->center().x() - arc->radius(), arc->center().y() - arc->radius(), arc->radius() * 2.0, arc->radius() * 2.0))) m_document->selection().add(arc);

    m_toolContext.boxSelecting = false;
    finishCommand(true);
    updateStatus(QStringLiteral("2D box select end"));
    refreshSelectionStyle();
}

void CanvasViewport2D::trimSelectedByPoint(const QPointF& point)
{
    UiSelectionTools::trimSelectedByPoint(m_document, point, m_stateCenter);
    updateStatus(QStringLiteral("2D trim applied"));
    refreshFromDocument();
}

void CanvasViewport2D::extendSelectedByPoint(const QPointF& point)
{
    UiSelectionTools::extendSelectedByPoint(m_document, point, m_stateCenter);
    updateStatus(QStringLiteral("2D extend applied"));
    refreshFromDocument();
}

void CanvasViewport2D::applySelectionTransform(const QPointF& anchor, const QPointF& target, const QString& mode)
{
    UiSelectionTools::applySelectionTransform(m_document, anchor, target, m_toolContext.transformCopy, mode, m_stateCenter, QStringLiteral("2D"));
    refreshCopiedSelection();
    updateStatus(QStringLiteral("2D %1 applied").arg(mode));
    refreshFromDocument();
}

void CanvasViewport2D::wheelEvent(QWheelEvent* event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 0.87;
    scale(factor, factor);
    updateStatus(QStringLiteral("2D zoom"));
}

void CanvasViewport2D::mouseDoubleClickEvent(QMouseEvent* event)
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

void CanvasViewport2D::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    auto* drawLine = menu.addAction(QStringLiteral("Draw Line"));
    auto* drawPolyline = menu.addAction(QStringLiteral("Draw Polyline"));
    auto* drawCircle = menu.addAction(QStringLiteral("Draw Circle"));
    auto* drawArc = menu.addAction(QStringLiteral("Draw Arc"));
    auto* move = menu.addAction(QStringLiteral("Move"));
    auto* copy = menu.addAction(QStringLiteral("Copy"));
    auto* rotate = menu.addAction(QStringLiteral("Rotate"));
    auto* mirror = menu.addAction(QStringLiteral("Mirror"));
    auto* trim = menu.addAction(QStringLiteral("Trim"));
    auto* extend = menu.addAction(QStringLiteral("Extend"));
    auto* boxSelect = menu.addAction(QStringLiteral("Box Select"));
    auto* measure = menu.addAction(QStringLiteral("Measure"));
    auto* deleteEntity = menu.addAction(QStringLiteral("Delete"));
    auto* editEntity = menu.addAction(QStringLiteral("Edit"));
    auto* selectEntity = menu.addAction(QStringLiteral("Select"));

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
    else if (chosen == boxSelect)
    {
        m_toolContext.tool = ToolContext::DrawTool::BoxSelect; startCommand(QStringLiteral("2d.box_select"));
    }
    else if (chosen == measure)
    {
        setMeasureMode(true); startCommand(QStringLiteral("2d.measure"));
    }
    else if (chosen == deleteEntity) deleteSelectedEntity();
    else if (chosen == editEntity) updateStatus(QStringLiteral("2D edit mode"));
    else if (chosen == selectEntity) enterSelectMode();
}

void CanvasViewport2D::mousePressEvent(QMouseEvent* event)
{
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        updateStatus(QStringLiteral("2D pan start"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Line)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        addPreviewLine(scenePos, scenePos);
        setCommandStage(QStringLiteral("Waiting for second point")); // 等待第二点
        updateStatus(QStringLiteral("2D line start"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Polyline)
    {
        if (m_polylinePoints.isEmpty())
            startCommand(QStringLiteral("2d.draw_polyline"));
        commitPolylinePoint(scenePos);
        setCommandStage(QStringLiteral("Polyline input")); // 折线点输入中
        updateStatus(QStringLiteral("2D polyline point"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Circle)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        addPreviewLine(scenePos, scenePos);
        setCommandStage(QStringLiteral("Waiting for radius point")); // 等待半径点
        updateStatus(QStringLiteral("2D circle center"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Arc)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        addPreviewLine(scenePos, scenePos);
        setCommandStage(QStringLiteral("Waiting for arc radius point")); // 等待弧半径点
        updateStatus(QStringLiteral("2D arc center"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::BoxSelect)
    {
        beginBoxSelect(scenePos);
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Move)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        setCommandStage(QStringLiteral("Select move target")); // 选择移动目标点
        updateStatus(QStringLiteral("2D move anchor"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Copy)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        setCommandStage(QStringLiteral("Select copy target")); // 选择复制目标点
        updateStatus(QStringLiteral("2D copy anchor"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Rotate)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        setCommandStage(QStringLiteral("Select rotate target")); // 选择旋转目标点
        updateStatus(QStringLiteral("2D rotate anchor"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Mirror)
    {
        m_toolContext.hasDrawStart = true;
        m_drawStartPoint = scenePos;
        setCommandStage(QStringLiteral("Select mirror base point")); // 选择镜像基准点
        updateStatus(QStringLiteral("2D mirror anchor"));
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Trim)
    {
        trimSelectedByPoint(scenePos);
        setCommandStage(QStringLiteral("Select trim target")); // 修剪目标选择
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Extend)
    {
        extendSelectedByPoint(scenePos);
        setCommandStage(QStringLiteral("Select extend target")); // 延伸目标选择
        return;
    }
    if (event->button() == Qt::LeftButton)
        setSelectedFromHitTest(scenePos);
    QGraphicsView::mousePressEvent(event);
}

void CanvasViewport2D::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));
    if (m_panning)
    {
        const QPointF delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - static_cast<int>(delta.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - static_cast<int>(delta.y()));
        return;
    }
    if (m_toolContext.tool == ToolContext::DrawTool::BoxSelect && m_toolContext.boxSelecting)
    {
        updateBoxSelect(scenePos); return;
    }
    if (m_toolContext.tool == ToolContext::DrawTool::Line && m_toolContext.hasDrawStart)
    {
        addPreviewLine(m_drawStartPoint, scenePos); setCommandStage(QStringLiteral("Line preview")); updateStatus(QStringLiteral("2D line preview")); return;
    } // 线段预览中
    if (m_toolContext.tool == ToolContext::DrawTool::Circle && m_toolContext.hasDrawStart)
    {
        addPreviewLine(m_drawStartPoint, scenePos); setCommandStage(QStringLiteral("Circle preview")); updateStatus(QStringLiteral("2D circle preview")); return;
    } // 圆预览中
    if (m_toolContext.tool == ToolContext::DrawTool::Arc && m_toolContext.hasDrawStart)
    {
        addPreviewLine(m_drawStartPoint, scenePos); setCommandStage(QStringLiteral("Arc preview")); updateStatus(QStringLiteral("2D arc preview")); return;
    } // 弧预览中
    if ((m_toolContext.tool == ToolContext::DrawTool::Move || m_toolContext.tool == ToolContext::DrawTool::Copy || m_toolContext.tool == ToolContext::DrawTool::Rotate || m_toolContext.tool == ToolContext::DrawTool::Mirror) && m_toolContext.hasDrawStart)
    {
        setCommandStage(QStringLiteral("Transform preview")); applySelectionTransform(m_drawStartPoint, scenePos, QStringLiteral("transform")); return;
    } // 变换预览中
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasViewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_panning)
    {
        m_panning = false;
        unsetCursor();
        updateStatus(QStringLiteral("2D pan end"));
        return;
    }
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Line && m_toolContext.hasDrawStart)
    {
        commitLine(m_drawStartPoint, scenePos);
        m_toolContext.hasDrawStart = false;
        setCommandStage(QStringLiteral("Submit completed")); // 提交完成
        finishCommand(true);
        updateStatus(QStringLiteral("2D line committed"));
        if (m_stateCenter) m_stateCenter->setDirty(true);
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Circle && m_toolContext.hasDrawStart)
    {
        const double radius = QLineF(m_drawStartPoint, scenePos).length();
        commitCircle(m_drawStartPoint, radius);
        m_toolContext.hasDrawStart = false;
        setCommandStage(QStringLiteral("Submit completed")); // 提交完成
        finishCommand(true);
        updateStatus(QStringLiteral("2D circle committed"));
        if (m_stateCenter) m_stateCenter->setDirty(true);
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::Arc && m_toolContext.hasDrawStart)
    {
        const double radius = QLineF(m_drawStartPoint, scenePos).length();
        commitArc(m_drawStartPoint, radius, 0.0, 90.0);
        m_toolContext.hasDrawStart = false;
        setCommandStage(QStringLiteral("Submit completed")); // 提交完成
        finishCommand(true);
        updateStatus(QStringLiteral("2D arc committed"));
        if (m_stateCenter) m_stateCenter->setDirty(true);
        return;
    }
    if (event->button() == Qt::LeftButton && m_toolContext.tool == ToolContext::DrawTool::BoxSelect && m_toolContext.boxSelecting)
    {
        endBoxSelect(scenePos);
        return;
    }
    if (event->button() == Qt::LeftButton && (m_toolContext.tool == ToolContext::DrawTool::Move || m_toolContext.tool == ToolContext::DrawTool::Copy || m_toolContext.tool == ToolContext::DrawTool::Rotate || m_toolContext.tool == ToolContext::DrawTool::Mirror) && m_toolContext.hasDrawStart)
    {
        applySelectionTransform(m_drawStartPoint, scenePos, QStringLiteral("transform"));
        m_toolContext.hasDrawStart = false;
        setCommandStage(QStringLiteral("Submit completed")); // 提交完成
        finishCommand(true);
        m_toolContext.transformCopy = false;
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void Viewport3D::rebuildTreeHighlight()
{
    // 通过根节点递归找路径，这样可以把真正的层级路径展示出来
    m_selectedPathNames.clear();

    if (!m_document || m_selectedNodeId.isEmpty())
        return;

    const auto node = m_document->nodeById(m_selectedNodeId);
    if (!node)
        return;

    m_selectedPathNames = node->pathNamesRecursive();

    // 路径变化后刷新视图，让路径文本和高亮同步
    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
    if (m_statusCallback)
        m_statusCallback(QStringLiteral("3D path: %1").arg(m_selectedPathNames.join(QStringLiteral(" / "))));
    if (m_statusCallback)
        m_statusCallback(QStringLiteral("3D selected: %1").arg(m_selectedNodeId));
    update();
}

SceneTreeDockWidget::SceneTreeDockWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Type") });
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item)
            return;
        const QString nodeId = item->data(0, Qt::UserRole).toString();
        selectPathParents(nodeId);
        highlightPathInTree(nodeId);
        if (m_selectionCallback)
            m_selectionCallback(nodeId);
        emit nodeActivated(nodeId);
        });
}

void SceneTreeDockWidget::setSceneDocument(SceneDocument3D* document)
{
    m_document = document;
    refresh();
}

void SceneTreeDockWidget::setSelectionCallback(std::function<void(const QString&)>&& callback)
{
    m_selectionCallback = std::move(callback);
}

void SceneTreeDockWidget::setPathCallback(std::function<void(const QStringList&)>&& callback)
{
    m_pathCallback = std::move(callback);
}

void SceneTreeDockWidget::refresh()
{
    rebuildTree();
    if (!m_tree)
        return;

    const auto currentId = currentNodeId();
    if (!currentId.isEmpty())
    {
        selectPathParents(currentId);
        highlightPathInTree(currentId);
    }
}

QString SceneTreeDockWidget::currentNodeId() const
{
    if (!m_tree || !m_tree->currentItem())
        return {};
    return m_tree->currentItem()->data(0, Qt::UserRole).toString();
}

void SceneTreeDockWidget::rebuildTree()
{
    if (!m_tree)
        return;

    m_tree->clear();
    if (!m_document)
        return;

    for (const auto& node : m_document->rootNodes())
    {
        if (!node)
            continue;
        auto* item = new QTreeWidgetItem(m_tree, { node->name(), QStringLiteral("Node") });
        item->setData(0, Qt::UserRole, node->id());
        for (const auto& child : node->children())
            addNodeItem(item, child);
    }

    m_tree->expandAll();
}

void SceneTreeDockWidget::addNodeItem(QTreeWidgetItem* parent, const std::shared_ptr<SceneNode>& node)
{
    if (!parent || !node)
        return;

    auto* item = new QTreeWidgetItem(parent, { node->name(), QStringLiteral("Node") });
    item->setData(0, Qt::UserRole, node->id());
    for (const auto& child : node->children())
        addNodeItem(item, child);
}

void SceneTreeDockWidget::highlightPathInTree(const QString& nodeId)
{
    if (!m_tree)
        return;

    const auto items = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
    for (auto* item : items)
    {
        if (!item)
            continue;

        const bool matched = item->data(0, Qt::UserRole).toString() == nodeId;
        item->setSelected(matched);
        item->setBackground(0, matched ? QColor(80, 180, 255, 120) : QColor());
    }
}

void SceneTreeDockWidget::selectPathParents(const QString& nodeId)
{
    // 展开目标节点所在的父级链，避免树节点定位不直观
    if (!m_tree || !m_document)
        return;

    if (auto* item = findItemByNodeId(nodeId))
    {
        item->setExpanded(true);
        auto* parent = item->parent();
        while (parent)
        {
            parent->setExpanded(true);
            parent = parent->parent();
        }
        m_tree->setCurrentItem(item);
    }
}

QTreeWidgetItem* SceneTreeDockWidget::findItemByNodeId(const QString& nodeId) const
{
    if (!m_tree)
        return nullptr;

    const auto items = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
    for (auto* item : items)
    {
        if (item && item->data(0, Qt::UserRole).toString() == nodeId)
            return item;
    }
    return nullptr;
}

PropertiesPanelWidget::PropertiesPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ QStringLiteral("Field"), QStringLiteral("Value") });
    layout->addWidget(m_tree);
}

void PropertiesPanelWidget::setEntityDocument(EntityDocument2D* document)
{
    m_entityDocument = document; refresh();
}
void PropertiesPanelWidget::setSceneDocument(SceneDocument3D* document)
{
    m_sceneDocument = document; refresh();
}
void PropertiesPanelWidget::setStateText(const QString& text)
{
    m_stateText = text; refresh();
}
void PropertiesPanelWidget::setSelectionText(const QString& text)
{
    m_selectionText = text; refresh();
}
void PropertiesPanelWidget::setObjectDetails(const QString& title, const QStringList& lines)
{
    m_objectTitle = title; m_objectLines = lines; refresh();
}

void PropertiesPanelWidget::refresh()
{
    if (m_tree)
        m_tree->clear();
    syncText();
}

void PropertiesPanelWidget::syncText()
{
    if (!m_tree)
        return;

    new QTreeWidgetItem(m_tree, { QStringLiteral("State"), m_stateText });
    new QTreeWidgetItem(m_tree, { QStringLiteral("Selection"), m_selectionText });
    new QTreeWidgetItem(m_tree, { QStringLiteral("Object"), m_objectTitle });
    new QTreeWidgetItem(m_tree, { QStringLiteral("2D Doc"), m_entityDocument ? QStringLiteral("Ready") : QStringLiteral("None") });
    new QTreeWidgetItem(m_tree, { QStringLiteral("3D Doc"), m_sceneDocument ? QStringLiteral("Ready") : QStringLiteral("None") });
    for (const auto& line : m_objectLines)
        new QTreeWidgetItem(m_tree, { QStringLiteral("Detail"), line });
}

Viewport3D::Viewport3D(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(640, 480);
    setAutoFillBackground(true);
}

void Viewport3D::setStatusCallback(std::function<void(const QString&)>&& callback)
{
    m_statusCallback = std::move(callback);
}

void Viewport3D::setSceneDocument(SceneDocument3D* document)
{
    m_document = document;
}

void Viewport3D::setCameraController(CameraController3D* controller)
{
    m_cameraController = controller;
}

void Viewport3D::setSelectionCallback(std::function<void(const QString&)>&& callback)
{
    m_selectionCallback = std::move(callback);
}

void Viewport3D::setPathCallback(std::function<void(const QStringList&)>&& callback)
{
    m_pathCallback = std::move(callback);
}

void Viewport3D::resetCamera()
{
    if (m_cameraController)
        m_cameraController->reset();
    m_yaw = 0.0;
    m_pitch = 15.0;
    m_distance = 10.0;
    update();
}

void Viewport3D::setOrbitMode(bool enabled)
{
    m_orbitMode = enabled;
}

void Viewport3D::setMeasureMode(bool enabled)
{
    m_measureMode = enabled;
}

QString Viewport3D::selectedNodeId() const
{
    return m_selectedNodeId;
}

QStringList Viewport3D::selectedPathNames() const
{
    return m_selectedPathNames;
}

void Viewport3D::selectNodeById(const QString& nodeId)
{
    m_selectedNodeId = nodeId;
    if (m_document)
    {
        m_document->selection().clear();
        if (auto node = m_document->nodeById(nodeId))
            m_document->selection().add(node);
    }
    rebuildTreeHighlight();
    if (m_selectionCallback)
        m_selectionCallback(nodeId);
    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
    if (m_statusCallback)
        m_statusCallback(QStringLiteral("3D selected: %1").arg(nodeId));
    update();
}

void Viewport3D::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);
}
void Viewport3D::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
}
void Viewport3D::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}
void Viewport3D::wheelEvent(QWheelEvent* event)
{
    QWidget::wheelEvent(event);
}
void Viewport3D::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
}
void Viewport3D::contextMenuEvent(QContextMenuEvent* event)
{
    QWidget::contextMenuEvent(event);
}

void Viewport3D::emitStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}

void Viewport3D::drawAxes(QPainter& painter)
{
    Q_UNUSED(painter);
}
void Viewport3D::drawWireCube(QPainter& painter)
{
    Q_UNUSED(painter);
}
void Viewport3D::drawNodePathOverlay(QPainter& painter) const
{
    Q_UNUSED(painter);
}