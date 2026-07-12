#include "UiCommandHandler.h"

#include <QObject>

#include "UiServices.h"
#include "SceneDocument2D.h"
#include "CommandSnapshots.h"
#include "Engine2D/Core/SceneManager.h"

namespace
{
    class DefaultTool : public ITool
    {
    public:
        explicit DefaultTool(const QString& toolId, const QString& displayName)
            : m_toolId(toolId)
            , m_displayName(displayName)
        {
        }

        QString toolId() const override
        {
            return m_toolId;
        }
        QString displayName() const override
        {
            return m_displayName;
        }
        bool activate(const UiServices& /*services*/) override
        {
            return true;
        }
        void cancel() override
        {
        }
        void reset() override
        {
        }

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
    return QObject::tr("Point Picker");
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
        return QObject::tr("Waiting for first point");
    if (count < static_cast<size_t>(m_requiredPoints))
        return QObject::tr("Waiting for point %1").arg(count + 1);
    return QObject::tr("Points collected");
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

    // 命令压栈后通知视图刷新
    notifyRefresh();
}

bool DefaultUndoStack::undo()
{
    if (!canUndo())
        return false;

    m_stack[m_currentIndex]->undo();
    m_currentIndex--;
    notifyRefresh();
    return true;
}

bool DefaultUndoStack::redo()
{
    if (!canRedo())
        return false;

    m_currentIndex++;
    m_stack[m_currentIndex]->redo();
    notifyRefresh();
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

void DefaultUndoStack::setRefreshCallback(RefreshCallback callback)
{
    m_refreshCallback = std::move(callback);
}

void DefaultUndoStack::notifyRefresh()
{
    if (m_isNotifying)
        return;

    m_isNotifying = true;
    if (m_refreshCallback)
        m_refreshCallback();
    m_isNotifying = false;
}

SnapshotUndoCommand::SnapshotUndoCommand(const QString& text, SceneDocument2D* document, const QVector<EntitySnapshot>& snapshots)
    : UndoCommand(text)
    , m_document(document)
    , m_snapshots(snapshots)
{
}

void SnapshotUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    m_storedEntities.clear();
    for (const auto& snap : m_snapshots)
    {
        bool ok = false;
        const Eg::EntityId eid = static_cast<Eg::EntityId>(snap.id.toULongLong(&ok));
        if (!ok)
            continue;

        auto entity = sm->extractEntityById(eid);
        if (entity)
        {
            m_storedEntities.push_back(std::move(entity));
        }
    }
}

void SnapshotUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    if (!m_storedEntities.empty())
    {
        for (auto& entity : m_storedEntities)
        {
            sm->insertEntityPreserveId(std::move(entity));
        }
        m_storedEntities.clear();
    }
    else
    {
        for (const auto& snap : m_snapshots)
            restoreFromSnapshot(m_document, snap);
    }
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