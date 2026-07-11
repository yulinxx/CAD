#include "CreateCommands.h"

#include <QObject>
#include <cmath>

#include "SceneDocument2D.h"
#include "UiServices.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Mat/Mat.hpp"
#include "CommandSnapshots.h"
#include "Log/SyLogger.h"

namespace {

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
        m_document->selectEntity(m_createdEntityId);
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

    return new LineUndoCommand(m_document, m_createdEntityId);
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

    return new CircleUndoCommand(m_document, m_createdEntityId);
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

    return new ArcUndoCommand(m_document, m_createdEntityId);
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

// 键盘事件：Enter完成绘制，Backspace删除最后一点，Esc取消
bool PolylineCommand::onKeyPress(int key)
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
    return new PolylineUndoCommand(m_document, snap);
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

bool PolygonCommand::activate(const UiServices& services)
{
    SY_INFO("[CreateCommands] PolygonCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_points.clear();
    m_currentPoint = QPointF();
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
    if (m_points.size() >= 3 && m_document)
    {
        m_createdEntityId = m_document->createPolygon(m_points);
    }
    SY_INFOF("[CreateCommands] PolygonCommand committed: entity=%s, vertices=%d", 
        m_createdEntityId.toUtf8().constData(), m_points.size());
    m_state = CommandState::Committed;
}

void PolygonCommand::reset()
{
    m_state = CommandState::Idle;
    m_points.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    m_createdEntityId.clear();
}

bool PolygonCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_points.append(QPointF(x, y));
    return true;
}

bool PolygonCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_currentPoint = QPointF(x, y);
    return true;
}

// 键盘事件：Enter完成绘制，Backspace删除最后一点，Esc取消
bool PolygonCommand::onKeyPress(int key)
{
    if (m_state != CommandState::Active)
        return false;

    if (key == Qt::Key_Enter || key == Qt::Key_Return)
    {
        if (m_points.size() >= 3)
            finish();
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
    return new PolylineUndoCommand(m_document, snap);
}

bool PolygonCommand::isComplete() const
{
    return m_completed;
}

CommandPreview PolygonCommand::preview() const
{
    CommandPreview p;
    if (m_points.size() >= 2)
    {
        p.valid = true;
        p.type = PreviewType::Polygon;
        p.previewPoints = m_points;
        if (!m_currentPoint.isNull())
            p.previewPoints.append(m_currentPoint);
        p.previewPoints.append(m_points.first());
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

CircleUndoCommand::CircleUndoCommand(SceneDocument2D* document, const QString& entityId)
    : UndoCommand(QObject::tr("Draw Circle"))
    , m_document(document)
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
    m_document->selectEntity(m_entityId);
}

// LineUndoCommand - 线段的撤销命令

LineUndoCommand::LineUndoCommand(SceneDocument2D* document, const QString& entityId)
    : UndoCommand(QObject::tr("Draw Line"))
    , m_document(document)
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
    m_document->selectEntity(m_entityId);
}

// ArcUndoCommand - 圆弧的撤销命令

ArcUndoCommand::ArcUndoCommand(SceneDocument2D* document, const QString& entityId)
    : UndoCommand(QObject::tr("Draw Arc"))
    , m_document(document)
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
    m_document->selectEntity(m_entityId);
}

// PolylineUndoCommand - 多段线的撤销命令

PolylineUndoCommand::PolylineUndoCommand(SceneDocument2D* document, const QString& entityId)
    : UndoCommand(QObject::tr("Draw Polyline"))
    , m_document(document)
    , m_entityId(entityId)
{
}

PolylineUndoCommand::PolylineUndoCommand(SceneDocument2D* document, const EntitySnapshot& snapshot)
    : UndoCommand(QObject::tr("Draw Polyline"))
    , m_document(document)
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
    m_document->selectEntity(m_entityId);
}