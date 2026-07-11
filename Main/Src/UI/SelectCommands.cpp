#include "SelectCommands.h"

#include <QObject>
#include "Log/SyLogger.h"

#include "SceneDocument2D.h"
#include "UiServices.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"

using Eg::SyEntity;

SelectCommand::SelectCommand()
{
}

QString SelectCommand::commandId() const
{
    return QStringLiteral("2d.select");
}

QString SelectCommand::displayName() const
{
    return QObject::tr("Select");
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
    SY_INFO("[SelectCommands] SelectCommand activated");
    m_services = &services;
    m_document = services.document2D;
    m_state = CommandState::Active;
    m_selectedEntityId.clear();
    m_oldSelectedId.clear();
    m_boxSelecting = false;
    m_boxSelectStart = QPointF();
    m_boxSelectEnd = QPointF();
    return true;
}

void SelectCommand::cancel()
{
    SY_INFO("[SelectCommands] SelectCommand cancelled");
    m_state = CommandState::Cancelled;
}

void SelectCommand::commit()
{
    SY_INFOF("[SelectCommands] SelectCommand committed: selected=%s", m_selectedEntityId.toUtf8().constData());
    m_state = CommandState::Committed;
}

void SelectCommand::reset()
{
    m_state = CommandState::Idle;
    m_selectedEntityId.clear();
    m_oldSelectedId.clear();
    m_boxSelecting = false;
    m_boxSelectStart = QPointF();
    m_boxSelectEnd = QPointF();
}

bool SelectCommand::onMouseDown(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (!m_document)
        return false;

    m_boxSelecting = true;
    m_boxSelectStart = QPointF(x, y);
    m_boxSelectEnd = QPointF(x, y);

    return true;
}

bool SelectCommand::onMouseMove(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    if (m_boxSelecting)
        m_boxSelectEnd = QPointF(x, y);
    return true;
}

bool SelectCommand::onMouseUp(int x, int y)
{
    if (m_state != CommandState::Active)
        return false;

    m_boxSelectEnd = QPointF(x, y);

    if (!m_document)
        return false;

    const QRectF rect(m_boxSelectStart, m_boxSelectEnd);
    const bool isClick = rect.width() < 5 && rect.height() < 5;

    if (isClick)
    {
        m_oldSelectedId = m_document->selectedIdsQ().isEmpty() ? QString() : m_document->selectedIdsQ().first();

        auto* sm = m_document->sceneManager();
        if (sm)
        {
            QString hitId;
            const QPointF clickPt(x, y);
            const double tolerance = 5.0;

            const auto& entities = sm->getAllEntities();
            for (auto it = entities.rbegin(); it != entities.rend(); ++it)
            {
                SyEntity* entity = *it;
                if (!entity)
                    continue;

                bool hit = false;

                switch (entity->eType)
                {
                case Eg::EType::LINE:
                {
                    auto* line = static_cast<Eg::SyLine*>(entity);
                    for (size_t i = 1; i < line->vPoints.size(); ++i)
                    {
                        const QPointF p0(line->vPoints[i-1].x(), line->vPoints[i-1].y());
                        const QPointF p1(line->vPoints[i].x(), line->vPoints[i].y());
                        QLineF lineSeg(p0, p1);
                        double dx = p1.x() - p0.x();
                        double dy = p1.y() - p0.y();
                        double lenSq = dx * dx + dy * dy;
                        double t = lenSq > 0 ? ((clickPt.x() - p0.x()) * dx + (clickPt.y() - p0.y()) * dy) / lenSq : 0.0;
                        t = std::max(0.0, std::min(1.0, t));
                        const QPointF closest = lineSeg.pointAt(t);
                        const double dist = QLineF(clickPt, closest).length();
                        if (dist <= tolerance)
                        {
                            hit = true;
                            break;
                        }
                    }
                    break;
                }
                case Eg::EType::CIRCLE:
                {
                    auto* circle = static_cast<Eg::SyCircle*>(entity);
                    const QPointF center(circle->basePoint.x(), circle->basePoint.y());
                    const double dist = QLineF(clickPt, center).length();
                    if (std::abs(dist - circle->dRadius) <= tolerance)
                        hit = true;
                    break;
                }
                case Eg::EType::ARC:
                {
                    auto* arc = static_cast<Eg::SyArc*>(entity);
                    const QPointF center(arc->basePoint.x(), arc->basePoint.y());
                    const double dist = QLineF(clickPt, center).length();
                    const double angle = std::atan2(clickPt.y() - center.y(), clickPt.x() - center.x());

                    double start = arc->dStartAngle;
                    double end = arc->dEndAngle;
                    if (end < start)
                        end += 2.0 * M_PI;

                    double checkAngle = angle;
                    while (checkAngle < start)
                        checkAngle += 2.0 * M_PI;
                    while (checkAngle > end)
                        checkAngle -= 2.0 * M_PI;

                    if (std::abs(dist - arc->dRadius) <= tolerance && checkAngle >= start && checkAngle <= end)
                        hit = true;
                    break;
                }
                case Eg::EType::POLYGON:
                {
                    auto* polygon = static_cast<Eg::SyPolygon*>(entity);
                    for (size_t i = 0; i < polygon->vVertices.size(); ++i)
                    {
                        size_t j = (i + 1) % polygon->vVertices.size();
                        const QPointF p0(polygon->vVertices[i].x(), polygon->vVertices[i].y());
                        const QPointF p1(polygon->vVertices[j].x(), polygon->vVertices[j].y());
                        QLineF lineSeg(p0, p1);
                        double dx = p1.x() - p0.x();
                        double dy = p1.y() - p0.y();
                        double lenSq = dx * dx + dy * dy;
                        double t = lenSq > 0 ? ((clickPt.x() - p0.x()) * dx + (clickPt.y() - p0.y()) * dy) / lenSq : 0.0;
                        t = std::max(0.0, std::min(1.0, t));
                        const QPointF closest = lineSeg.pointAt(t);
                        const double dist = QLineF(clickPt, closest).length();
                        if (dist <= tolerance)
                        {
                            hit = true;
                            break;
                        }
                    }
                    break;
                }
                default:
                    break;
                }

                if (hit)
                {
                    hitId = QString::number(entity->id);
                    break;
                }
            }

            m_selectedEntityId = hitId;
            if (hitId.isEmpty())
                m_document->clearSelection();
            else
                m_document->setSelectedEntityId(hitId);
        }
    }
    else
    {
        performBoxSelect();
    }

    m_boxSelecting = false;
    commit();
    return true;
}

void SelectCommand::performBoxSelect()
{
    if (!m_document)
        return;

    auto* sm = m_document->sceneManager();
    if (!sm)
        return;

    const QRectF rect(m_boxSelectStart, m_boxSelectEnd);
    QStringList selectedIds;

    const auto& entities = sm->getAllEntities();
    for (SyEntity* entity : entities)
    {
        if (!entity)
            continue;

        bool inside = false;

        switch (entity->eType)
        {
        case Eg::EType::LINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
            {
                if (rect.contains(QPointF(pt.x(), pt.y())))
                {
                    inside = true;
                    break;
                }
            }
            break;
        }
        case Eg::EType::CIRCLE:
        case Eg::EType::ARC:
            inside = rect.contains(QPointF(entity->basePoint.x(), entity->basePoint.y()));
            break;
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            for (const auto& v : polygon->vVertices)
            {
                if (rect.contains(QPointF(v.x(), v.y())))
                {
                    inside = true;
                    break;
                }
            }
            break;
        }
        default:
            break;
        }

        if (inside)
            selectedIds.append(QString::number(entity->id));
    }

    if (selectedIds.isEmpty())
        m_document->clearSelection();
    else
        m_document->setSelectedEntityIds(selectedIds);
}

ITool* SelectCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* SelectCommand::createUndoCommand()
{
    if (m_selectedEntityId == m_oldSelectedId)
        return nullptr;

    return new SelectUndoCommand(m_document, m_oldSelectedId, m_selectedEntityId);
}

bool SelectCommand::isComplete() const
{
    return m_state == CommandState::Committed;
}

CommandPreview SelectCommand::preview() const
{
    return CommandPreview();
}

void SelectCommand::setDocument(SceneDocument2D* document)
{
    m_document = document;
}

void SelectCommand::setSelectedEntityId(const QString& entityId)
{
    m_selectedEntityId = entityId;
}

SelectUndoCommand::SelectUndoCommand(SceneDocument2D* document, const QString& oldId, const QString& newId)
    : UndoCommand(QObject::tr("Select"))
    , m_document(document)
    , m_oldId(oldId)
    , m_newId(newId)
{
}

void SelectUndoCommand::undo()
{
    if (!m_document)
        return;

    if (m_oldId.isEmpty())
        m_document->clearSelection();
    else
        m_document->setSelectedEntityId(m_oldId);
}

void SelectUndoCommand::redo()
{
    if (!m_document)
        return;

    if (m_newId.isEmpty())
        m_document->clearSelection();
    else
        m_document->setSelectedEntityId(m_newId);
}