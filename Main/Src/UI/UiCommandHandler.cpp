#include "UiCommandHandler.h"

#include "UiEntities.h"
#include "UiServices.h"

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
    return QStringLiteral("拾点工具");
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
        return QStringLiteral("等待第一点");
    if (count < static_cast<size_t>(m_requiredPoints))
        return QStringLiteral("等待第%1点").arg(count + 1);
    return QStringLiteral("点收集完成");
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
    DrawLineUndoCommand(EntityDocument2D* document, const QString& entityId,
                        const QPointF& start, const QPointF& end)
        : UndoCommand(QStringLiteral("画线"))
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
        {
            auto line = m_document->createLine(m_start, m_end);
            if (line)
                m_entityId = line->id();
        }
    }

private:
    EntityDocument2D* m_document;
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
    return QStringLiteral("画线");
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

    // 优先使用 services 中的文档，兼容 setDocument() 注入
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
            auto line = m_document->createLine(points[0], points[1]);
            if (line)
            {
                m_createdEntityId = line->id();
                m_document->selection().clear();
                m_document->selection().add(line);
            }
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
    // 拾点工具收集到足够点数后，命令完成
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

void DrawLineCommand::setDocument(EntityDocument2D* document)
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
    RotateUndoCommand(EntityDocument2D* document, const QString& entityId,
                      const std::vector<QPointF>& originalPoints,
                      const std::vector<QPointF>& newPoints)
        : UndoCommand(QStringLiteral("旋转"))
        , m_document(document)
        , m_entityId(entityId)
        , m_originalPoints(originalPoints)
        , m_newPoints(newPoints)
    {}

    void undo() override
    {
        if (!m_document || m_entityId.isEmpty())
            return;

        auto entity = m_document->entityById(m_entityId);
        if (!entity)
            return;

        auto transformable = dynamic_cast<ITransformable*>(entity.get());
        if (transformable && !m_originalPoints.empty())
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : m_originalPoints)
                qtPoints.push_back(pt);
            transformable->setKeyPoints(qtPoints);
        }
    }

    void redo() override
    {
        if (!m_document || m_entityId.isEmpty())
            return;

        auto entity = m_document->entityById(m_entityId);
        if (!entity)
            return;

        auto transformable = dynamic_cast<ITransformable*>(entity.get());
        if (transformable && !m_newPoints.empty())
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : m_newPoints)
                qtPoints.push_back(pt);
            transformable->setKeyPoints(qtPoints);
        }
    }

private:
    EntityDocument2D* m_document;
    QString m_entityId;
    std::vector<QPointF> m_originalPoints;
    std::vector<QPointF> m_newPoints;
};

} // namespace

namespace {

class SelectUndoCommand : public UndoCommand
{
public:
    SelectUndoCommand(EntityDocument2D* document, const QString& oldSelectedId, const QString& newSelectedId)
        : UndoCommand(QStringLiteral("选择"))
        , m_document(document)
        , m_oldSelectedId(oldSelectedId)
        , m_newSelectedId(newSelectedId)
    {}

    void undo() override
    {
        if (!m_document)
            return;

        m_document->selection().clear();
        if (!m_oldSelectedId.isEmpty())
        {
            if (auto entity = m_document->entityById(m_oldSelectedId))
                m_document->selection().add(entity);
        }
    }

    void redo() override
    {
        if (!m_document)
            return;

        m_document->selection().clear();
        if (!m_newSelectedId.isEmpty())
        {
            if (auto entity = m_document->entityById(m_newSelectedId))
                m_document->selection().add(entity);
        }
    }

private:
    EntityDocument2D* m_document;
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
    return QStringLiteral("选择");
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
        m_document->selection().clear();
        if (!m_oldSelectedId.isEmpty())
        {
            if (auto entity = m_document->entityById(m_oldSelectedId))
                m_document->selection().add(entity);
        }
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

    if (!m_document->selection().empty())
    {
        auto items = m_document->selection().items();
        if (!items.isEmpty())
            m_oldSelectedId = items[0]->id();
    }

    m_document->selection().clear();

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

    QRectF rect = QRectF(m_boxSelectStart, m_boxSelectEnd).normalized();
    for (const auto& entity : m_document->entities())
    {
        if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
        {
            if (rect.intersects(line->bounds()))
                m_document->selection().add(line);
        }
        else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
        {
            if (rect.intersects(polyline->bounds()))
                m_document->selection().add(polyline);
        }
        else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
        {
            if (rect.intersects(circle->bounds()))
                m_document->selection().add(circle);
        }
        else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
        {
            const QRectF arcBounds(arc->center().x() - arc->radius(), arc->center().y() - arc->radius(),
                arc->radius() * 2.0, arc->radius() * 2.0);
            if (rect.intersects(arcBounds))
                m_document->selection().add(arc);
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
        p.stageText = QStringLiteral("框选模式");
    }
    return p;
}

void SelectCommand::setDocument(EntityDocument2D* document)
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
    return QStringLiteral("旋转");
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

    auto entity = m_document->entityById(m_selectedEntityId);
    if (!entity)
        return;

    auto transformable = dynamic_cast<ITransformable*>(entity.get());
    if (transformable)
    {
        QVector<QPointF> qtPoints;
        for (const auto& pt : m_originalPoints)
            qtPoints.push_back(pt);
        transformable->setKeyPoints(qtPoints);
    }
}

bool RotateCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_startPoint = QPointF(x, y);
    m_rotationCenter = QPointF(x, y);

    if (m_document && !m_document->selection().empty())
    {
        auto selected = m_document->selection().items();
        if (!selected.empty())
        {
            m_selectedEntityId = selected[0]->id();
            auto entity = m_document->entityById(m_selectedEntityId);
            if (entity)
            {
                auto transformable = dynamic_cast<ITransformable*>(entity.get());
                if (transformable)
                {
                    auto keyPoints = transformable->keyPoints();
                    for (const auto& pt : keyPoints)
                        m_originalPoints.push_back(pt);
                    m_rotationCenter = transformable->center();
                }
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
        auto entity = m_document->entityById(m_selectedEntityId);
        if (entity)
        {
            auto transformable = dynamic_cast<ITransformable*>(entity.get());
            if (transformable)
            {
                double cosAngle = cos(angleDelta);
                double sinAngle = sin(angleDelta);
                transformable->rotate(m_rotationCenter, cosAngle, sinAngle);
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

    auto entity = m_document->entityById(m_selectedEntityId);
    if (!entity)
        return nullptr;

    auto transformable = dynamic_cast<ITransformable*>(entity.get());
    if (!transformable)
        return nullptr;

    std::vector<QPointF> newPoints;
    auto keyPoints = transformable->keyPoints();
    for (const auto& pt : keyPoints)
        newPoints.push_back(pt);

    return new RotateUndoCommand(m_document, m_selectedEntityId, m_originalPoints, newPoints);
}

bool RotateCommand::isComplete() const
{
    // 旋转命令在鼠标释放后完成
    return m_state == CommandState::Active && m_startAngle != m_currentAngle;
}

void RotateCommand::setDocument(EntityDocument2D* document)
{
    m_document = document;
}

// ============================================================================
// MoveCommand 实现（新架构落地样板 P0-12）
// ============================================================================

MoveCommand::MoveCommand() = default;

QString MoveCommand::commandId() const
{
    return QStringLiteral("2d.move");
}

QString MoveCommand::displayName() const
{
    return QStringLiteral("移动");
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

    if (m_document->selection().empty())
        return false;

    // 保存原始位置快照（用于 createUndoCommand）
    // 生命周期契约：activate() 不修改文档，仅保存状态用于撤销
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
    // 生命周期契约：cancel() 不修改文档（文档变更仅在 commit() 中发生）
    // onMouseUp 只记录状态，不需要 restoreOriginalPositions()
    m_state = CommandState::Cancelled;
    m_originalPositions.clear();
    m_hasAnchor = false;
    m_committed = false;
}

void MoveCommand::commit()
{
    // 生命周期契约：文档变更必须且只能在 commit() 中发生
    // onMouseUp 仅记录状态，cancel/preview/submit失败 绝不修改文档
    const QPointF delta = m_targetPoint - m_anchorPoint;
    if (m_document && !m_committed)
    {
        for (const auto& entity : m_document->selection().items())
        {
            if (!entity)
                continue;

            if (auto transformable = dynamic_cast<ITransformable*>(entity.get()))
            {
                transformable->translate(delta);
            }
            else if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
            {
                polyline->translate(delta);
            }
            else if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
            {
                circle->setCenter(circle->center() + delta);
            }
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
        p.stageText = QStringLiteral("移动预览中");
    }
    return p;
}

void MoveCommand::setDocument(EntityDocument2D* document)
{
    m_document = document;
}

void MoveCommand::saveOriginalPositions()
{
    m_originalPositions.clear();
    if (!m_document)
        return;

    for (const auto& entity : m_document->selection().items())
    {
        if (!entity)
            continue;

        if (auto transformable = dynamic_cast<ITransformable*>(entity.get()))
        {
            std::vector<QPointF> pts;
            for (const auto& pt : transformable->keyPoints())
                pts.push_back(pt);
            m_originalPositions[entity->id()] = pts;
        }
        else if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
        {
            std::vector<QPointF> pts;
            for (const auto& pt : polyline->points())
                pts.push_back(pt);
            m_originalPositions[entity->id()] = pts;
        }
        else if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
        {
            m_originalPositions[entity->id()] = { circle->center(), QPointF(circle->radius(), 0) };
        }
    }
}

void MoveCommand::restoreOriginalPositions()
{
    if (!m_document)
        return;

    for (const auto& pair : m_originalPositions)
    {
        const auto& entityId = pair.first;
        const auto& pts = pair.second;

        auto entity = m_document->entityById(entityId);
        if (!entity)
            continue;

        if (auto transformable = dynamic_cast<ITransformable*>(entity.get()))
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : pts)
                qtPoints.push_back(pt);
            transformable->setKeyPoints(qtPoints);
        }
        else if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : pts)
                qtPoints.push_back(pt);
            polyline->setPoints(qtPoints);
        }
        else if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
        {
            if (!pts.empty())
                circle->setCenter(pts[0]);
        }
    }
}

// ============================================================================
// MoveUndoCommand 实现
// ============================================================================

MoveUndoCommand::MoveUndoCommand(EntityDocument2D* document,
                                 std::map<QString, std::vector<QPointF>> originalPositions)
    : UndoCommand(QStringLiteral("移动"))
    , m_document(document)
    , m_originalPositions(std::move(originalPositions))
{
    // 保存移动后的位置（用于 redo）
    for (const auto& pair : m_originalPositions)
    {
        const auto& entityId = pair.first;

        auto entity = m_document->entityById(entityId);
        if (!entity)
            continue;

        if (auto transformable = dynamic_cast<ITransformable*>(entity.get()))
        {
            std::vector<QPointF> pts;
            for (const auto& pt : transformable->keyPoints())
                pts.push_back(pt);
            m_newPositions[entityId] = pts;
        }
        else if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
        {
            std::vector<QPointF> pts;
            for (const auto& pt : polyline->points())
                pts.push_back(pt);
            m_newPositions[entityId] = pts;
        }
        else if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
        {
            m_newPositions[entityId] = { circle->center(), QPointF(circle->radius(), 0) };
        }
    }
}

void MoveUndoCommand::undo()
{
    if (!m_document)
        return;

    for (const auto& pair : m_originalPositions)
    {
        const auto& entityId = pair.first;
        const auto& pts = pair.second;

        auto entity = m_document->entityById(entityId);
        if (!entity)
            continue;

        if (auto transformable = dynamic_cast<ITransformable*>(entity.get()))
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : pts)
                qtPoints.push_back(pt);
            transformable->setKeyPoints(qtPoints);
        }
        else if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : pts)
                qtPoints.push_back(pt);
            polyline->setPoints(qtPoints);
        }
        else if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
        {
            if (!pts.empty())
                circle->setCenter(pts[0]);
        }
    }
}

void MoveUndoCommand::redo()
{
    if (!m_document)
        return;

    for (const auto& pair : m_newPositions)
    {
        const auto& entityId = pair.first;
        const auto& pts = pair.second;

        auto entity = m_document->entityById(entityId);
        if (!entity)
            continue;

        if (auto transformable = dynamic_cast<ITransformable*>(entity.get()))
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : pts)
                qtPoints.push_back(pt);
            transformable->setKeyPoints(qtPoints);
        }
        else if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
        {
            QVector<QPointF> qtPoints;
            for (const auto& pt : pts)
                qtPoints.push_back(pt);
            polyline->setPoints(qtPoints);
        }
        else if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
        {
            if (!pts.empty())
                circle->setCenter(pts[0]);
        }
    }
}

CircleUndoCommand::CircleUndoCommand(EntityDocument2D* document, const QString& entityId)
    : UndoCommand(QStringLiteral("画圆"))
    , m_document(document)
    , m_entityId(entityId)
{
    if (m_document)
    {
        auto entity = m_document->entityById(entityId);
        if (auto circle = dynamic_cast<CircleEntity2D*>(entity.get()))
        {
            m_center = circle->center();
            m_radius = circle->radius();
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
    m_document->createCircle(m_center, m_radius);
}

PolylineUndoCommand::PolylineUndoCommand(EntityDocument2D* document, const QString& entityId)
    : UndoCommand(QStringLiteral("画折线"))
    , m_document(document)
    , m_entityId(entityId)
{
    if (m_document)
    {
        auto entity = m_document->entityById(entityId);
        if (auto polyline = dynamic_cast<PolylineEntity2D*>(entity.get()))
        {
            m_points = polyline->points();
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
    m_document->createPolyline(m_points);
}

CopyUndoCommand::CopyUndoCommand(EntityDocument2D* document, const QStringList& copiedEntityIds)
    : UndoCommand(QStringLiteral("复制"))
    , m_document(document)
    , m_copiedEntityIds(copiedEntityIds)
{
    if (m_document)
    {
        for (const auto& id : copiedEntityIds)
        {
            if (auto entity = m_document->entityById(id))
                m_copiedEntities.push_back(entity);
        }
    }
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
    for (const auto& entity : m_copiedEntities)
    {
        if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
            m_document->createLine(line->start(), line->end());
        else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
            m_document->createCircle(circle->center(), circle->radius());
        else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
            m_document->createPolyline(polyline->points());
    }
}

// ============================================================================
// CircleCommand 实现（画图命令）
// ============================================================================

CircleCommand::CircleCommand() = default;

QString CircleCommand::commandId() const { return QStringLiteral("2d.draw_circle"); }
QString CircleCommand::displayName() const { return QStringLiteral("画圆"); }
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
            auto entity = m_document->createCircle(m_center, radius);
            m_createdEntityId = entity->id();
            m_document->selection().clear();
            m_document->selection().add(entity);
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
        p.stageText = QStringLiteral("圆预览中");
    }
    return p;
}

void CircleCommand::setDocument(EntityDocument2D* document)
{
    m_document = document;
}

// ============================================================================
// PolylineCommand 实现（折线命令）
// ============================================================================

PolylineCommand::PolylineCommand() = default;

QString PolylineCommand::commandId() const { return QStringLiteral("2d.draw_polyline"); }
QString PolylineCommand::displayName() const { return QStringLiteral("折线"); }
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
        auto entity = m_document->createPolyline(m_points);
        m_createdEntityId = entity->id();
        m_document->selection().clear();
        m_document->selection().add(entity);
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
        p.stageText = QStringLiteral("折线点输入中 (%1)").arg(qint64(m_points.size()));
    }
    return p;
}

void PolylineCommand::setDocument(EntityDocument2D* document)
{
    m_document = document;
}

void PolylineCommand::finish()
{
    m_completed = true;
}

// ============================================================================
// CopyCommand 实现（复制命令）
// ============================================================================

CopyCommand::CopyCommand() = default;

QString CopyCommand::commandId() const { return QStringLiteral("2d.copy"); }
QString CopyCommand::displayName() const { return QStringLiteral("复制"); }
bool CopyCommand::isInteractive() const { return true; }
CommandState CopyCommand::state() const { return m_state; }

bool CopyCommand::activate(const UiServices& services)
{
    m_services = &services;
    if (services.document2D)
        m_document = services.document2D;

    if (!m_document)
        return false;

    if (m_document->selection().empty())
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
    if (!m_document || !m_hasAnchor || m_copiedEntityIds.size() > 0)
        return;

    const QPointF delta = m_targetPoint - m_anchorPoint;
    for (const auto& entity : m_document->selection().items())
    {
        if (!entity)
            continue;

        if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
        {
            auto newLine = m_document->createLine(line->start() + delta, line->end() + delta);
            m_copiedEntityIds.append(newLine->id());
        }
        else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
        {
            auto newCircle = m_document->createCircle(circle->center() + delta, circle->radius());
            m_copiedEntityIds.append(newCircle->id());
        }
        else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
        {
            auto pts = polyline->points();
            for (auto& pt : pts)
                pt += delta;
            auto newPolyline = m_document->createPolyline(pts);
            m_copiedEntityIds.append(newPolyline->id());
        }
    }

    if (!m_copiedEntityIds.isEmpty())
    {
        m_document->selection().clear();
        for (const auto& id : m_copiedEntityIds)
        {
            if (auto entity = m_document->entityById(id))
                m_document->selection().add(entity);
        }
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

void CopyCommand::setDocument(EntityDocument2D* document)
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
