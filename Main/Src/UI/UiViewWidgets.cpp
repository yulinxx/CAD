#include "UiViewWidgets.h"

#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include "SceneDocument2D.h"
#include "UiCommandDispatcher.h"
#include "UiInteractionDispatcher.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/Core/SceneManager.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

namespace
{
    constexpr double kGridStep = 50.0;
    constexpr double kHitRadius = 10.0;
    constexpr double kLineHitTolerance = 8.0;

    // 将活动命令的鼠标事件转发给交互分发器
    // 传入场景坐标（而非视口坐标），确保命令系统拿到的是世界坐标系下的点位
    bool forwardActiveCommand(IInteractionDispatcher* dispatcher, const QPointF& scenePos,
        bool (IInteractionDispatcher::* forwardFn)(int, int))
    {
        if (!dispatcher || !dispatcher->hasActiveCommand())
            return false;

        return (dispatcher->*forwardFn)(static_cast<int>(scenePos.x()), static_cast<int>(scenePos.y()));
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
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle"));
    updateStatus(enabled ? tr("2D draw mode") : tr("2D select mode"));
}

void Viewport2D::setMeasureMode(bool enabled)
{
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle"));
    updateStatus(enabled ? tr("2D measure mode") : tr("2D select mode"));
}

void Viewport2D::resetView()
{
    resetTransform();
    centerOn(0, 0);
    updateStatus(tr("2D view reset")); // 2D 视图重置
}

void Viewport2D::zoomToFit()
{
    const QRectF bounds = documentBounds();
    if (bounds.isNull() || bounds.isEmpty())
    {
        resetView();
        return;
    }

    constexpr double margin = 50.0;
    QRectF padded = bounds.adjusted(-margin, -margin, margin, margin);
    fitInView(padded, Qt::KeepAspectRatio);
    updateStatus(tr("2D zoom extents"));
}

void Viewport2D::setPanModeEnabled(bool enabled)
{
    m_panModeEnabled = enabled;
    setDragMode(enabled ? QGraphicsView::ScrollHandDrag : QGraphicsView::RubberBandDrag);
    updateStatus(enabled ? tr("2D pan mode") : tr("2D select mode"));
}

QRectF Viewport2D::documentBounds() const
{
    if (!m_document)
        return {};

    auto* sm = m_document->sceneManager();
    if (!sm)
        return {};

    QRectF bounds;
    for (const auto* entity : sm->getAllEntities())
    {
        if (!entity)
            continue;

        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
                bounds |= QRectF(pt.x(), pt.y(), 0, 0);
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<const Eg::SyCircle*>(entity);
            const auto& c = entity->basePoint;
            const double r = circle->dRadius;
            bounds |= QRectF(c.x() - r, c.y() - r, r * 2.0, r * 2.0);
        }
        else if (entity->eType == Eg::EType::ARC)
        {
            auto* arc = static_cast<const Eg::SyArc*>(entity);
            const auto& c = entity->basePoint;
            const double r = arc->dRadius;
            bounds |= QRectF(c.x() - r, c.y() - r, r * 2.0, r * 2.0);
        }
        else if (entity->eType == Eg::EType::POLYGON)
        {
            auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
            for (const auto& v : polygon->vVertices)
                bounds |= QRectF(v.x(), v.y(), 0, 0);
        }
    }

    return bounds;
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
    // 清除其他类型的预览
    if (m_previewEllipse)
    {
        m_scene->removeItem(m_previewEllipse);
        delete m_previewEllipse;
        m_previewEllipse = nullptr;
    }
    for (auto* item : m_previewPolylineItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPolylineItems.clear();

    if (!m_previewLine)
    {
        m_previewLine = m_scene->addLine(QLineF(start, end), QPen(QColor(80, 180, 255), 2, Qt::DashLine));
        return;
    }
    m_previewLine->setLine(QLineF(start, end));
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
                // 渲染所有连续线段（支持多段折线）
                for (size_t i = 1; i < line->vPoints.size(); ++i)
                {
                    const auto& p0 = line->vPoints[i - 1];
                    const auto& p1 = line->vPoints[i];
                    m_scene->addLine(QLineF(QPointF(p0.x(), p0.y()), QPointF(p1.x(), p1.y())), QPen(lineColor, lineWidth));
                }
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
            QPainterPath arcPath;
            QRectF arcRect(c.x() - r, c.y() - r, r * 2.0, r * 2.0);
            double startDeg = arc->dStartAngle * 180.0 / M_PI;
            double endDeg = arc->dEndAngle * 180.0 / M_PI;
            double sweepDeg = endDeg - startDeg;
            if (sweepDeg < 0)
                sweepDeg += 360.0;
            arcPath.arcMoveTo(arcRect, startDeg);
            arcPath.arcTo(arcRect, startDeg, sweepDeg);
            m_scene->addPath(arcPath, QPen(QColor(255, 170, 120), 2));
        }
        else if (entity->eType == Eg::EType::POLYGON)
        {
            // 多边形：连接所有顶点并闭合
            auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
            if (polygon->vVertices.size() >= 2)
            {
                QPolygonF qpoly;
                for (const auto& v : polygon->vVertices)
                    qpoly.append(QPointF(v.x(), v.y()));
                if (polygon->bClosed && !qpoly.isClosed())
                    qpoly.append(qpoly.first());
                m_scene->addPolygon(qpoly, QPen(lineColor, lineWidth));
            }
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
    auto ids = m_document->selectedIdsQ();
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

    setCommandStage(tr("Idle"));
    if (m_selectionCallback)
        m_selectionCallback(committed ? tr("2D-Commit") : tr("2D-Cancel"), QString()); // 2D 提交 / 2D 取消
}

void Viewport2D::setCommandStage(const QString& stage)
{
    if (m_commandStageCallback)
        m_commandStageCallback(stage);
}

void Viewport2D::beginBoxSelect(const QPointF& scenePos)
{
    m_boxSelecting = true;
    m_boxSelectStart = scenePos;
}

void Viewport2D::updateBoxSelect(const QPointF& scenePos)
{
    if (!m_boxSelecting)
        return;
    const QRectF rect(m_boxSelectStart, scenePos);
    m_scene->addRect(rect.normalized(), QPen(QColor(80, 180, 255), 1, Qt::DashLine));
}

void Viewport2D::endBoxSelect(const QPointF& scenePos)
{
    if (!m_document || !m_boxSelecting)
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

    m_boxSelecting = false;
    finishCommand(true);
    updateStatus(tr("2D box select end"));
    refreshSelectionStyle();
}

void Viewport2D::wheelEvent(QWheelEvent* event)
{
    // Ctrl+滚轮优先转发给活动命令（如多边形调整边数）
    if (event->modifiers() & Qt::ControlModifier)
    {
        if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
        {
            auto handler = m_interactionDispatcher->currentHandler();
            if (handler && handler->onWheel(event->angleDelta().y()))
            {
                updateCommandPreview();
                updateStatus(tr("2D command wheel"));
                return;
            }
        }
    }

    const double factor = event->angleDelta().y() > 0 ? 1.15 : 0.87;
    scale(factor, factor);
    updateStatus(tr("2D zoom"));
}

void Viewport2D::keyPressEvent(QKeyEvent* event)
{
    // 键盘事件优先转发给活动命令（如 Esc 取消、Enter 确认）
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        if (m_interactionDispatcher->forwardKeyPress(event->key()))
        {
            // 命令可能在按键后完成并提交（如 Enter 确认）
            if (!m_interactionDispatcher->hasActiveCommand())
            {
                refreshFromDocument();
                updateCommandPreview();
            }
            return;
        }
    }

    // Esc：取消活动命令
    if (event->key() == Qt::Key_Escape)
    {
        if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
        {
            m_interactionDispatcher->cancel();
            updateCommandPreview();
            return;
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void Viewport2D::mouseDoubleClickEvent(QMouseEvent* event)
{
    // 双击事件处理：对于交互式命令，双击可作为提交结束信号
    // Polyline 等命令已通过 Enter 键或命令内部逻辑处理结束，此处仅保留基础转发
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));

    // 如果有活动命令，将双击视为结束命令的信号
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        // 调用 submit 结束当前命令
        m_interactionDispatcher->submit();
        updateCommandPreview();
        refreshFromDocument();
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
    auto* drawPolygon = menu.addAction(tr("Draw Polygon")); // 绘制多边形
    auto* drawBezier2 = menu.addAction(tr("Draw Bezier2")); // 二阶贝塞尔
    auto* drawBezier = menu.addAction(tr("Draw Bezier")); // 三阶贝塞尔
    auto* drawNurbs = menu.addAction(tr("Draw NURBS")); // NURBS曲线
    auto* drawSmartLine = menu.addAction(tr("Draw SmartLine")); // 复合图元
    menu.addSeparator();
    auto* move = menu.addAction(tr("Move")); // 移动
    auto* copy = menu.addAction(tr("Copy")); // 复制
    auto* rotate = menu.addAction(tr("Rotate")); // 旋转
    auto* mirror = menu.addAction(tr("Mirror")); // 镜像
    auto* deleteEntity = menu.addAction(tr("Delete")); // 删除
    auto* selectEntity = menu.addAction(tr("Select")); // 选择

    QAction* chosen = menu.exec(event->globalPos());
    // 统一通过 commandDispatcher 触发命令（阶段 3：消除旧工具路径）
    if (!chosen)
        return;

    auto triggerCommand = [this](const QString& commandId) {
        if (m_commandDispatcher)
            m_commandDispatcher->execute(commandId);
        };

    if (chosen == drawLine)       triggerCommand(QStringLiteral("2d.draw_line"));
    else if (chosen == drawPolyline) triggerCommand(QStringLiteral("2d.draw_polyline"));
    else if (chosen == drawCircle)   triggerCommand(QStringLiteral("2d.draw_circle"));
    else if (chosen == drawArc)      triggerCommand(QStringLiteral("2d.draw_arc"));
    else if (chosen == drawPolygon)  triggerCommand(QStringLiteral("2d.draw_polygon"));
    else if (chosen == drawBezier2)  triggerCommand(QStringLiteral("2d.draw_bezier2"));
    else if (chosen == drawBezier)   triggerCommand(QStringLiteral("2d.draw_bezier"));
    else if (chosen == drawNurbs)    triggerCommand(QStringLiteral("2d.draw_nurbs"));
    else if (chosen == drawSmartLine) triggerCommand(QStringLiteral("2d.draw_smartline"));
    else if (chosen == move)         triggerCommand(QStringLiteral("2d.move"));
    else if (chosen == copy)         triggerCommand(QStringLiteral("2d.copy"));
    else if (chosen == rotate)       triggerCommand(QStringLiteral("2d.rotate"));
    else if (chosen == mirror)       triggerCommand(QStringLiteral("2d.mirror"));
    else if (chosen == deleteEntity) triggerCommand(QStringLiteral("2d.delete"));
    else if (chosen == selectEntity) triggerCommand(QStringLiteral("2d.select"));
}

void Viewport2D::updateCommandPreview()
{
    // 通过 ICommandHandler::preview() 通用接口获取预览数据
    // 视口不再依赖具体命令类（如 DrawLineCommand）
    // 后续 OperationBus 中的 IOperation 也可通过此接口提供预览
    if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
    {
        clearPreviewItems();
        return;
    }

    auto handler = m_interactionDispatcher->currentHandler();
    if (!handler)
    {
        clearPreviewItems();
        return;
    }

    CommandPreview preview = handler->preview();
    if (!preview.valid)
    {
        clearPreviewItems();
        return;
    }

    // 根据预览类型选择对应的绘制方式
    switch (preview.type)
    {
        case PreviewType::Line:
            addPreviewLine(preview.previewStart, preview.previewEnd);
            break;
        case PreviewType::Circle:
            addPreviewCircle(preview.previewCenter, preview.previewRadius);
            break;
        case PreviewType::Arc:
        {
            QVector<QPointF> arcPreviewPts;
            arcPreviewPts.append(preview.previewCenter);
            arcPreviewPts.append(preview.previewPoints.value(0));
            arcPreviewPts.append(preview.previewPoints.value(1));
            addPreviewPolyline(arcPreviewPts);
            break;
        }
        case PreviewType::Polyline:
            addPreviewPolyline(preview.previewPoints);
            break;
        case PreviewType::Polygon:
            addPreviewPolyline(preview.previewPoints);
            break;
        case PreviewType::Bezier2:
            addPreviewBezier(preview.previewPoints, preview.controlPoints);
            break;
        case PreviewType::Bezier:
            addPreviewBezier(preview.previewPoints, preview.controlPoints);
            break;
        case PreviewType::Nurbs:
            addPreviewPolyline(preview.controlPoints);
            break;
        case PreviewType::SmartLine:
            addPreviewPolyline(preview.previewPoints);
            break;
        default:
            clearPreviewItems();
            break;
    }
}

void Viewport2D::clearPreviewItems()
{
    if (m_previewLine)
    {
        m_scene->removeItem(m_previewLine);
        delete m_previewLine;
        m_previewLine = nullptr;
    }
    if (m_previewEllipse)
    {
        m_scene->removeItem(m_previewEllipse);
        delete m_previewEllipse;
        m_previewEllipse = nullptr;
    }
    for (auto* item : m_previewPolylineItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPolylineItems.clear();
    for (auto* item : m_previewPathItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPathItems.clear();
}

void Viewport2D::addPreviewCircle(const QPointF& center, double radius)
{
    // 清除旧的线预览和多段线预览，保留椭圆预览
    if (m_previewLine)
    {
        m_scene->removeItem(m_previewLine);
        delete m_previewLine;
        m_previewLine = nullptr;
    }
    for (auto* item : m_previewPolylineItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPolylineItems.clear();

    if (radius < 0.5)
        radius = 0.5;

    QRectF rect(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
    if (!m_previewEllipse)
    {
        m_previewEllipse = m_scene->addEllipse(rect, QPen(QColor(80, 180, 255, 180), 2, Qt::DashLine));
    }
    else
    {
        m_previewEllipse->setRect(rect);
    }
}

void Viewport2D::addPreviewPolyline(const QVector<QPointF>& points)
{
    // 清除旧的线预览和椭圆预览
    if (m_previewLine)
    {
        m_scene->removeItem(m_previewLine);
        delete m_previewLine;
        m_previewLine = nullptr;
    }
    if (m_previewEllipse)
    {
        m_scene->removeItem(m_previewEllipse);
        delete m_previewEllipse;
        m_previewEllipse = nullptr;
    }

    // 清除旧的多段线预览
    for (auto* item : m_previewPolylineItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPolylineItems.clear();

    // 绘制新的多段线预览
    for (int i = 0; i < points.size() - 1; ++i)
    {
        auto* item = m_scene->addLine(QLineF(points[i], points[i + 1]),
            QPen(QColor(80, 180, 255, 180), 2, Qt::DashLine));
        m_previewPolylineItems.append(item);
    }
}

void Viewport2D::addPreviewBezier(const QVector<QPointF>& endpoints, const QVector<QPointF>& controlPoints)
{
    // 清除旧的线预览和椭圆预览
    if (m_previewLine)
    {
        m_scene->removeItem(m_previewLine);
        delete m_previewLine;
        m_previewLine = nullptr;
    }
    if (m_previewEllipse)
    {
        m_scene->removeItem(m_previewEllipse);
        delete m_previewEllipse;
        m_previewEllipse = nullptr;
    }

    // 清除旧的多段线预览
    for (auto* item : m_previewPolylineItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPolylineItems.clear();

    // 清除旧的贝塞尔路径预览
    for (auto* item : m_previewPathItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_previewPathItems.clear();

    if (endpoints.size() < 2)
        return;

    const QPointF& start = endpoints[0];
    const QPointF& end = endpoints[1];

    // 绘制控制线（虚线）
    QPen controlPen(QColor(255, 100, 100, 150), 1, Qt::DashLine);
    for (const auto& cp : controlPoints)
    {
        auto* cLine1 = m_scene->addLine(QLineF(start, cp), controlPen);
        auto* cLine2 = m_scene->addLine(QLineF(cp, end), controlPen);
        m_previewPolylineItems.append(cLine1);
        m_previewPolylineItems.append(cLine2);
    }

    // 绘制贝塞尔曲线（实线）
    QPen curvePen(QColor(80, 180, 255, 200), 2, Qt::SolidLine);
    QPainterPath path;
    path.moveTo(start);

    if (controlPoints.size() == 1)
    {
        // 二阶贝塞尔
        path.quadTo(controlPoints[0], end);
    }
    else if (controlPoints.size() >= 2)
    {
        // 三阶贝塞尔
        path.cubicTo(controlPoints[0], controlPoints[1], end);
    }

    auto* pathItem = m_scene->addPath(path, curvePen);
    m_previewPathItems.append(pathItem);
}

void Viewport2D::mousePressEvent(QMouseEvent* event)
{
    // 事件路由优先级（OperationBus 为主线）
    // 1. 活动操作 → 转发给 IInteractionDispatcher（过渡期兼容层，后续统一到 OperationBus）
    // 2. 空闲态基础交互 → 中键平移、左键选择
    // 3. 旧工具路径 → 过渡期兜底，后续逐步迁移到 OperationBus

    const QPointF scenePos = snapPoint(mapToScene(event->pos()));

    // 优先级1：活动操作优先（过渡期通过 IInteractionDispatcher 转发）
    if (forwardActiveCommand(m_interactionDispatcher, scenePos, &IInteractionDispatcher::forwardMouseDown))
    {
        updateCommandPreview();
        // 命令可能在 onMouseDown 后完成并提交（如两点画线），此时需要刷新文档显示
        if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
            refreshFromDocument();
        return;
    }

    // 优先级2：空闲态基础交互
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        updateStatus(tr("2D pan start")); // 2D 平移开始
        return;
    }

    // 空闲态左键选择（无活动命令时触发）
    if (event->button() == Qt::LeftButton
        && (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand()))
    {
        setSelectedFromHitTest(scenePos);
        updateStatus(tr("2D select"));
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void Viewport2D::mouseMoveEvent(QMouseEvent* event)
{
    // 事件路由优先级同 mousePressEvent（OperationBus 为主线）
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));

    if (forwardActiveCommand(m_interactionDispatcher, scenePos, &IInteractionDispatcher::forwardMouseMove))
    {
        updateCommandPreview();
        return;
    }

    // 优先级2：中键平移
    if (m_panning)
    {
        const QPointF delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - static_cast<int>(delta.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - static_cast<int>(delta.y()));
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void Viewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    // 事件路由优先级（OperationBus 为主线）
    const QPointF scenePos = snapPoint(mapToScene(event->pos()));

    if (forwardActiveCommand(m_interactionDispatcher, scenePos, &IInteractionDispatcher::forwardMouseUp))
    {
        // 命令可能在 onMouseUp 后完成并提交，此时需要刷新文档显示
        if (!m_interactionDispatcher || !m_interactionDispatcher->hasActiveCommand())
            refreshFromDocument();
        return;
    }

    // 优先级2：中键平移结束
    if (event->button() == Qt::MiddleButton && m_panning)
    {
        m_panning = false;
        unsetCursor();
        updateStatus(tr("2D pan end")); // 2D 平移结束
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}