#include "UiCommandHandler.h"

#include <QObject>

#include "SceneDocument2D.h"
#include "UiServices.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Mat/Mat.hpp"

namespace {

class DefaultTool : public ITool
{
public:
    explicit DefaultTool(const QString& toolId, const QString& displayName)
        : m_toolId(toolId)
        , m_displayName(displayName)
    {}

    QString toolId() const override { return m_toolId; }
    QString displayName() const override { return m_displayName; }
    bool activate(const UiServices& /*services*/) override { return true; }
    void cancel() override {}
    void reset() override {}

private:
    QString m_toolId;
    QString m_displayName;
};

} // namespace

PointPickerTool::PointPickerTool(int requiredPoints)
    : m_requiredPoints(requiredPoints)
{
}

QString PointPickerTool::toolId() const
{
    return QStringLiteral("point_picker");
}

QString PointPickerTool::displayName() const
{
    return QObject::tr("Point Picker"); // 拾点工具
}

bool PointPickerTool::activate(const UiServices& services)
{
    m_services = services;
    m_pickedPoints.clear();
    m_lastPoint = QPointF();
    return true;
}

void PointPickerTool::cancel()
{
    m_pickedPoints.clear();
    m_lastPoint = QPointF();
}

void PointPickerTool::reset()
{
    m_pickedPoints.clear();
    m_lastPoint = QPointF();
}

bool PointPickerTool::onMouseDown(int x, int y)
{
    m_lastPoint = QPointF(x, y);
    m_pickedPoints.push_back(m_lastPoint);
    return true;
}

bool PointPickerTool::isStageComplete() const
{
    return hasEnoughPoints();
}

QString PointPickerTool::currentStage() const
{
    const size_t count = m_pickedPoints.size();
    if (count == 0)
        return QObject::tr("Waiting for first point"); // 等待第一点
    if (count < static_cast<size_t>(m_requiredPoints))
        return QObject::tr("Waiting for point %1").arg(count + 1); // 等待第 %1 点
    return QObject::tr("Points collected"); // 点收集完成
}

UndoCommand::UndoCommand(const QString& text)
    : m_text(text)
{
}

QString UndoCommand::text() const
{
    return m_text;
}

void DefaultUndoStack::push(UndoCommand* command)
{
    if (!command)
        return;

    m_stack.resize(m_currentIndex + 1);
    m_stack.push_back(std::unique_ptr<UndoCommand>(command));
    m_currentIndex++;
}

bool DefaultUndoStack::undo()
{
    if (!canUndo())
        return false;

    m_stack[m_currentIndex]->undo();
    m_currentIndex--;
    return true;
}

bool DefaultUndoStack::redo()
{
    if (!canRedo())
        return false;

    m_currentIndex++;
    m_stack[m_currentIndex]->redo();
    return true;
}

bool DefaultUndoStack::canUndo() const
{
    return m_currentIndex >= 0;
}

bool DefaultUndoStack::canRedo() const
{
    return m_currentIndex < static_cast<int>(m_stack.size()) - 1;
}

QString DefaultUndoStack::undoText() const
{
    if (!canUndo())
        return QString();
    return m_stack[m_currentIndex]->text();
}

QString DefaultUndoStack::redoText() const
{
    if (!canRedo())
        return QString();
    return m_stack[m_currentIndex + 1]->text();
}

void DefaultUndoStack::clear()
{
    m_stack.clear();
    m_currentIndex = -1;
}

int DefaultUndoStack::count() const
{
    return static_cast<int>(m_stack.size());
}

namespace {

class DrawLineUndoCommand : public UndoCommand
{
public:
    DrawLineUndoCommand(SceneDocument2D* document, const QString& entityId,
                        const QPointF& start, const QPointF& end)
        : UndoCommand(QObject::tr("Draw Line")) // 画线
        , m_document(document)
        , m_entityId(entityId)
        , m_start(start)
        , m_end(end)
    {}

    void undo() override
    {
        if (m_document)
            m_document->removeEntity(m_entityId);
    }

    void redo() override
    {
        if (m_document)
            m_entityId = m_document->createLine(m_start, m_end);
    }

private:
    SceneDocument2D* m_document;
    QString m_entityId;
    QPointF m_start;
    QPointF m_end;
};

} // namespace

DrawLineCommand::DrawLineCommand()
    : m_pointPicker(2)
{
}

QString DrawLineCommand::commandId() const
{
    return QStringLiteral("2d.draw_line");
}

QString DrawLineCommand::displayName() const
{
    return QObject::tr("Draw Line"); // 画线
}

bool DrawLineCommand::isInteractive() const
{
    return true;
}

CommandState DrawLineCommand::state() const
{
    return m_state;
}

bool DrawLineCommand::activate(const UiServices& services)
{
    m_services = &services;

    if (services.document2D)
        m_document = services.document2D;

    if (!m_document)
        return false;

    m_state = CommandState::Active;
    m_pointPicker.activate(services);
    m_previewStart = QPointF();
    m_previewEnd = QPointF();
    m_createdEntityId.clear();
    return true;
}

void DrawLineCommand::cancel()
{
    m_state = CommandState::Cancelled;
    m_pointPicker.cancel();
    m_previewStart = QPointF();
    m_previewEnd = QPointF();
    m_createdEntityId.clear();
}

void DrawLineCommand::commit()
{
    m_state = CommandState::Committed;

    if (m_document && m_pointPicker.hasEnoughPoints())
    {
        const auto& points = m_pointPicker.pickedPoints();
        if (points.size() >= 2)
        {
            m_createdEntityId = m_document->createLine(points[0], points[1]);
            m_document->clearSelection();
            m_document->selectEntity(m_createdEntityId);
        }
    }

    m_previewStart = QPointF();
    m_previewEnd = QPointF();
}

void DrawLineCommand::reset()
{
    m_state = CommandState::Idle;
    m_pointPicker.reset();
    m_previewStart = QPointF();
    m_previewEnd = QPointF();
    m_createdEntityId.clear();
}

bool DrawLineCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_pointPicker.onMouseDown(x, y);

    const auto& points = m_pointPicker.pickedPoints();
    if (points.size() == 1)
    {
        m_previewStart = points[0];
    }
    else if (points.size() >= 2)
    {
        m_previewEnd = points.back();
    }

    return true;
}

bool DrawLineCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    const auto& points = m_pointPicker.pickedPoints();
    if (!points.empty())
    {
        m_previewEnd = QPointF(x, y);
    }

    return true;
}

ITool* DrawLineCommand::activeTool() const
{
    return const_cast<PointPickerTool*>(&m_pointPicker);
}

UndoCommand* DrawLineCommand::createUndoCommand()
{
    if (m_createdEntityId.isEmpty() || !m_document)
        return nullptr;

    const auto& points = m_pointPicker.pickedPoints();
    if (points.size() >= 2)
        return new DrawLineUndoCommand(m_document, m_createdEntityId, points[0], points[1]);

    return nullptr;
}

bool DrawLineCommand::isComplete() const
{
    return m_pointPicker.isStageComplete();
}

CommandPreview DrawLineCommand::preview() const
{
    CommandPreview p;
    if (!m_previewStart.isNull())
    {
        p.valid = true;
        p.previewStart = m_previewStart;
        p.previewEnd = m_previewEnd;
        p.stageText = m_pointPicker.currentStage();
    }
    return p;
}

void DrawLineCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

namespace {

QPointF rotatePoint(const QPointF& point, const QPointF& center, double cosAngle, double sinAngle)
{
    double dx = point.x() - center.x();
    double dy = point.y() - center.y();
    return QPointF(
        center.x() + dx * cosAngle - dy * sinAngle,
        center.y() + dx * sinAngle + dy * cosAngle
    );
}

class RotateUndoCommand : public UndoCommand
{
public:
    RotateUndoCommand(SceneDocument2D* document, const QString& entityId,
                      const std::vector<QPointF>& originalPoints,
                      const std::vector<QPointF>& newPoints)
        : UndoCommand(QObject::tr("Rotate")) // 旋转
        , m_document(document)
        , m_entityId(entityId)
        , m_originalPoints(originalPoints)
        , m_newPoints(newPoints)
    {}

    void undo() override
    {
        if (!m_document || m_entityId.isEmpty())
            return;
        auto* entity = m_document->entityByStringId(m_entityId);
        if (!entity || entity->eType != Eg::EType::LINE)
            return;
        auto* line = static_cast<Eg::SyLine*>(entity);
        if (m_originalPoints.size() >= 2)
        {
            line->vPoints[0] = Ut::Vec2d(m_originalPoints[0].x(), m_originalPoints[0].y());
            line->vPoints[1] = Ut::Vec2d(m_originalPoints[1].x(), m_originalPoints[1].y());
        }
    }

    void redo() override
    {
        if (!m_document || m_entityId.isEmpty())
            return;
        auto* entity = m_document->entityByStringId(m_entityId);
        if (!entity || entity->eType != Eg::EType::LINE)
            return;
        auto* line = static_cast<Eg::SyLine*>(entity);
        if (m_newPoints.size() >= 2)
        {
            line->vPoints[0] = Ut::Vec2d(m_newPoints[0].x(), m_newPoints[0].y());
            line->vPoints[1] = Ut::Vec2d(m_newPoints[1].x(), m_newPoints[1].y());
        }
    }

private:
    SceneDocument2D* m_document;
    QString m_entityId;
    std::vector<QPointF> m_originalPoints;
    std::vector<QPointF> m_newPoints;
};

} // namespace

namespace {

class SelectUndoCommand : public UndoCommand
{
public:
    SelectUndoCommand(SceneDocument2D* document, const QString& oldSelectedId, const QString& newSelectedId)
        : UndoCommand(QObject::tr("Select")) // 选择
        , m_document(document)
        , m_oldSelectedId(oldSelectedId)
        , m_newSelectedId(newSelectedId)
    {}

    void undo() override
    {
        if (!m_document)
            return;
        m_document->clearSelection();
        if (!m_oldSelectedId.isEmpty())
            m_document->selectEntity(m_oldSelectedId);
    }

    void redo() override
    {
        if (!m_document)
            return;
        m_document->clearSelection();
        if (!m_newSelectedId.isEmpty())
            m_document->selectEntity(m_newSelectedId);
    }

private:
    SceneDocument2D* m_document;
    QString m_oldSelectedId;
    QString m_newSelectedId;
};

} // namespace

SelectCommand::SelectCommand()
{
}

QString SelectCommand::commandId() const
{
    return QStringLiteral("2d.select");
}

QString SelectCommand::displayName() const
{
    return QObject::tr("Select"); // 选择
}

bool SelectCommand::isInteractive() const
{
    return true;
}

CommandState SelectCommand::state() const
{
    return m_state;
}

bool SelectCommand::activate(const UiServices& services)
{
    m_services = &services;
    m_state = CommandState::Active;
    m_selectedEntityId.clear();
    m_oldSelectedId.clear();
    return true;
}

void SelectCommand::cancel()
{
    m_state = CommandState::Cancelled;
    if (m_document)
    {
        m_document->clearSelection();
        if (!m_oldSelectedId.isEmpty())
            m_document->selectEntity(m_oldSelectedId);
    }
    m_selectedEntityId.clear();
    m_oldSelectedId.clear();
}

void SelectCommand::commit()
{
    m_state = CommandState::Committed;
}

void SelectCommand::reset()
{
    m_state = CommandState::Idle;
    m_selectedEntityId.clear();
    m_oldSelectedId.clear();
}

bool SelectCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active || !m_document)
        return false;

    m_boxSelectStart = QPointF(x, y);
    m_boxSelectEnd = QPointF(x, y);
    m_boxSelecting = true;

    auto selectedIds = m_document->selectedIds();
    if (!selectedIds.isEmpty())
        m_oldSelectedId = selectedIds[0];

    m_document->clearSelection();

    return true;
}

bool SelectCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active || !m_boxSelecting)
        return false;

    m_boxSelectEnd = QPointF(x, y);
    return true;
}

bool SelectCommand::onMouseUp(int x, int y)
{
    if (m_state != CommandState::Active || !m_boxSelecting)
        return false;

    m_boxSelectEnd = QPointF(x, y);

    const QRectF rect(m_boxSelectStart, m_boxSelectEnd);
    if (rect.width() < 5 && rect.height() < 5)
    {
        m_boxSelecting = false;
        return true;
    }

    performBoxSelect();
    m_boxSelecting = false;
    return true;
}

void SelectCommand::performBoxSelect()
{
    if (!m_document)
        return;
    auto* sm = m_document->sceneManager();
    if (!sm) return;

    QRectF rect = QRectF(m_boxSelectStart, m_boxSelectEnd).normalized();
    for (auto* entity : sm->getAllEntities())
    {
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            if (line->vPoints.size() >= 2)
            {
                const QPointF p0(line->vPoints[0].x(), line->vPoints[0].y());
                const QPointF p1(line->vPoints[1].x(), line->vPoints[1].y());
                if (rect.intersects(QRectF(p0, p1).normalized()))
                    m_document->selectEntity(QString::number(entity->id));
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            const double r = circle->dRadius;
            const QRectF bounds(entity->basePoint.x() - r, entity->basePoint.y() - r, r * 2.0, r * 2.0);
            if (rect.intersects(bounds))
                m_document->selectEntity(QString::number(entity->id));
        }
    }
}

ITool* SelectCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* SelectCommand::createUndoCommand()
{
    if (m_selectedEntityId.isEmpty() && m_oldSelectedId.isEmpty())
        return nullptr;

    return new SelectUndoCommand(m_document, m_oldSelectedId, m_selectedEntityId);
}

bool SelectCommand::isComplete() const
{
    return m_state == CommandState::Active && (!m_selectedEntityId.isEmpty() || !m_boxSelecting);
}

CommandPreview SelectCommand::preview() const
{
    CommandPreview p;
    if (m_boxSelecting)
    {
        p.valid = true;
        p.previewStart = m_boxSelectStart;
        p.previewEnd = m_boxSelectEnd;
        p.stageText = QObject::tr("Box select mode"); // 框选模式
    }
    return p;
}

void SelectCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

void SelectCommand::setSelectedEntityId(const QString& entityId)
{
    m_selectedEntityId = entityId;
}

RotateCommand::RotateCommand()
{
}

QString RotateCommand::commandId() const
{
    return QStringLiteral("2d.rotate");
}

QString RotateCommand::displayName() const
{
    return QObject::tr("Rotate"); // 旋转
}

bool RotateCommand::isInteractive() const
{
    return true;
}

CommandState RotateCommand::state() const
{
    return m_state;
}

bool RotateCommand::activate(const UiServices& services)
{
    m_services = &services;
    m_state = CommandState::Active;
    m_selectedEntityId.clear();
    m_originalPoints.clear();
    m_startAngle = 0.0;
    m_currentAngle = 0.0;
    return true;
}

void RotateCommand::cancel()
{
    m_state = CommandState::Cancelled;
    restoreOriginalPoints();
    m_selectedEntityId.clear();
    m_originalPoints.clear();
}

void RotateCommand::commit()
{
    m_state = CommandState::Committed;
}

void RotateCommand::reset()
{
    m_state = CommandState::Idle;
    m_selectedEntityId.clear();
    m_originalPoints.clear();
    m_startAngle = 0.0;
    m_currentAngle = 0.0;
}

void RotateCommand::restoreOriginalPoints()
{
    if (!m_document || m_selectedEntityId.isEmpty() || m_originalPoints.empty())
        return;
    auto* entity = m_document->entityByStringId(m_selectedEntityId);
    if (!entity || entity->eType != Eg::EType::LINE)
        return;
    auto* line = static_cast<Eg::SyLine*>(entity);
    if (m_originalPoints.size() >= 2)
    {
        line->vPoints[0] = Ut::Vec2d(m_originalPoints[0].x(), m_originalPoints[0].y());
        line->vPoints[1] = Ut::Vec2d(m_originalPoints[1].x(), m_originalPoints[1].y());
    }
}

bool RotateCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_startPoint = QPointF(x, y);
    m_rotationCenter = QPointF(x, y);

    if (m_document)
    {
        auto selectedIds = m_document->selectedIds();
        if (!selectedIds.isEmpty())
        {
            m_selectedEntityId = selectedIds[0];
            auto* entity = m_document->entityByStringId(m_selectedEntityId);
            if (entity && entity->eType == Eg::EType::LINE)
            {
                auto* line = static_cast<Eg::SyLine*>(entity);
                m_originalPoints = { QPointF(line->vPoints[0].x(), line->vPoints[0].y()),
                                     QPointF(line->vPoints[1].x(), line->vPoints[1].y()) };
                m_rotationCenter = QPointF(
                    (line->vPoints[0].x() + line->vPoints[1].x()) * 0.5,
                    (line->vPoints[0].y() + line->vPoints[1].y()) * 0.5);
            }
        }
    }

    m_startAngle = atan2(y - m_rotationCenter.y(), x - m_rotationCenter.x());
    m_currentAngle = m_startAngle;

    return true;
}

bool RotateCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_currentAngle = atan2(y - m_rotationCenter.y(), x - m_rotationCenter.x());

    return true;
}

bool RotateCommand::onMouseUp(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);

    if (m_state != CommandState::Active)
        return false;

    double angleDelta = m_currentAngle - m_startAngle;

    if (m_document && !m_selectedEntityId.isEmpty() && !m_originalPoints.empty())
    {
        auto* entity = m_document->entityByStringId(m_selectedEntityId);
        if (entity && entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            const double cosA = cos(angleDelta);
            const double sinA = sin(angleDelta);
            for (auto& pt : line->vPoints)
            {
                double dx = pt.x() - m_rotationCenter.x();
                double dy = pt.y() - m_rotationCenter.y();
                pt.x() = m_rotationCenter.x() + dx * cosA - dy * sinA;
                pt.y() = m_rotationCenter.y() + dx * sinA + dy * cosA;
            }
        }
    }

    return true;
}

ITool* RotateCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* RotateCommand::createUndoCommand()
{
    if (m_selectedEntityId.isEmpty() || m_originalPoints.empty() || !m_document)
        return nullptr;

    auto* entity = m_document->entityByStringId(m_selectedEntityId);
    if (!entity || entity->eType != Eg::EType::LINE)
        return nullptr;

    auto* line = static_cast<Eg::SyLine*>(entity);
    std::vector<QPointF> newPoints = {
        QPointF(line->vPoints[0].x(), line->vPoints[0].y()),
        QPointF(line->vPoints[1].x(), line->vPoints[1].y())
    };

    return new RotateUndoCommand(m_document, m_selectedEntityId, m_originalPoints, newPoints);
}

bool RotateCommand::isComplete() const
{
    return m_state == CommandState::Active && m_startAngle != m_currentAngle;
}

void RotateCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// ============================================================================
// MoveCommand 实现
// ============================================================================

MoveCommand::MoveCommand() = default;

QString MoveCommand::commandId() const
{
    return QStringLiteral("2d.move");
}

QString MoveCommand::displayName() const
{
    return QObject::tr("Move"); // 移动
}

bool MoveCommand::isInteractive() const
{
    return true;
}

CommandState MoveCommand::state() const
{
    return m_state;
}

bool MoveCommand::activate(const UiServices& services)
{
    m_services = &services;

    if (services.document2D)
        m_document = services.document2D;

    if (!m_document)
        return false;

    if (m_document->selectedIds().isEmpty())
        return false;

    saveOriginalPositions();

    m_state = CommandState::Active;
    m_hasAnchor = false;
    m_committed = false;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    return true;
}

void MoveCommand::cancel()
{
    m_state = CommandState::Cancelled;
    m_originalPositions.clear();
    m_hasAnchor = false;
    m_committed = false;
}

void MoveCommand::commit()
{
    const QPointF delta = m_targetPoint - m_anchorPoint;
    if (m_document && !m_committed)
    {
        auto* sm = m_document->sceneManager();
        if (sm)
        {
            auto transMat = Ut::Mat3d::translate(delta.x(), delta.y());
            for (auto* entity : sm->getSelectedEntities())
                entity->transform(transMat);
        }
    }

    m_state = CommandState::Committed;
    m_committed = true;
}

void MoveCommand::reset()
{
    m_state = CommandState::Idle;
    m_originalPositions.clear();
    m_hasAnchor = false;
    m_committed = false;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
}

bool MoveCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_anchorPoint = QPointF(x, y);
    m_hasAnchor = true;
    return true;
}

bool MoveCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active || !m_hasAnchor)
        return false;

    m_targetPoint = QPointF(x, y);
    return true;
}

bool MoveCommand::onMouseUp(int x, int y)
{
    if (m_state != CommandState::Active || !m_hasAnchor)
        return false;

    m_targetPoint = QPointF(x, y);

    return true;
}

ITool* MoveCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* MoveCommand::createUndoCommand()
{
    if (!m_document || m_originalPositions.empty() || !m_committed)
        return nullptr;

    return new MoveUndoCommand(m_document, m_originalPositions);
}

bool MoveCommand::isComplete() const
{
    return m_hasAnchor && !m_targetPoint.isNull();
}

CommandPreview MoveCommand::preview() const
{
    CommandPreview p;
    if (m_hasAnchor)
    {
        p.valid = true;
        p.previewStart = m_anchorPoint;
        p.previewEnd = m_targetPoint;
        p.stageText = QObject::tr("Move preview"); // 移动预览
    }
    return p;
}

void MoveCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

void MoveCommand::saveOriginalPositions()
{
    m_originalPositions.clear();
    if (!m_document)
        return;
    auto* sm = m_document->sceneManager();
    if (!sm) return;

    for (auto* entity : sm->getSelectedEntities())
    {
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            std::vector<QPointF> pts = {
                QPointF(line->vPoints[0].x(), line->vPoints[0].y()),
                QPointF(line->vPoints[1].x(), line->vPoints[1].y())
            };
            m_originalPositions[QString::number(entity->id)] = pts;
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            std::vector<QPointF> pts = { QPointF(entity->basePoint.x(), entity->basePoint.y()) };
            m_originalPositions[QString::number(entity->id)] = pts;
        }
    }
}

void MoveCommand::restoreOriginalPositions()
{
    if (!m_document)
        return;

    for (const auto& pair : m_originalPositions)
    {
        auto* entity = m_document->entityByStringId(pair.first);
        if (!entity)
            continue;
        const auto& pts = pair.second;

        if (entity->eType == Eg::EType::LINE && pts.size() >= 2)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            line->vPoints[0] = Ut::Vec2d(pts[0].x(), pts[0].y());
            line->vPoints[1] = Ut::Vec2d(pts[1].x(), pts[1].y());
        }
        else if (entity->eType == Eg::EType::CIRCLE && !pts.empty())
        {
            entity->basePoint = Ut::Vec2d(pts[0].x(), pts[0].y());
        }
    }
}

// ============================================================================
// MoveUndoCommand 实现
// ============================================================================

MoveUndoCommand::MoveUndoCommand(SceneDocument2D* document,
                                  std::map<QString, std::vector<QPointF>> originalPositions)
    : UndoCommand(QObject::tr("Move")) // 移动
    , m_document(document)
    , m_originalPositions(std::move(originalPositions))
{
    for (const auto& pair : m_originalPositions)
    {
        auto* entity = m_document->entityByStringId(pair.first);
        if (!entity)
            continue;
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            m_newPositions[pair.first] = {
                QPointF(line->vPoints[0].x(), line->vPoints[0].y()),
                QPointF(line->vPoints[1].x(), line->vPoints[1].y())
            };
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            m_newPositions[pair.first] = { QPointF(entity->basePoint.x(), entity->basePoint.y()) };
        }
    }
}

void MoveUndoCommand::undo()
{
    if (!m_document)
        return;

    for (const auto& pair : m_originalPositions)
    {
        auto* entity = m_document->entityByStringId(pair.first);
        if (!entity)
            continue;
        const auto& pts = pair.second;

        if (entity->eType == Eg::EType::LINE && pts.size() >= 2)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            line->vPoints[0] = Ut::Vec2d(pts[0].x(), pts[0].y());
            line->vPoints[1] = Ut::Vec2d(pts[1].x(), pts[1].y());
        }
        else if (entity->eType == Eg::EType::CIRCLE && !pts.empty())
        {
            entity->basePoint = Ut::Vec2d(pts[0].x(), pts[0].y());
        }
    }
}

void MoveUndoCommand::redo()
{
    if (!m_document)
        return;

    for (const auto& pair : m_newPositions)
    {
        auto* entity = m_document->entityByStringId(pair.first);
        if (!entity)
            continue;
        const auto& pts = pair.second;

        if (entity->eType == Eg::EType::LINE && pts.size() >= 2)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            line->vPoints[0] = Ut::Vec2d(pts[0].x(), pts[0].y());
            line->vPoints[1] = Ut::Vec2d(pts[1].x(), pts[1].y());
        }
        else if (entity->eType == Eg::EType::CIRCLE && !pts.empty())
        {
            entity->basePoint = Ut::Vec2d(pts[0].x(), pts[0].y());
        }
    }
}

CircleUndoCommand::CircleUndoCommand(SceneDocument2D* document, const QString& entityId)
    : UndoCommand(QObject::tr("Draw Circle")) // 画圆
    , m_document(document)
    , m_entityId(entityId)
{
    if (m_document)
    {
        auto* entity = m_document->entityByStringId(entityId);
        if (entity && entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            m_center = QPointF(entity->basePoint.x(), entity->basePoint.y());
            m_radius = circle->dRadius;
        }
    }
}

void CircleUndoCommand::undo()
{
    if (!m_document)
        return;
    m_document->removeEntity(m_entityId);
}

void CircleUndoCommand::redo()
{
    if (!m_document)
        return;
    m_entityId = m_document->createCircle(m_center, m_radius);
}

PolylineUndoCommand::PolylineUndoCommand(SceneDocument2D* document, const QString& entityId)
    : UndoCommand(QObject::tr("Draw Polyline")) // 画折线
    , m_document(document)
    , m_entityId(entityId)
{
    if (m_document)
    {
        auto* entity = m_document->entityByStringId(entityId);
        if (entity && entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
                m_points.push_back(QPointF(pt.x(), pt.y()));
        }
    }
}

void PolylineUndoCommand::undo()
{
    if (!m_document)
        return;
    m_document->removeEntity(m_entityId);
}

void PolylineUndoCommand::redo()
{
    if (!m_document)
        return;
    m_entityId = m_document->createLine(m_points.front(), m_points.back());
}

CopyUndoCommand::CopyUndoCommand(SceneDocument2D* document, const QStringList& copiedEntityIds)
    : UndoCommand(QObject::tr("Copy")) // 复制
    , m_document(document)
    , m_copiedEntityIds(copiedEntityIds)
{
}

void CopyUndoCommand::undo()
{
    if (!m_document)
        return;
    for (const auto& id : m_copiedEntityIds)
        m_document->removeEntity(id);
}

void CopyUndoCommand::redo()
{
    if (!m_document)
        return;
    QStringList newIds;
    for (const auto& id : m_copiedEntityIds)
    {
        auto* entity = m_document->entityByStringId(id);
        if (!entity)
            continue;
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            newIds.append(m_document->createLine(
                QPointF(line->vPoints[0].x(), line->vPoints[0].y()),
                QPointF(line->vPoints[1].x(), line->vPoints[1].y())));
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            newIds.append(m_document->createCircle(
                QPointF(entity->basePoint.x(), entity->basePoint.y()), circle->dRadius));
        }
    }
    m_copiedEntityIds = newIds;
}

// ============================================================================
// CircleCommand 实现
// ============================================================================

CircleCommand::CircleCommand() = default;

QString CircleCommand::commandId() const { return QStringLiteral("2d.draw_circle"); }
QString CircleCommand::displayName() const { return QObject::tr("Draw Circle"); } // 画圆
bool CircleCommand::isInteractive() const { return true; }
CommandState CircleCommand::state() const { return m_state; }

bool CircleCommand::activate(const UiServices& services)
{
    m_services = &services;
    if (services.document2D)
        m_document = services.document2D;

    if (!m_document)
        return false;

    m_state = CommandState::Active;
    m_hasCenter = false;
    m_center = QPointF();
    m_endPoint = QPointF();
    return true;
}

void CircleCommand::cancel()
{
    m_state = CommandState::Cancelled;
    m_hasCenter = false;
    m_center = QPointF();
    m_endPoint = QPointF();
}

void CircleCommand::commit()
{
    if (m_document && m_hasCenter && !m_endPoint.isNull())
    {
        const double radius = QLineF(m_center, m_endPoint).length();
        if (radius > 0)
        {
            m_createdEntityId = m_document->createCircle(m_center, radius);
            m_document->clearSelection();
            m_document->selectEntity(m_createdEntityId);
        }
    }
    m_state = CommandState::Committed;
}

void CircleCommand::reset()
{
    m_state = CommandState::Idle;
    m_hasCenter = false;
    m_center = QPointF();
    m_endPoint = QPointF();
    m_createdEntityId.clear();
}

bool CircleCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (!m_hasCenter)
    {
        m_center = QPointF(x, y);
        m_hasCenter = true;
    }
    return true;
}

bool CircleCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active || !m_hasCenter)
        return false;

    m_endPoint = QPointF(x, y);
    return true;
}

bool CircleCommand::onMouseUp(int x, int y)
{
    if (m_state != CommandState::Active || !m_hasCenter)
        return false;

    m_endPoint = QPointF(x, y);
    return true;
}

ITool* CircleCommand::activeTool() const { return nullptr; }
UndoCommand* CircleCommand::createUndoCommand()
{
    if (m_document && !m_createdEntityId.isEmpty())
        return new CircleUndoCommand(m_document, m_createdEntityId);
    return nullptr;
}

bool CircleCommand::isComplete() const
{
    return m_hasCenter && !m_endPoint.isNull();
}

CommandPreview CircleCommand::preview() const
{
    CommandPreview p;
    if (m_hasCenter)
    {
        p.valid = true;
        p.previewStart = m_center;
        p.previewEnd = m_endPoint;
        p.stageText = QObject::tr("Circle preview"); // 圆预览
    }
    return p;
}

void CircleCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

// ============================================================================
// PolylineCommand 实现
// ============================================================================

PolylineCommand::PolylineCommand() = default;

QString PolylineCommand::commandId() const { return QStringLiteral("2d.draw_polyline"); }
QString PolylineCommand::displayName() const { return QObject::tr("Polyline"); } // 折线
bool PolylineCommand::isInteractive() const { return true; }
CommandState PolylineCommand::state() const { return m_state; }

bool PolylineCommand::activate(const UiServices& services)
{
    m_services = &services;
    if (services.document2D)
        m_document = services.document2D;

    if (!m_document)
        return false;

    m_state = CommandState::Active;
    m_points.clear();
    m_currentPoint = QPointF();
    m_completed = false;
    return true;
}

void PolylineCommand::cancel()
{
    m_state = CommandState::Cancelled;
    m_points.clear();
    m_completed = false;
}

void PolylineCommand::commit()
{
    if (m_document && m_points.size() >= 2)
    {
        m_createdEntityId = m_document->createLine(m_points.front(), m_points.back());
        m_document->clearSelection();
        m_document->selectEntity(m_createdEntityId);
    }
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

    m_points.push_back(QPointF(x, y));
    m_currentPoint = QPointF(x, y);
    return true;
}

bool PolylineCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active || m_points.isEmpty())
        return false;

    m_currentPoint = QPointF(x, y);
    return true;
}

bool PolylineCommand::onMouseUp(int /*x*/, int /*y*/)
{
    if (m_state != CommandState::Active)
        return false;
    if (m_points.size() >= 2)
    {
        m_completed = true;
        return true;
    }
    return false;
}

ITool* PolylineCommand::activeTool() const { return nullptr; }
UndoCommand* PolylineCommand::createUndoCommand()
{
    if (m_document && !m_createdEntityId.isEmpty())
        return new PolylineUndoCommand(m_document, m_createdEntityId);
    return nullptr;
}

bool PolylineCommand::isComplete() const
{
    return m_completed && m_points.size() >= 2;
}

CommandPreview PolylineCommand::preview() const
{
    CommandPreview p;
    if (!m_points.isEmpty())
    {
        p.valid = true;
        p.previewStart = m_points.last();
        p.previewEnd = m_currentPoint;
        p.stageText = QObject::tr("Polyline point input (%1)").arg(qint64(m_points.size())); // 折线点输入 (%1)
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

// ============================================================================
// CopyCommand 实现
// ============================================================================

CopyCommand::CopyCommand() = default;

QString CopyCommand::commandId() const { return QStringLiteral("2d.copy"); }
QString CopyCommand::displayName() const { return QObject::tr("Copy"); } // 复制
bool CopyCommand::isInteractive() const { return true; }
CommandState CopyCommand::state() const { return m_state; }

bool CopyCommand::activate(const UiServices& services)
{
    m_services = &services;
    if (services.document2D)
        m_document = services.document2D;

    if (!m_document)
        return false;

    if (m_document->selectedIds().isEmpty())
        return false;

    m_state = CommandState::Active;
    m_hasAnchor = false;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    m_copiedEntityIds.clear();
    return true;
}

void CopyCommand::cancel()
{
    m_state = CommandState::Cancelled;
    m_hasAnchor = false;
    m_copiedEntityIds.clear();
}

void CopyCommand::commit()
{
    if (!m_document || !m_hasAnchor || !m_copiedEntityIds.isEmpty())
        return;

    const QPointF delta = m_targetPoint - m_anchorPoint;
    auto* sm = m_document->sceneManager();
    if (!sm) return;

    for (auto* entity : sm->getSelectedEntities())
    {
        auto copy = entity->clone();
        copy->transform(Ut::Mat3d::translate(delta.x(), delta.y()));
        sm->addEntity(copy.release());
        m_copiedEntityIds.append(QString::number(copy->id));
    }

    if (!m_copiedEntityIds.isEmpty())
    {
        m_document->clearSelection();
        for (const auto& id : m_copiedEntityIds)
            m_document->selectEntity(id);
    }

    m_state = CommandState::Committed;
}

void CopyCommand::reset()
{
    m_state = CommandState::Idle;
    m_hasAnchor = false;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    m_copiedEntityIds.clear();
}

bool CopyCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (!m_hasAnchor)
    {
        m_hasAnchor = true;
        m_anchorPoint = QPointF(x, y);
        m_targetPoint = QPointF(x, y);
    }
    return true;
}

bool CopyCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active || !m_hasAnchor)
        return false;

    m_targetPoint = QPointF(x, y);
    return true;
}

bool CopyCommand::onMouseUp(int x, int y)
{
    if (m_state != CommandState::Active || !m_hasAnchor)
        return false;

    m_targetPoint = QPointF(x, y);
    return true;
}

ITool* CopyCommand::activeTool() const { return nullptr; }

UndoCommand* CopyCommand::createUndoCommand()
{
    if (m_document && !m_copiedEntityIds.isEmpty())
        return new CopyUndoCommand(m_document, m_copiedEntityIds);
    return nullptr;
}

bool CopyCommand::isComplete() const
{
    return m_state == CommandState::Active && m_hasAnchor && m_anchorPoint != m_targetPoint;
}

CommandPreview CopyCommand::preview() const
{
    CommandPreview p;
    return p;
}

void CopyCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

SimpleCommandHandler::SimpleCommandHandler(const QString& commandId, const QString& displayName)
    : m_commandId(commandId)
    , m_displayName(displayName)
{
}

QString SimpleCommandHandler::commandId() const
{
    return m_commandId;
}

QString SimpleCommandHandler::displayName() const
{
    return m_displayName;
}

bool SimpleCommandHandler::isInteractive() const
{
    return false;
}

CommandState SimpleCommandHandler::state() const
{
    return m_state;
}

bool SimpleCommandHandler::activate(const UiServices& /*services*/)
{
    m_state = CommandState::Active;
    return true;
}

void SimpleCommandHandler::cancel()
{
    m_state = CommandState::Cancelled;
}

void SimpleCommandHandler::commit()
{
    m_state = CommandState::Committed;
}

void SimpleCommandHandler::reset()
{
    m_state = CommandState::Idle;
}
