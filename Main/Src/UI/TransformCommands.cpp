#include "TransformCommands.h"

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
#include "CommandGeometry.h"
#include "Log/SyLogger.h"

MoveUndoCommand::MoveUndoCommand(SceneDocument2D* document,
                                 std::map<QString, std::vector<QPointF>> originalPositions)
    : UndoCommand(QObject::tr("Move"))
    , m_document(document)
    , m_originalPositions(std::move(originalPositions))
{
}

void MoveUndoCommand::undo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    m_newPositions.clear();
    for (const auto& pair : m_originalPositions)
    {
        const QString& id = pair.first;
        const auto& points = pair.second;
        auto* entity = sm->findEntityById(id.toULongLong());
        if (!entity)
            continue;

        std::vector<QPointF> currentPoints;
        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
                currentPoints.emplace_back(pt.x(), pt.y());
            line->vPoints.clear();
            for (const auto& pt : points)
                line->vPoints.push_back(toVec2d(pt));
            line->basePoint = line->vPoints.front();
            line->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            for (const auto& v : polygon->vVertices)
                currentPoints.emplace_back(v.x(), v.y());
            polygon->vVertices.clear();
            for (const auto& pt : points)
                polygon->vVertices.push_back(toVec2d(pt));
            polygon->basePoint = polygon->vVertices.front();
            polygon->setModified();
            break;
        }
        case Eg::EType::ARC:
        {
            auto* arc = static_cast<Eg::SyArc*>(entity);
            currentPoints.emplace_back(arc->basePoint.x(), arc->basePoint.y());
            arc->basePoint = toVec2d(points.front());
            arc->setModified();
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            currentPoints.emplace_back(circle->basePoint.x(), circle->basePoint.y());
            circle->basePoint = toVec2d(points.front());
            circle->setModified();
            break;
        }
        default:
            break;
        }
        m_newPositions[id] = currentPoints;
    }
}

void MoveUndoCommand::redo()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    for (const auto& pair : m_newPositions)
    {
        const QString& id = pair.first;
        const auto& points = pair.second;
        auto* entity = sm->findEntityById(id.toULongLong());
        if (!entity)
            continue;

        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            line->vPoints.clear();
            for (const auto& pt : points)
                line->vPoints.push_back(toVec2d(pt));
            line->basePoint = line->vPoints.front();
            line->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            polygon->vVertices.clear();
            for (const auto& pt : points)
                polygon->vVertices.push_back(toVec2d(pt));
            polygon->basePoint = polygon->vVertices.front();
            polygon->setModified();
            break;
        }
        case Eg::EType::ARC:
        {
            auto* arc = static_cast<Eg::SyArc*>(entity);
            arc->basePoint = toVec2d(points.front());
            arc->setModified();
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            circle->basePoint = toVec2d(points.front());
            circle->setModified();
            break;
        }
        default:
            break;
        }
    }
}

CopyUndoCommand::CopyUndoCommand(SceneDocument2D* document, const QStringList& copiedEntityIds)
    : UndoCommand(QObject::tr("Copy"))
    , m_document(document)
    , m_copiedEntityIds(copiedEntityIds)
{
}

CopyUndoCommand::CopyUndoCommand(SceneDocument2D* document, const QVector<EntitySnapshot>& snapshots)
    : UndoCommand(QObject::tr("Copy"))
    , m_document(document)
    , m_snapshots(snapshots)
{
}

void CopyUndoCommand::undo()
{
    if (!m_document)
        return;

    for (const auto& id : m_copiedEntityIds)
        m_document->removeEntity(id);

    if (!m_oldSelection.isEmpty() && m_document)
        m_document->setSelectedEntityIds(m_oldSelection);
}

void CopyUndoCommand::redo()
{
    if (!m_document)
        return;

    for (const auto& snap : m_snapshots)
        restoreFromSnapshot(m_document, snap);

    if (!m_copiedEntityIds.isEmpty() && m_document)
        m_document->setSelectedEntityIds(m_copiedEntityIds);
}

MoveCommand::MoveCommand()
{
}

QString MoveCommand::commandId() const
{
    return QStringLiteral("2d.move");
}

QString MoveCommand::displayName() const
{
    return QObject::tr("Move");
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
    SY_INFO("[TransformCommands] MoveCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    m_hasAnchor = false;
    m_committed = false;
    m_originalPositions.clear();
    saveOriginalPositions();
    SY_DEBUGF("[TransformCommands] MoveCommand saved %d original positions", m_originalPositions.size());
    return true;
}

void MoveCommand::cancel()
{
    SY_INFO("[TransformCommands] MoveCommand cancelled");
    restoreOriginalPositions();
    m_state = CommandState::Cancelled;
}

void MoveCommand::commit()
{
    SY_INFOF("[TransformCommands] MoveCommand committed: %d entities moved", m_originalPositions.size());
    m_committed = true;
    m_state = CommandState::Committed;
}

void MoveCommand::reset()
{
    m_state = CommandState::Idle;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    m_hasAnchor = false;
    m_committed = false;
    m_originalPositions.clear();
}

void MoveCommand::saveOriginalPositions()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    const QStringList selectedIds = m_document->selectedIdsQ();
    for (const QString& idStr : selectedIds)
    {
        bool ok = false;
        const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
        if (!ok)
            continue;

        auto* entity = sm->findEntityById(id);
        if (!entity)
            continue;

        std::vector<QPointF> points;
        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
                points.emplace_back(pt.x(), pt.y());
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            for (const auto& v : polygon->vVertices)
                points.emplace_back(v.x(), v.y());
            break;
        }
        case Eg::EType::ARC:
        {
            auto* arc = static_cast<Eg::SyArc*>(entity);
            points.emplace_back(arc->basePoint.x(), arc->basePoint.y());
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            points.emplace_back(circle->basePoint.x(), circle->basePoint.y());
            break;
        }
        default:
            break;
        }
        m_originalPositions[idStr] = points;
    }
}

void MoveCommand::restoreOriginalPositions()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    for (const auto& pair : m_originalPositions)
    {
        const QString& idStr = pair.first;
        const auto& points = pair.second;
        bool ok = false;
        const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
        if (!ok)
            continue;

        auto* entity = sm->findEntityById(id);
        if (!entity)
            continue;

        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            line->vPoints.clear();
            for (const auto& pt : points)
                line->vPoints.push_back(toVec2d(pt));
            line->basePoint = line->vPoints.front();
            line->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            polygon->vVertices.clear();
            for (const auto& pt : points)
                polygon->vVertices.push_back(toVec2d(pt));
            polygon->basePoint = polygon->vVertices.front();
            polygon->setModified();
            break;
        }
        case Eg::EType::ARC:
        {
            auto* arc = static_cast<Eg::SyArc*>(entity);
            arc->basePoint = toVec2d(points.front());
            arc->setModified();
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            circle->basePoint = toVec2d(points.front());
            circle->setModified();
            break;
        }
        default:
            break;
        }
    }
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
    if (m_state != CommandState::Active)
        return false;

    if (!m_hasAnchor)
        return false;

    m_targetPoint = QPointF(x, y);

    if (!m_document || m_originalPositions.empty())
        return true;

    const double dx = m_targetPoint.x() - m_anchorPoint.x();
    const double dy = m_targetPoint.y() - m_anchorPoint.y();

    if (std::fabs(dx) < 1e-8 && std::fabs(dy) < 1e-8)
        return true;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return true;

    restoreOriginalPositions();

    for (const auto& pair : m_originalPositions)
    {
        const QString& idStr = pair.first;
        const auto& points = pair.second;
        bool ok = false;
        const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
        if (!ok)
            continue;

        auto* entity = sm->findEntityById(id);
        if (!entity)
            continue;

        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (size_t i = 0; i < points.size() && i < line->vPoints.size(); ++i)
            {
                line->vPoints[i] = Ut::Vec2d(points[i].x() + dx, points[i].y() + dy);
            }
            line->basePoint = line->vPoints.front();
            line->setModified();
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            for (size_t i = 0; i < points.size() && i < polygon->vVertices.size(); ++i)
            {
                polygon->vVertices[i] = Ut::Vec2d(points[i].x() + dx, points[i].y() + dy);
            }
            polygon->basePoint = polygon->vVertices.front();
            polygon->setModified();
            break;
        }
        case Eg::EType::ARC:
        {
            auto* arc = static_cast<Eg::SyArc*>(entity);
            arc->basePoint = Ut::Vec2d(points.front().x() + dx, points.front().y() + dy);
            arc->setModified();
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            circle->basePoint = Ut::Vec2d(points.front().x() + dx, points.front().y() + dy);
            circle->setModified();
            break;
        }
        default:
            break;
        }
    }
    return true;
}

bool MoveCommand::onMouseUp(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (!m_hasAnchor)
        return false;

    m_targetPoint = QPointF(x, y);

    const double dx = m_targetPoint.x() - m_anchorPoint.x();
    const double dy = m_targetPoint.y() - m_anchorPoint.y();

    if (!(std::fabs(dx) < 1e-8 && std::fabs(dy) < 1e-8))
    {
        if (m_document)
        {
            auto* sm = m_document->sceneManager();
            if (sm)
            {
                for (const auto& pair : m_originalPositions)
                {
                    const QString& idStr = pair.first;
                    bool ok = false;
                    const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
                    if (!ok)
                        continue;

                    auto* entity = sm->findEntityById(id);
                    if (!entity)
                        continue;

                    entity->setModified();
                }
            }
        }
    }

    commit();
    return true;
}

ITool* MoveCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* MoveCommand::createUndoCommand()
{
    if (m_originalPositions.empty())
        return nullptr;

    return new MoveUndoCommand(m_document, m_originalPositions);
}

bool MoveCommand::isComplete() const
{
    return m_committed;
}

CommandPreview MoveCommand::preview() const
{
    return CommandPreview();
}

void MoveCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
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
    return QObject::tr("Rotate");
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
    SY_INFO("[TransformCommands] RotateCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_stage = 0;
    m_selectedEntityId.clear();
    m_originalPoints.clear();
    m_originalSnapshots.clear();

    if (m_document)
    {
        const QStringList selectedIds = m_document->selectedIdsQ();
        if (!selectedIds.isEmpty())
        {
            m_selectedEntityId = selectedIds.first();
            auto* sm = m_document->sceneManager();
            if (sm)
            {
                for (const QString& idStr : selectedIds)
                {
                    bool ok = false;
                    const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
                    if (!ok)
                        continue;
                    auto* entity = sm->findEntityById(id);
                    if (entity)
                        m_originalSnapshots.append(takeSnapshot(entity));
                }
                SY_DEBUGF("[TransformCommands] RotateCommand saved %d snapshots", m_originalSnapshots.size());

                bool ok = false;
                const Eg::EntityId id = static_cast<Eg::EntityId>(m_selectedEntityId.toULongLong(&ok));
                if (ok)
                {
                    auto* entity = sm->findEntityById(id);
                    if (entity)
                    {
                        switch (entity->eType)
                        {
                        case Eg::EType::LINE:
                        {
                            auto* line = static_cast<Eg::SyLine*>(entity);
                            for (const auto& pt : line->vPoints)
                                m_originalPoints.emplace_back(pt.x(), pt.y());
                            break;
                        }
                        case Eg::EType::POLYGON:
                        {
                            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
                            for (const auto& v : polygon->vVertices)
                                m_originalPoints.emplace_back(v.x(), v.y());
                            break;
                        }
                        case Eg::EType::ARC:
                        case Eg::EType::CIRCLE:
                        {
                            m_originalPoints.emplace_back(entity->basePoint.x(), entity->basePoint.y());
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }
            computeDefaultCenter();
        }
    }
    return true;
}

void RotateCommand::computeDefaultCenter()
{
    if (m_originalPoints.empty())
    {
        m_rotationCenter = QPointF(0, 0);
        return;
    }

    QPointF center;
    for (const auto& pt : m_originalPoints)
    {
        center.rx() += pt.x();
        center.ry() += pt.y();
    }
    center.rx() /= m_originalPoints.size();
    center.ry() /= m_originalPoints.size();
    m_rotationCenter = center;
}

void RotateCommand::cancel()
{
    SY_INFO("[TransformCommands] RotateCommand cancelled");
    restoreOriginalPoints();
    m_state = CommandState::Cancelled;
}

void RotateCommand::commit()
{
    SY_INFOF("[TransformCommands] RotateCommand committed: %d entities rotated", m_originalSnapshots.size());
    m_state = CommandState::Committed;
}

void RotateCommand::reset()
{
    m_state = CommandState::Idle;
    m_stage = 0;
    m_selectedEntityId.clear();
    m_originalPoints.clear();
    m_originalSnapshots.clear();
    m_startAngle = 0.0;
    m_currentAngle = 0.0;
}

void RotateCommand::restoreOriginalPoints()
{
    if (!m_document)
        return;

    for (const auto& snap : m_originalSnapshots)
    {
        auto* sm = m_document->sceneManager();
        if (!sm)
            break;

        bool ok = false;
        const Eg::EntityId id = static_cast<Eg::EntityId>(snap.id.toULongLong(&ok));
        if (!ok)
            continue;

        auto* entity = sm->findEntityById(id);
        if (entity)
            restoreEntityGeometryFromSnapshot(entity, snap);
    }
}

bool RotateCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage == 0)
    {
        m_rotationCenter = QPointF(x, y);
        m_stage = 1;
        return true;
    }
    else if (m_stage == 1)
    {
        m_startPoint = QPointF(x, y);
        m_startAngle = std::atan2(y - m_rotationCenter.y(), x - m_rotationCenter.x());
        m_stage = 2;
        return true;
    }
    return false;
}

bool RotateCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage < 2)
        return true;

    const double angle = std::atan2(y - m_rotationCenter.y(), x - m_rotationCenter.x());
    const double delta = angle - m_startAngle;

    m_currentAngle = delta;
    m_startAngle = angle;

    if (!m_document)
        return true;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return true;

    const QStringList selectedIds = m_document->selectedIdsQ();
    for (const QString& idStr : selectedIds)
    {
        bool ok = false;
        const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
        if (!ok)
            continue;

        auto* entity = sm->findEntityById(id);
        if (entity)
            applyRotationToEntity(entity, m_rotationCenter, delta);
    }
    return true;
}

bool RotateCommand::onMouseUp(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);

    if (m_state == CommandState::Active && m_stage >= 2)
        commit();
    return true;
}

ITool* RotateCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* RotateCommand::createUndoCommand()
{
    if (m_originalSnapshots.isEmpty())
        return nullptr;

    return new SnapshotUndoCommand(QObject::tr("Rotate"), m_document, m_originalSnapshots);
}

bool RotateCommand::isComplete() const
{
    return m_state == CommandState::Committed;
}

void RotateCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

CopyCommand::CopyCommand()
{
}

QString CopyCommand::commandId() const
{
    return QStringLiteral("2d.copy");
}

QString CopyCommand::displayName() const
{
    return QObject::tr("Copy");
}

bool CopyCommand::isInteractive() const
{
    return true;
}

CommandState CopyCommand::state() const
{
    return m_state;
}

bool CopyCommand::activate(const UiServices& services)
{
    SY_INFO("[TransformCommands] CopyCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    m_hasAnchor = false;
    m_copiedEntityIds.clear();
    m_copiedSnapshots.clear();
    return true;
}

void CopyCommand::cancel()
{
    SY_INFO("[TransformCommands] CopyCommand cancelled");
    m_state = CommandState::Cancelled;
}

void CopyCommand::commit()
{
    SY_INFOF("[TransformCommands] CopyCommand committed: %d entities copied", m_copiedEntityIds.size());
    m_state = CommandState::Committed;
}

void CopyCommand::reset()
{
    m_state = CommandState::Idle;
    m_anchorPoint = QPointF();
    m_targetPoint = QPointF();
    m_hasAnchor = false;
    m_copiedEntityIds.clear();
    m_copiedSnapshots.clear();
}

bool CopyCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (!m_hasAnchor)
    {
        m_anchorPoint = QPointF(x, y);
        m_hasAnchor = true;
    }
    else
    {
        m_targetPoint = QPointF(x, y);

        if (!m_document)
        {
            commit();
            return true;
        }

        const double dx = m_targetPoint.x() - m_anchorPoint.x();
        const double dy = m_targetPoint.y() - m_anchorPoint.y();

        if (std::fabs(dx) < 1e-8 && std::fabs(dy) < 1e-8)
        {
            commit();
            return true;
        }

        auto* sm = m_document->sceneManager();
        if (!sm)
        {
            commit();
            return true;
        }

        const QStringList selectedIds = m_document->selectedIdsQ();
        for (const QString& idStr : selectedIds)
        {
            bool ok = false;
            const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
            if (!ok)
                continue;

            auto* entity = sm->findEntityById(id);
            if (!entity)
                continue;

            std::unique_ptr<Eg::SyEntity> copy;
            switch (entity->eType)
            {
            case Eg::EType::LINE:
                copy = std::make_unique<Eg::SyLine>(*static_cast<Eg::SyLine*>(entity));
                break;
            case Eg::EType::CIRCLE:
                copy = std::make_unique<Eg::SyCircle>(*static_cast<Eg::SyCircle*>(entity));
                break;
            case Eg::EType::ARC:
                copy = std::make_unique<Eg::SyArc>(*static_cast<Eg::SyArc*>(entity));
                break;
            case Eg::EType::POLYGON:
                copy = std::make_unique<Eg::SyPolygon>(*static_cast<Eg::SyPolygon*>(entity));
                break;
            default:
                continue;
            }

            switch (copy->eType)
            {
            case Eg::EType::LINE:
            {
                auto* line = static_cast<Eg::SyLine*>(copy.get());
                for (auto& pt : line->vPoints)
                {
                    pt = Ut::Vec2d(pt.x() + dx, pt.y() + dy);
                }
                break;
            }
            case Eg::EType::POLYGON:
            {
                auto* polygon = static_cast<Eg::SyPolygon*>(copy.get());
                for (auto& v : polygon->vVertices)
                {
                    v = Ut::Vec2d(v.x() + dx, v.y() + dy);
                }
                break;
            }
            default:
                break;
            }
            copy->basePoint = Ut::Vec2d(copy->basePoint.x() + dx, copy->basePoint.y() + dy);

            Eg::SyEntity* copiedRaw = copy.get();
            sm->addEntity(copy.get());
            m_copiedEntityIds.append(QString::number(copiedRaw->id));
            m_copiedSnapshots.append(takeSnapshot(copiedRaw));
        }

        commit();
    }
    return true;
}

bool CopyCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_hasAnchor)
        m_targetPoint = QPointF(x, y);
    return true;
}

bool CopyCommand::onMouseUp(int x, int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    return false;
}

ITool* CopyCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* CopyCommand::createUndoCommand()
{
    if (m_copiedEntityIds.isEmpty())
        return nullptr;

    return new CopyUndoCommand(m_document, m_copiedSnapshots);
}

bool CopyCommand::isComplete() const
{
    return !m_copiedEntityIds.isEmpty();
}

CommandPreview CopyCommand::preview() const
{
    return CommandPreview();
}

void CopyCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

DeleteCommand::DeleteCommand()
{
}

QString DeleteCommand::commandId() const
{
    return QStringLiteral("2d.delete");
}

QString DeleteCommand::displayName() const
{
    return QObject::tr("Delete");
}

bool DeleteCommand::isInteractive() const
{
    return false;
}

CommandState DeleteCommand::state() const
{
    return m_state;
}

bool DeleteCommand::activate(const UiServices& services)
{
    SY_INFO("[TransformCommands] DeleteCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_deletedEntityIds.clear();
    m_snapshots.clear();

    if (m_document)
    {
        auto* sm = m_document->sceneManager();
        if (sm)
        {
            const QStringList selectedIds = m_document->selectedIdsQ();
            for (const QString& idStr : selectedIds)
            {
                bool ok = false;
                const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
                if (!ok)
                    continue;

                auto* entity = sm->findEntityById(id);
                if (entity)
                {
                    m_snapshots.append(takeSnapshot(entity));
                    m_deletedEntityIds.append(idStr);
                }
            }
            for (const QString& id : m_deletedEntityIds)
                m_document->removeEntity(id);
        }
    }

    SY_INFOF("[TransformCommands] DeleteCommand committed: %d entities deleted", m_deletedEntityIds.size());
    m_committed = true;
    m_state = CommandState::Committed;
    return true;
}

void DeleteCommand::cancel()
{
    SY_INFO("[TransformCommands] DeleteCommand cancelled");
    m_state = CommandState::Cancelled;
}

void DeleteCommand::commit()
{
    SY_INFO("[TransformCommands] DeleteCommand commit");
    m_state = CommandState::Committed;
}

void DeleteCommand::reset()
{
    m_state = CommandState::Idle;
    m_deletedEntityIds.clear();
    m_snapshots.clear();
    m_committed = false;
}

ITool* DeleteCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* DeleteCommand::createUndoCommand()
{
    if (m_snapshots.isEmpty())
        return nullptr;

    return new CopyUndoCommand(m_document, m_snapshots);
}

bool DeleteCommand::isComplete() const
{
    return m_committed;
}

CommandPreview DeleteCommand::preview() const
{
    return CommandPreview();
}

void DeleteCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

MirrorCommand::MirrorCommand()
{
}

QString MirrorCommand::commandId() const
{
    return QStringLiteral("2d.mirror");
}

QString MirrorCommand::displayName() const
{
    return QObject::tr("Mirror");
}

bool MirrorCommand::isInteractive() const
{
    return true;
}

CommandState MirrorCommand::state() const
{
    return m_state;
}

bool MirrorCommand::activate(const UiServices& services)
{
    SY_INFO("[TransformCommands] MirrorCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_stage = 0;
    m_mirrorStart = QPointF();
    m_mirrorEnd = QPointF();
    m_mirroredEntityIds.clear();
    m_originalSnapshots.clear();
    m_mirroredSnapshots.clear();
    return true;
}

void MirrorCommand::cancel()
{
    SY_INFO("[TransformCommands] MirrorCommand cancelled");
    m_state = CommandState::Cancelled;
}

void MirrorCommand::commit()
{
    SY_INFOF("[TransformCommands] MirrorCommand committed: %d entities mirrored", m_mirroredEntityIds.size());
    m_state = CommandState::Committed;
}

void MirrorCommand::reset()
{
    m_state = CommandState::Idle;
    m_stage = 0;
    m_mirrorStart = QPointF();
    m_mirrorEnd = QPointF();
    m_mirroredEntityIds.clear();
    m_originalSnapshots.clear();
    m_mirroredSnapshots.clear();
}

bool MirrorCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage == 0)
    {
        m_mirrorStart = QPointF(x, y);
        m_stage = 1;
    }
    else if (m_stage == 1)
    {
        m_mirrorEnd = QPointF(x, y);

        if (!m_document)
        {
            commit();
            return true;
        }

        const double dx = m_mirrorEnd.x() - m_mirrorStart.x();
        const double dy = m_mirrorEnd.y() - m_mirrorStart.y();
        if (std::fabs(dx) < 1e-8 && std::fabs(dy) < 1e-8)
        {
            commit();
            return true;
        }

        auto* sm = m_document->sceneManager();
        if (!sm)
        {
            commit();
            return true;
        }

        const QStringList selectedIds = m_document->selectedIdsQ();
        for (const QString& idStr : selectedIds)
        {
            bool ok = false;
            const Eg::EntityId id = static_cast<Eg::EntityId>(idStr.toULongLong(&ok));
            if (!ok)
                continue;

            auto* entity = sm->findEntityById(id);
            if (!entity)
                continue;

            m_originalSnapshots.append(takeSnapshot(entity));
            applyMirrorToEntity(entity, m_mirrorStart, m_mirrorEnd);
            m_mirroredSnapshots.append(takeSnapshot(entity));
            m_mirroredEntityIds.append(idStr);
        }
        commit();
    }
    return true;
}

bool MirrorCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_stage == 1)
        m_mirrorEnd = QPointF(x, y);
    return true;
}

ITool* MirrorCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* MirrorCommand::createUndoCommand()
{
    if (m_originalSnapshots.isEmpty())
        return nullptr;

    return new SnapshotUndoCommand(QObject::tr("Mirror"), m_document, m_originalSnapshots);
}

bool MirrorCommand::isComplete() const
{
    return m_state == CommandState::Committed;
}

CommandPreview MirrorCommand::preview() const
{
    return CommandPreview();
}

void MirrorCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}