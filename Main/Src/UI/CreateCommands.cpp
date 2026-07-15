#include "CreateCommands.h"

#include <QObject>
#include <cmath>

#include "SceneDocument2D.h"
#include "ISelectionService.h"
#include "UiServices.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"
#include "Mat/Mat.hpp"
#include "CommandSnapshots.h"
#include "Log/SyLogger.h"

namespace
{
    // 将QPointF转换为Ut::Vec2d
    inline Ut::Vec2d toVec2d(const QPointF& p)
    {
        return Ut::Vec2d(p.x(), p.y());
    }
} // namespace

// 构造函数：初始化点拾取器为2点模式
DrawLineCommand::DrawLineCommand()
    : m_pointPicker(2)
{
}

// 返回命令ID
QString DrawLineCommand::commandId() const
{
    return QStringLiteral("2d.draw_line");
}

// 返回命令显示名称
QString DrawLineCommand::displayName() const
{
    return QObject::tr("Draw Line");
}

// 判断是否为交互式命令
bool DrawLineCommand::isInteractive() const
{
    return true;
}

// 获取当前命令状态
CommandState DrawLineCommand::state() const
{
    return m_state;
}

// 激活命令：初始化状态并激活点拾取器
bool DrawLineCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] DrawLineCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_pointPicker.activate(services);
    m_previewStart = QPointF();
    m_previewEnd = QPointF();
    m_createdEntityId.clear();
    return true;
}

// 取消命令：重置点拾取器并标记为已取消
void DrawLineCommand::cancel()
{
    SY_INFO("[CreateCommands] DrawLineCommand cancelled");
    m_pointPicker.cancel();
    m_state = CommandState::Cancelled;
}

// 提交命令：标记为已提交，并选中创建的实体
void DrawLineCommand::commit()
{
    SY_INFOF("[CreateCommands] DrawLineCommand committed: entity=%s", m_createdEntityId.toUtf8().constData());
    m_state = CommandState::Committed;
    if (m_document && !m_createdEntityId.isEmpty())
        m_selectionService->select(m_createdEntityId.toStdString());
}

// 重置命令状态到初始状态
void DrawLineCommand::reset()
{
    m_pointPicker.reset();
    m_state = CommandState::Idle;
    m_previewStart = QPointF();
    m_previewEnd = QPointF();
    m_createdEntityId.clear();
}

// 鼠标按下事件：通过点拾取器收集点，收集完成后创建线段
bool DrawLineCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_pointPicker.onMouseDown(x, y);

    const auto& points = m_pointPicker.pickedPoints();
    if (points.size() >= 1)
    {
        m_previewStart = points[0];
        if (points.size() >= 2)
        {
            m_previewEnd = points[1];
            if (m_document)
                m_createdEntityId = m_document->createLine(m_previewStart, m_previewEnd);
            commit();
        }
    }
    return true;
}

// 鼠标移动事件：更新预览终点
bool DrawLineCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_previewEnd = QPointF(x, y);
    return true;
}

// 键盘事件：Esc键取消命令
bool DrawLineCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }
    return false;
}

// 返回当前激活的工具
ITool* DrawLineCommand::activeTool() const
{
    return const_cast<PointPickerTool*>(&m_pointPicker);
}

// 创建撤销命令
UndoCommand* DrawLineCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    return new LineUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

// 判断命令是否完成
bool DrawLineCommand::isComplete() const
{
    return m_createdEntityId.isEmpty() == false;
}

// 获取命令预览数据
CommandPreview DrawLineCommand::preview() const
{
    CommandPreview p;
    if (m_previewStart.isNull() == false && m_previewEnd.isNull() == false)
    {
        p.valid = true;
        p.type = PreviewType::Line;
        p.previewPoints.append(m_previewStart);
        p.previewPoints.append(m_previewEnd);
    }
    return p;
}

// 设置文档引用
void DrawLineCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// CircleCommand - 绘制圆命令

CircleCommand::CircleCommand()
{
}

QString CircleCommand::commandId() const
{
    return QStringLiteral("2d.draw_circle");
}

QString CircleCommand::displayName() const
{
    return QObject::tr("Draw Circle");
}

bool CircleCommand::isInteractive() const
{
    return true;
}

CommandState CircleCommand::state() const
{
    return m_state;
}

// 激活命令：初始化状态
bool CircleCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] CircleCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_center = QPointF();
    m_endPoint = QPointF();
    m_hasCenter = false;
    m_createdEntityId.clear();
    return true;
}

void CircleCommand::cancel()
{
    SY_INFO("[CreateCommands] CircleCommand cancelled");
    m_state = CommandState::Cancelled;
}

void CircleCommand::commit()
{
    SY_INFOF("[CreateCommands] CircleCommand committed: entity=%s", m_createdEntityId.toUtf8().constData());
    m_state = CommandState::Committed;
}

void CircleCommand::reset()
{
    m_state = CommandState::Idle;
    m_center = QPointF();
    m_endPoint = QPointF();
    m_hasCenter = false;
    m_createdEntityId.clear();
}

// 鼠标按下事件：第一点确定圆心，第二点确定半径
bool CircleCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (!m_hasCenter)
    {
        m_center = QPointF(x, y);
        m_hasCenter = true;
    }
    else
    {
        m_endPoint = QPointF(x, y);
        const double radius = std::hypot(m_endPoint.x() - m_center.x(), m_endPoint.y() - m_center.y());
        if (m_document && radius > 0)
            m_createdEntityId = m_document->createCircle(m_center, radius);
        commit();
    }
    return true;
}

// 鼠标移动事件：更新终点用于预览
bool CircleCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_hasCenter)
        m_endPoint = QPointF(x, y);
    return true;
}

bool CircleCommand::onMouseUp(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    return false;
}

// 键盘事件：Esc键取消命令
bool CircleCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }
    return false;
}

ITool* CircleCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* CircleCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    return new CircleUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

bool CircleCommand::isComplete() const
{
    return m_createdEntityId.isEmpty() == false;
}

CommandPreview CircleCommand::preview() const
{
    CommandPreview p;
    if (m_hasCenter && !m_endPoint.isNull())
    {
        p.valid = true;
        p.type = PreviewType::Circle;
        p.previewCenter = m_center;
        const double radius = std::hypot(m_endPoint.x() - m_center.x(), m_endPoint.y() - m_center.y());
        p.previewRadius = radius;
    }
    return p;
}

void CircleCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// ArcCommand - 绘制圆弧命令

ArcCommand::ArcCommand()
{
}

QString ArcCommand::commandId() const
{
    return QStringLiteral("2d.draw_arc");
}

QString ArcCommand::displayName() const
{
    return QObject::tr("Draw Arc");
}

bool ArcCommand::isInteractive() const
{
    return true;
}

CommandState ArcCommand::state() const
{
    return m_state;
}

// 激活命令：初始化状态和阶段
bool ArcCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] ArcCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_stage = 0;
    m_center = QPointF();
    m_startPoint = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
    return true;
}

void ArcCommand::cancel()
{
    SY_INFO("[CreateCommands] ArcCommand cancelled");
    m_state = CommandState::Cancelled;
}

void ArcCommand::commit()
{
    SY_INFOF("[CreateCommands] ArcCommand committed: entity=%s", m_createdEntityId.toUtf8().constData());
    m_state = CommandState::Committed;
}

void ArcCommand::reset()
{
    m_state = CommandState::Idle;
    m_stage = 0;
    m_center = QPointF();
    m_startPoint = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
}

// 鼠标按下事件：三阶段绘制圆弧（圆心→起点→终点）
bool ArcCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    const QPointF pt(x, y);

    if (m_stage == 0)
    {
        m_center = pt;
        m_stage = 1;
    }
    else if (m_stage == 1)
    {
        m_startPoint = pt;
        m_stage = 2;
    }
    else if (m_stage == 2)
    {
        m_endPoint = pt;

        const double dx = m_startPoint.x() - m_center.x();
        const double dy = m_startPoint.y() - m_center.y();
        const double radius = std::hypot(dx, dy);

        if (radius > 0 && m_document)
        {
            const double startAngle = std::atan2(dy, dx);
            const double endAngle = std::atan2(m_endPoint.y() - m_center.y(), m_endPoint.x() - m_center.x());
            m_createdEntityId = m_document->createArc(m_center, radius, startAngle, endAngle);
        }
        commit();
    }
    return true;
}

// 鼠标移动事件：更新终点用于预览
bool ArcCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage >= 2)
        m_endPoint = QPointF(x, y);
    return true;
}

// 键盘事件：Backspace回退阶段，Esc取消命令
bool ArcCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Backspace)
    {
        if (m_stage > 0)
        {
            m_stage--;
            if (m_stage == 1)
                m_endPoint = QPointF();
            else if (m_stage == 0)
                m_startPoint = QPointF();
            return true;
        }
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }

    return false;
}

ITool* ArcCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* ArcCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    auto* entity = m_document->sceneManager()->findEntityById(m_createdEntityId.toULongLong());
    if (!entity)
        return nullptr;

    return new ArcUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

bool ArcCommand::isComplete() const
{
    return m_createdEntityId.isEmpty() == false;
}

CommandPreview ArcCommand::preview() const
{
    CommandPreview p;
    if (m_stage >= 2 && !m_endPoint.isNull())
    {
        p.valid = true;
        p.type = PreviewType::Arc;
        p.previewCenter = m_center;
        const double dx = m_startPoint.x() - m_center.x();
        const double dy = m_startPoint.y() - m_center.y();
        const double radius = std::hypot(dx, dy);
        p.previewRadius = radius;
        p.previewPoints.append(m_startPoint);
        p.previewPoints.append(m_endPoint);
    }
    return p;
}

void ArcCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// PolylineCommand - 绘制多段线命令

PolylineCommand::PolylineCommand()
{
}

QString PolylineCommand::commandId() const
{
    return QStringLiteral("2d.draw_polyline");
}

QString PolylineCommand::displayName() const
{
    return QObject::tr("Draw Polyline");
}

bool PolylineCommand::isInteractive() const
{
    return true;
}

CommandState PolylineCommand::state() const
{
    return m_state;
}

bool PolylineCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] PolylineCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_points.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
    return true;
}

void PolylineCommand::cancel()
{
    SY_INFO("[CreateCommands] PolylineCommand cancelled");
    m_state = CommandState::Cancelled;
}

void PolylineCommand::commit()
{
    if (m_points.size() >= 2 && m_document)
        m_createdEntityId = m_document->createPolyline(m_points);
    SY_INFOF("[CreateCommands] PolylineCommand committed: entity=%s, points=%d",
        m_createdEntityId.toUtf8().constData(), m_points.size());
    m_state = CommandState::Committed;
}

void PolylineCommand::reset()
{
    m_state = CommandState::Idle;
    m_points.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
}

bool PolylineCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_points.append(QPointF(x, y));
    return true;
}

bool PolylineCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_currentPoint = QPointF(x, y);
    return true;
}

bool PolylineCommand::onMouseUp(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    return false;
}

// 键盘事件：Enter/空格完成绘制，Backspace删除最后一点，Esc取消
bool PolylineCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Space)
    {
        if (m_points.size() >= 2)
        {
            m_completed = true;
            commit();
        }
        return true;
    }
    else if (key == Qt::Key_Backspace && !m_points.isEmpty())
    {
        m_points.removeLast();
        return true;
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }

    return false;
}

ITool* PolylineCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* PolylineCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    auto* entity = m_document->sceneManager()->findEntityById(m_createdEntityId.toULongLong());
    if (!entity)
        return nullptr;

    EntitySnapshot snap = takeSnapshot(entity);
    return new PolylineUndoCommand(m_document, snap, m_selectionService);
}

bool PolylineCommand::isComplete() const
{
    return m_completed;
}

CommandPreview PolylineCommand::preview() const
{
    CommandPreview p;
    if (!m_points.isEmpty())
    {
        p.valid = true;
        p.type = PreviewType::Polyline;
        p.previewPoints = m_points;
        if (!m_currentPoint.isNull())
            p.previewPoints.append(m_currentPoint);
    }
    return p;
}

void PolylineCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

void PolylineCommand::finish()
{
    m_completed = true;
}

// PolygonCommand - 绘制多边形命令

PolygonCommand::PolygonCommand()
{
}

QString PolygonCommand::commandId() const
{
    return QStringLiteral("2d.draw_polygon");
}

QString PolygonCommand::displayName() const
{
    return QObject::tr("Draw Polygon");
}

bool PolygonCommand::isInteractive() const
{
    return true;
}

CommandState PolygonCommand::state() const
{
    return m_state;
}

// 激活命令：初始化状态、边数和阶段
bool PolygonCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] PolygonCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_stage = 0;
    m_sides = 6;
    m_center = QPointF();
    m_radiusPoint = QPointF();
    m_currentPoint = QPointF();
    m_previewPoints.clear();
    m_completed = false;
    m_createdEntityId.clear();
    return true;
}

void PolygonCommand::cancel()
{
    SY_INFO("[CreateCommands] PolygonCommand cancelled");
    m_state = CommandState::Cancelled;
}

void PolygonCommand::commit()
{
    if (!m_center.isNull() && !m_radiusPoint.isNull() && m_sides >= 3 && m_document)
    {
        m_createdEntityId = m_document->createPolygon(m_previewPoints);
    }
    SY_INFOF("[CreateCommands] PolygonCommand committed: entity=%s, sides=%d",
        m_createdEntityId.toUtf8().constData(), m_sides);
    m_state = CommandState::Committed;
}

void PolygonCommand::reset()
{
    m_state = CommandState::Idle;
    m_stage = 0;
    m_sides = 6;
    m_center = QPointF();
    m_radiusPoint = QPointF();
    m_currentPoint = QPointF();
    m_previewPoints.clear();
    m_completed = false;
    m_createdEntityId.clear();
}

// 更新多边形预览顶点
void PolygonCommand::updatePreviewPoints()
{
    m_previewPoints.clear();
    if (m_center.isNull() || m_radiusPoint.isNull())
        return;

    const double dx = m_radiusPoint.x() - m_center.x();
    const double dy = m_radiusPoint.y() - m_center.y();
    const double radius = std::hypot(dx, dy);
    const double startAngle = std::atan2(dy, dx) - M_PI / 2.0;
    const double stepAngle = 2.0 * M_PI / m_sides;

    for (int i = 0; i < m_sides; ++i)
    {
        const double angle = startAngle + i * stepAngle;
        m_previewPoints.append(QPointF(
            m_center.x() + radius * std::cos(angle),
            m_center.y() + radius * std::sin(angle)));
    }
}

// 鼠标按下事件：两阶段绘制多边形（中心→半径）
bool PolygonCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    const QPointF pt(x, y);

    if (m_stage == 0)
    {
        m_center = pt;
        m_stage = 1;
    }
    else if (m_stage == 1)
    {
        m_radiusPoint = pt;
        updatePreviewPoints();
        commit();
    }
    return true;
}

// 鼠标移动事件：更新半径点用于预览
bool PolygonCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage == 1)
    {
        m_radiusPoint = QPointF(x, y);
        updatePreviewPoints();
    }
    return true;
}

// 键盘事件：Enter完成绘制，Backspace回退阶段，Esc取消
bool PolygonCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Enter || key == Qt::Key_Return)
    {
        if (m_stage == 1 && !m_center.isNull() && !m_radiusPoint.isNull())
            finish();
        return true;
    }
    else if (key == Qt::Key_Backspace)
    {
        if (m_stage > 0)
        {
            m_stage--;
            if (m_stage == 0)
                m_radiusPoint = QPointF();
            return true;
        }
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }
    return false;
}

// 滚轮事件：Ctrl+滚轮调整边数
bool PolygonCommand::onWheel(int delta)
{
    if (m_state != CommandState::Active)
        return false;

    const int step = delta > 0 ? 1 : -1;
    m_sides = std::max(3, std::min(36, m_sides + step));
    SY_INFOF("[CreateCommands] PolygonCommand sides changed: %d", m_sides);
    updatePreviewPoints();
    return true;
}

ITool* PolygonCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* PolygonCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    auto* entity = m_document->sceneManager()->findEntityById(m_createdEntityId.toULongLong());
    if (!entity)
        return nullptr;

    EntitySnapshot snap = takeSnapshot(entity);
    return new PolylineUndoCommand(m_document, snap, m_selectionService);
}

bool PolygonCommand::isComplete() const
{
    return m_completed;
}

CommandPreview PolygonCommand::preview() const
{
    CommandPreview p;
    if (m_stage == 1 && !m_radiusPoint.isNull())
    {
        p.valid = true;
        p.type = PreviewType::Polygon;
        p.previewPoints = m_previewPoints;
    }
    return p;
}

void PolygonCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

void PolygonCommand::finish()
{
    m_completed = true;
}

// CircleUndoCommand - 圆的撤销命令

CircleUndoCommand::CircleUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw Circle"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
{
    auto* sm = document->sceneManager();
    if (sm)
    {
        auto* circle = dynamic_cast<Eg::SyCircle*>(sm->findEntityById(entityId.toULongLong()));
        if (circle)
        {
            m_center = QPointF(circle->basePoint.x(), circle->basePoint.y());
            m_radius = circle->dRadius;
        }
    }
}

void CircleUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void CircleUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        m_entityId = m_document->createCircle(m_center, m_radius);
    }
    m_selectionService->select(m_entityId.toStdString());
}

// LineUndoCommand - 线段的撤销命令

LineUndoCommand::LineUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw Line"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
{
    auto* sm = document->sceneManager();
    if (sm)
    {
        auto* line = dynamic_cast<Eg::SyLine*>(sm->findEntityById(entityId.toULongLong()));
        if (line && line->vPoints.size() >= 2)
        {
            m_start = QPointF(line->vPoints[0].x(), line->vPoints[0].y());
            m_end = QPointF(line->vPoints[1].x(), line->vPoints[1].y());
        }
    }
}

void LineUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void LineUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        m_entityId = m_document->createLine(m_start, m_end);
    }
    m_selectionService->select(m_entityId.toStdString());
}

// ArcUndoCommand - 圆弧的撤销命令

ArcUndoCommand::ArcUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw Arc"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
{
    auto* sm = document->sceneManager();
    if (sm)
    {
        auto* arc = dynamic_cast<Eg::SyArc*>(sm->findEntityById(entityId.toULongLong()));
        if (arc)
        {
            m_center = QPointF(arc->basePoint.x(), arc->basePoint.y());
            m_radius = arc->dRadius;
            m_startAngle = arc->dStartAngle;
            m_endAngle = arc->dEndAngle;
        }
    }
}

void ArcUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void ArcUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        m_entityId = m_document->createArc(m_center, m_radius, m_startAngle, m_endAngle);
    }
    m_selectionService->select(m_entityId.toStdString());
}

// PolylineUndoCommand - 多段线的撤销命令

PolylineUndoCommand::PolylineUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw Polyline"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
{
}

PolylineUndoCommand::PolylineUndoCommand(SceneDocument2D* document, const EntitySnapshot& snapshot, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw Polyline"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(snapshot.id)
    , m_snapshot(snapshot)
{
}

void PolylineUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void PolylineUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        restoreFromSnapshot(m_document, m_snapshot);
    }
    m_selectionService->select(m_entityId.toStdString());
}

// Bezier2Command - 绘制二阶贝塞尔曲线命令

Bezier2Command::Bezier2Command()
{
}

QString Bezier2Command::commandId() const
{
    return QStringLiteral("2d.draw_bezier2");
}

QString Bezier2Command::displayName() const
{
    return QObject::tr("Draw Bezier2");
}

bool Bezier2Command::isInteractive() const
{
    return true;
}

CommandState Bezier2Command::state() const
{
    return m_state;
}

// 激活命令：初始化状态和阶段
bool Bezier2Command::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] Bezier2Command activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_stage = 0;
    m_startPoint = QPointF();
    m_controlPoint = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
    return true;
}

void Bezier2Command::cancel()
{
    SY_INFO("[CreateCommands] Bezier2Command cancelled");
    m_state = CommandState::Cancelled;
}

void Bezier2Command::commit()
{
    SY_INFOF("[CreateCommands] Bezier2Command committed: entity=%s", m_createdEntityId.toUtf8().constData());
    m_state = CommandState::Committed;
}

void Bezier2Command::reset()
{
    m_state = CommandState::Idle;
    m_stage = 0;
    m_startPoint = QPointF();
    m_controlPoint = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
}

// 鼠标按下事件：三阶段绘制二阶贝塞尔曲线（起点→控制点→终点）
bool Bezier2Command::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    const QPointF pt(x, y);

    if (m_stage == 0)
    {
        m_startPoint = pt;
        m_stage = 1;
    }
    else if (m_stage == 1)
    {
        m_controlPoint = pt;
        m_stage = 2;
    }
    else if (m_stage == 2)
    {
        m_endPoint = pt;
        if (m_document)
            m_createdEntityId = m_document->createBezier2(m_startPoint, m_controlPoint, m_endPoint);
        commit();
    }
    return true;
}

// 鼠标移动事件：更新当前点用于预览
bool Bezier2Command::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage == 1)
        m_controlPoint = QPointF(x, y);
    else if (m_stage == 2)
        m_endPoint = QPointF(x, y);
    return true;
}

// 键盘事件：Backspace回退阶段，Esc取消命令
bool Bezier2Command::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Backspace)
    {
        if (m_stage > 0)
        {
            m_stage--;
            return true;
        }
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }

    return false;
}

ITool* Bezier2Command::activeTool() const
{
    return nullptr;
}

UndoCommand* Bezier2Command::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    return new BezierUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

bool Bezier2Command::isComplete() const
{
    return m_createdEntityId.isEmpty() == false;
}

CommandPreview Bezier2Command::preview() const
{
    CommandPreview p;
    if (m_stage >= 2 && !m_endPoint.isNull())
    {
        p.valid = true;
        p.type = PreviewType::Bezier2;
        p.previewPoints.append(m_startPoint);
        p.previewPoints.append(m_endPoint);
        p.controlPoints.append(m_controlPoint);
    }
    return p;
}

void Bezier2Command::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// BezierCommand - 绘制三阶贝塞尔曲线命令

BezierCommand::BezierCommand()
{
}

QString BezierCommand::commandId() const
{
    return QStringLiteral("2d.draw_bezier");
}

QString BezierCommand::displayName() const
{
    return QObject::tr("Draw Bezier");
}

bool BezierCommand::isInteractive() const
{
    return true;
}

CommandState BezierCommand::state() const
{
    return m_state;
}

// 激活命令：初始化状态和阶段
bool BezierCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] BezierCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_stage = 0;
    m_startPoint = QPointF();
    m_controlPoint1 = QPointF();
    m_controlPoint2 = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
    return true;
}

void BezierCommand::cancel()
{
    SY_INFO("[CreateCommands] BezierCommand cancelled");
    m_state = CommandState::Cancelled;
}

void BezierCommand::commit()
{
    SY_INFOF("[CreateCommands] BezierCommand committed: entity=%s", m_createdEntityId.toUtf8().constData());
    m_state = CommandState::Committed;
}

void BezierCommand::reset()
{
    m_state = CommandState::Idle;
    m_stage = 0;
    m_startPoint = QPointF();
    m_controlPoint1 = QPointF();
    m_controlPoint2 = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
}

// 鼠标按下事件：四阶段绘制三阶贝塞尔曲线（起点→控制点1→控制点2→终点）
bool BezierCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    const QPointF pt(x, y);

    if (m_stage == 0)
    {
        m_startPoint = pt;
        m_stage = 1;
    }
    else if (m_stage == 1)
    {
        m_controlPoint1 = pt;
        m_stage = 2;
    }
    else if (m_stage == 2)
    {
        m_controlPoint2 = pt;
        m_stage = 3;
    }
    else if (m_stage == 3)
    {
        m_endPoint = pt;
        if (m_document)
            m_createdEntityId = m_document->createBezier(m_startPoint, m_controlPoint1, m_controlPoint2, m_endPoint);
        commit();
    }
    return true;
}

// 鼠标移动事件：更新当前点用于预览
bool BezierCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage == 1)
        m_controlPoint1 = QPointF(x, y);
    else if (m_stage == 2)
        m_controlPoint2 = QPointF(x, y);
    else if (m_stage == 3)
        m_endPoint = QPointF(x, y);
    return true;
}

// 键盘事件：Backspace回退阶段，Esc取消命令
bool BezierCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Backspace)
    {
        if (m_stage > 0)
        {
            m_stage--;
            return true;
        }
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }

    return false;
}

ITool* BezierCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* BezierCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    return new BezierUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

bool BezierCommand::isComplete() const
{
    return m_createdEntityId.isEmpty() == false;
}

CommandPreview BezierCommand::preview() const
{
    CommandPreview p;
    if (m_stage >= 3 && !m_endPoint.isNull())
    {
        p.valid = true;
        p.type = PreviewType::Bezier;
        p.previewPoints.append(m_startPoint);
        p.previewPoints.append(m_endPoint);
        p.controlPoints.append(m_controlPoint1);
        p.controlPoints.append(m_controlPoint2);
    }
    return p;
}

void BezierCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// NurbsCommand - 绘制NURBS曲线命令

NurbsCommand::NurbsCommand()
{
}

QString NurbsCommand::commandId() const
{
    return QStringLiteral("2d.draw_nurbs");
}

QString NurbsCommand::displayName() const
{
    return QObject::tr("Draw NURBS");
}

bool NurbsCommand::isInteractive() const
{
    return true;
}

CommandState NurbsCommand::state() const
{
    return m_state;
}

bool NurbsCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] NurbsCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_controlPoints.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
    return true;
}

void NurbsCommand::cancel()
{
    SY_INFO("[CreateCommands] NurbsCommand cancelled");
    m_state = CommandState::Cancelled;
}

void NurbsCommand::commit()
{
    if (m_controlPoints.size() >= 2 && m_document)
        m_createdEntityId = m_document->createNurbs(m_controlPoints);
    SY_INFOF("[CreateCommands] NurbsCommand committed: entity=%s, points=%d",
        m_createdEntityId.toUtf8().constData(), m_controlPoints.size());
    m_state = CommandState::Committed;
}

void NurbsCommand::reset()
{
    m_state = CommandState::Idle;
    m_controlPoints.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
}

bool NurbsCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_controlPoints.append(QPointF(x, y));
    return true;
}

bool NurbsCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_currentPoint = QPointF(x, y);
    return true;
}

// 键盘事件：Enter完成绘制，Backspace删除最后一点，Esc取消
bool NurbsCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Enter || key == Qt::Key_Return)
    {
        if (m_controlPoints.size() >= 2)
        {
            m_completed = true;
            commit();
        }
        return true;
    }
    else if (key == Qt::Key_Backspace && !m_controlPoints.isEmpty())
    {
        m_controlPoints.removeLast();
        return true;
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }

    return false;
}

ITool* NurbsCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* NurbsCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    return new NurbsUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

bool NurbsCommand::isComplete() const
{
    return m_completed;
}

CommandPreview NurbsCommand::preview() const
{
    CommandPreview p;
    if (!m_controlPoints.isEmpty())
    {
        p.valid = true;
        p.type = PreviewType::Nurbs;
        p.controlPoints = m_controlPoints;
        if (!m_currentPoint.isNull())
            p.controlPoints.append(m_currentPoint);
    }
    return p;
}

void NurbsCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// SmartLineCommand - 绘制复合图元命令

SmartLineCommand::SmartLineCommand()
{
}

QString SmartLineCommand::commandId() const
{
    return QStringLiteral("2d.draw_smartline");
}

QString SmartLineCommand::displayName() const
{
    return QObject::tr("Draw SmartLine");
}

bool SmartLineCommand::isInteractive() const
{
    return true;
}

CommandState SmartLineCommand::state() const
{
    return m_state;
}

bool SmartLineCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] SmartLineCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_points.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
    return true;
}

void SmartLineCommand::cancel()
{
    SY_INFO("[CreateCommands] SmartLineCommand cancelled");
    m_state = CommandState::Cancelled;
}

void SmartLineCommand::commit()
{
    if (m_points.size() >= 2 && m_document)
        m_createdEntityId = m_document->createSmartLine(m_points);
    SY_INFOF("[CreateCommands] SmartLineCommand committed: entity=%s, points=%d",
        m_createdEntityId.toUtf8().constData(), m_points.size());
    m_state = CommandState::Committed;
}

void SmartLineCommand::reset()
{
    m_state = CommandState::Idle;
    m_points.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
}

bool SmartLineCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_points.append(QPointF(x, y));
    return true;
}

bool SmartLineCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_currentPoint = QPointF(x, y);
    return true;
}

// 键盘事件：Enter完成绘制，Backspace删除最后一点，Esc取消
bool SmartLineCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Enter || key == Qt::Key_Return)
    {
        if (m_points.size() >= 2)
        {
            m_completed = true;
            commit();
        }
        return true;
    }
    else if (key == Qt::Key_Backspace && !m_points.isEmpty())
    {
        m_points.removeLast();
        return true;
    }
    else if (key == Qt::Key_Escape)
    {
        cancel();
        return true;
    }

    return false;
}

ITool* SmartLineCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* SmartLineCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty())
        return nullptr;

    return new SmartLineUndoCommand(m_document, m_createdEntityId, m_selectionService);
}

bool SmartLineCommand::isComplete() const
{
    return m_completed;
}

CommandPreview SmartLineCommand::preview() const
{
    CommandPreview p;
    if (!m_points.isEmpty())
    {
        p.valid = true;
        p.type = PreviewType::SmartLine;
        p.previewPoints = m_points;
        if (!m_currentPoint.isNull())
            p.previewPoints.append(m_currentPoint);
    }
    return p;
}

void SmartLineCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// BezierUndoCommand - 贝塞尔曲线的撤销命令

BezierUndoCommand::BezierUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw Bezier"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
    , m_isBezier2(false)
{
    auto* sm = document->sceneManager();
    if (sm)
    {
        auto* bezier2 = dynamic_cast<Eg::SyBezier2*>(sm->findEntityById(entityId.toULongLong()));
        if (bezier2)
        {
            m_isBezier2 = true;
            m_start = QPointF(bezier2->basePoint.x(), bezier2->basePoint.y());
            m_ctrl1 = QPointF(bezier2->ptCtrl.x(), bezier2->ptCtrl.y());
            m_end = QPointF(bezier2->ptEnd.x(), bezier2->ptEnd.y());
        }
        else
        {
            auto* bezier = dynamic_cast<Eg::SyBezier*>(sm->findEntityById(entityId.toULongLong()));
            if (bezier)
            {
                m_isBezier2 = false;
                m_start = QPointF(bezier->basePoint.x(), bezier->basePoint.y());
                m_ctrl1 = QPointF(bezier->ptCtrl0.x(), bezier->ptCtrl0.y());
                m_ctrl2 = QPointF(bezier->ptCtrl1.x(), bezier->ptCtrl1.y());
                m_end = QPointF(bezier->ptEnd.x(), bezier->ptEnd.y());
            }
        }
    }
}

void BezierUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void BezierUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        if (m_isBezier2)
            m_entityId = m_document->createBezier2(m_start, m_ctrl1, m_end);
        else
            m_entityId = m_document->createBezier(m_start, m_ctrl1, m_ctrl2, m_end);
    }
    m_selectionService->select(m_entityId.toStdString());
}

// NurbsUndoCommand - NURBS曲线的撤销命令

NurbsUndoCommand::NurbsUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw NURBS"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
{
    auto* sm = document->sceneManager();
    if (sm)
    {
        auto* nurbs = dynamic_cast<Eg::SyNurbs*>(sm->findEntityById(entityId.toULongLong()));
        if (nurbs)
        {
            for (const auto& pt : nurbs->vControlPoints)
                m_controlPoints.append(QPointF(pt.x(), pt.y()));
        }
    }
}

void NurbsUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void NurbsUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        m_entityId = m_document->createNurbs(m_controlPoints);
    }
    m_selectionService->select(m_entityId.toStdString());
}

// SmartLineUndoCommand - 复合图元的撤销命令

SmartLineUndoCommand::SmartLineUndoCommand(SceneDocument2D* document, const QString& entityId, ISelectionService* selService)
    : UndoCommand(QObject::tr("Draw SmartLine"))
    , m_document(document)
    , m_selectionService(selService)
    , m_entityId(entityId)
{
    // SmartLine 是容器型图元，撤销时直接保存其 ID 即可，
    // 具体的子段信息在 undo/redo 时通过场景管理器整体提取/恢复。
}

void SmartLineUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    auto eid = static_cast<Eg::EntityId>(m_entityId.toULongLong());
    auto entity = sm->extractEntityById(eid);
    if (entity)
    {
        m_storedEntity = std::move(entity);
    }
}

void SmartLineUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (m_storedEntity)
    {
        sm->insertEntityPreserveId(std::move(m_storedEntity));
    }
    else
    {
        m_entityId = m_document->createSmartLine(m_points);
    }
    m_selectionService->select(m_entityId.toStdString());
}