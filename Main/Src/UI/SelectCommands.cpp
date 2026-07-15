#include "SelectCommands.h"

#include <QObject>
#include <QApplication>
#include "Log/SyLogger.h"

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
    m_selectionService = services.selectionService;
    m_state = CommandState::Active;
    m_selectedEntityIds.clear();
    m_oldSelectedIds.clear();
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
    SY_INFOF("[SelectCommands] SelectCommand committed: selected=%d entities", m_selectedEntityIds.size());
    m_state = CommandState::Committed;
}

void SelectCommand::reset()
{
    m_state = CommandState::Idle;
    m_selectedEntityIds.clear();
    m_oldSelectedIds.clear();
    m_boxSelecting = false;
    m_boxSelectStart = QPointF();
    m_boxSelectEnd = QPointF();
}

bool SelectCommand::isShiftPressed() const
{
    return QApplication::keyboardModifiers() & Qt::ShiftModifier;
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

static bool hitTestEntity(SyEntity* entity, const QPointF& clickPt, double tolerance)
{
    if (!entity)
        return false;

    switch (entity->eType)
    {
        case Eg::EType::LINE:
        case Eg::EType::SMARTLINE:
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            for (size_t i = 1; i < line->vPoints.size(); ++i)
            {
                const QPointF p0(line->vPoints[i - 1].x(), line->vPoints[i - 1].y());
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
                    return true;
            }
            break;
        }
        case Eg::EType::CIRCLE:
        {
            auto* circle = static_cast<Eg::SyCircle*>(entity);
            const QPointF center(circle->basePoint.x(), circle->basePoint.y());
            const double dist = QLineF(clickPt, center).length();
            if (std::abs(dist - circle->dRadius) <= tolerance)
                return true;
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
                return true;
            break;
        }
        case Eg::EType::POLYGON:
        {
            auto* polygon = static_cast<Eg::SyPolygon*>(entity);
            const auto& verts = polygon->vertices();
            for (size_t i = 0; i < verts.size(); ++i)
            {
                size_t j = (i + 1) % verts.size();
                const QPointF p0(verts[i].x(), verts[i].y());
                const QPointF p1(verts[j].x(), verts[j].y());
                QLineF lineSeg(p0, p1);
                double dx = p1.x() - p0.x();
                double dy = p1.y() - p0.y();
                double lenSq = dx * dx + dy * dy;
                double t = lenSq > 0 ? ((clickPt.x() - p0.x()) * dx + (clickPt.y() - p0.y()) * dy) / lenSq : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const QPointF closest = lineSeg.pointAt(t);
                const double dist = QLineF(clickPt, closest).length();
                if (dist <= tolerance)
                    return true;
            }
            break;
        }
        case Eg::EType::BEZIER2:
        {
            auto* bezier = static_cast<Eg::SyBezier2*>(entity);
            const QPointF start(bezier->basePoint.x(), bezier->basePoint.y());
            const QPointF ctrl(bezier->ptCtrl.x(), bezier->ptCtrl.y());
            const QPointF end(bezier->ptEnd.x(), bezier->ptEnd.y());

            const int steps = 50;
            for (int i = 0; i < steps; ++i)
            {
                double t1 = static_cast<double>(i) / steps;
                double t2 = static_cast<double>(i + 1) / steps;

                QPointF p1 = QPointF(
                    (1 - t1) * (1 - t1) * start.x() + 2 * (1 - t1) * t1 * ctrl.x() + t1 * t1 * end.x(),
                    (1 - t1) * (1 - t1) * start.y() + 2 * (1 - t1) * t1 * ctrl.y() + t1 * t1 * end.y());
                QPointF p2 = QPointF(
                    (1 - t2) * (1 - t2) * start.x() + 2 * (1 - t2) * t2 * ctrl.x() + t2 * t2 * end.x(),
                    (1 - t2) * (1 - t2) * start.y() + 2 * (1 - t2) * t2 * ctrl.y() + t2 * t2 * end.y());

                QLineF lineSeg(p1, p2);
                double dx = p2.x() - p1.x();
                double dy = p2.y() - p1.y();
                double lenSq = dx * dx + dy * dy;
                double t = lenSq > 0 ? ((clickPt.x() - p1.x()) * dx + (clickPt.y() - p1.y()) * dy) / lenSq : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const QPointF closest = lineSeg.pointAt(t);
                const double dist = QLineF(clickPt, closest).length();
                if (dist <= tolerance)
                    return true;
            }
            break;
        }
        case Eg::EType::BEZIER:
        {
            auto* bezier = static_cast<Eg::SyBezier*>(entity);
            const QPointF start(bezier->basePoint.x(), bezier->basePoint.y());
            const QPointF ctrl0(bezier->ptCtrl0.x(), bezier->ptCtrl0.y());
            const QPointF ctrl1(bezier->ptCtrl1.x(), bezier->ptCtrl1.y());
            const QPointF end(bezier->ptEnd.x(), bezier->ptEnd.y());

            const int steps = 50;
            for (int i = 0; i < steps; ++i)
            {
                double t1 = static_cast<double>(i) / steps;
                double t2 = static_cast<double>(i + 1) / steps;

                QPointF p1 = QPointF(
                    std::pow(1 - t1, 3) * start.x() + 3 * t1 * std::pow(1 - t1, 2) * ctrl0.x() + 3 * t1 * t1 * (1 - t1) * ctrl1.x() + std::pow(t1, 3) * end.x(),
                    std::pow(1 - t1, 3) * start.y() + 3 * t1 * std::pow(1 - t1, 2) * ctrl0.y() + 3 * t1 * t1 * (1 - t1) * ctrl1.y() + std::pow(t1, 3) * end.y());
                QPointF p2 = QPointF(
                    std::pow(1 - t2, 3) * start.x() + 3 * t2 * std::pow(1 - t2, 2) * ctrl0.x() + 3 * t2 * t2 * (1 - t2) * ctrl1.x() + std::pow(t2, 3) * end.x(),
                    std::pow(1 - t2, 3) * start.y() + 3 * t2 * std::pow(1 - t2, 2) * ctrl0.y() + 3 * t2 * t2 * (1 - t2) * ctrl1.y() + std::pow(t2, 3) * end.y());

                QLineF lineSeg(p1, p2);
                double dx = p2.x() - p1.x();
                double dy = p2.y() - p1.y();
                double lenSq = dx * dx + dy * dy;
                double t = lenSq > 0 ? ((clickPt.x() - p1.x()) * dx + (clickPt.y() - p1.y()) * dy) / lenSq : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const QPointF closest = lineSeg.pointAt(t);
                const double dist = QLineF(clickPt, closest).length();
                if (dist <= tolerance)
                    return true;
            }
            break;
        }
        case Eg::EType::SPLINE:
        {
            auto* nurbs = static_cast<Eg::SyNurbs*>(entity);
            const int steps = 50;
            for (int i = 0; i < steps; ++i)
            {
                double t1 = static_cast<double>(i) / steps;
                double t2 = static_cast<double>(i + 1) / steps;

                Ut::Vec2d p1 = nurbs->value(t1);
                Ut::Vec2d p2 = nurbs->value(t2);

                QPointF qp1(p1.x(), p1.y());
                QPointF qp2(p2.x(), p2.y());

                QLineF lineSeg(qp1, qp2);
                double dx = qp2.x() - qp1.x();
                double dy = qp2.y() - qp1.y();
                double lenSq = dx * dx + dy * dy;
                double t = lenSq > 0 ? ((clickPt.x() - qp1.x()) * dx + (clickPt.y() - qp1.y()) * dy) / lenSq : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const QPointF closest = lineSeg.pointAt(t);
                const double dist = QLineF(clickPt, closest).length();
                if (dist <= tolerance)
                    return true;
            }
            break;
        }
        default:
            break;
    }

    return false;
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
        // 记录当前选中列表，用于 Shift+点击 的累加选择
        const auto currentIds = m_selectionService->selectedIds();
        for (const std::string& id : currentIds)
            m_oldSelectedIds.append(QString::fromStdString(id));

        auto* sm = m_document->sceneManager();
        if (sm)
        {
            QString hitId;
            const QPointF clickPt(x, y);
            const double tolerance = 5.0;

            const auto& entities = sm->getAllEntities();
            for (auto it = entities.rbegin(); it != entities.rend(); ++it)
            {
                if (hitTestEntity(*it, clickPt, tolerance))
                {
                    hitId = QString::number((*it)->id);
                    break;
                }
            }

            if (isShiftPressed())
            {
                m_selectedEntityIds = m_oldSelectedIds;
                std::vector<std::string> ids;
                ids.reserve(static_cast<size_t>(m_selectedEntityIds.size()));
                for (const QString& sid : m_selectedEntityIds)
                    ids.push_back(sid.toStdString());
                m_selectionService->selectMultiple(ids);
            }
            else
            {
                if (hitId.isEmpty())
                {
                    m_selectedEntityIds.clear();
                    m_selectionService->clear();
                }
                else
                {
                    m_selectedEntityIds.clear();
                    m_selectedEntityIds.append(hitId);
                    m_selectionService->selectMultiple({ hitId.toStdString() });
                }
            }
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
    QVector<QString> selectedIds;

    const auto& entities = sm->getAllEntities();
    for (SyEntity* entity : entities)
    {
        if (!entity)
            continue;

        bool inside = false;

        switch (entity->eType)
        {
            case Eg::EType::LINE:
            case Eg::EType::SMARTLINE:
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
                for (const auto& v : polygon->vertices())
                {
                    if (rect.contains(QPointF(v.x(), v.y())))
                    {
                        inside = true;
                        break;
                    }
                }
                break;
            }
            case Eg::EType::BEZIER2:
            case Eg::EType::BEZIER:
                inside = rect.contains(QPointF(entity->basePoint.x(), entity->basePoint.y()));
                break;
            case Eg::EType::SPLINE:
            {
                auto* nurbs = static_cast<Eg::SyNurbs*>(entity);
                for (const auto& pt : nurbs->vControlPoints)
                {
                    if (rect.contains(QPointF(pt.x(), pt.y())))
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

    if (isShiftPressed())
    {
        const auto currentIds = m_selectionService->selectedIds();
        for (const std::string& id : currentIds)
            m_selectedEntityIds.append(QString::fromStdString(id));
        for (const QString& id : selectedIds)
        {
            if (!m_selectedEntityIds.contains(id))
                m_selectedEntityIds.append(id);
        }
        std::vector<std::string> ids;
        ids.reserve(static_cast<size_t>(m_selectedEntityIds.size()));
        for (const QString& sid : m_selectedEntityIds)
            ids.push_back(sid.toStdString());
        m_selectionService->selectMultiple(ids);
    }
    else
    {
        m_selectedEntityIds = selectedIds;
        if (selectedIds.isEmpty())
            m_selectionService->clear();
        else
        {
            std::vector<std::string> ids;
            ids.reserve(static_cast<size_t>(selectedIds.size()));
            for (const QString& id : selectedIds)
                ids.push_back(id.toStdString());
            m_selectionService->selectMultiple(ids);
        }
    }
}

ITool* SelectCommand::activeTool() const
{
    return nullptr;
}

UndoCommand* SelectCommand::createUndoCommand()
{
    return nullptr;
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
    m_selectedEntityIds.clear();
    m_selectedEntityIds.append(entityId);
}

SelectUndoCommand::SelectUndoCommand(ISelectionService* selService, const QString& oldId, const QString& newId)
    : UndoCommand(QObject::tr("Select"))
    , m_selectionService(selService)
    , m_oldId(oldId)
    , m_newId(newId)
{
}

void SelectUndoCommand::undo()
{
    if (!m_selectionService)
        return;

    // 恢复到撤销前的选中实体
    if (m_oldId.isEmpty())
        m_selectionService->clear();
    else
    {
        m_selectionService->clear();
        m_selectionService->select(m_oldId.toStdString());
    }
}

void SelectUndoCommand::redo()
{
    if (!m_selectionService)
        return;

    // 重做到撤销后的选中实体
    if (m_newId.isEmpty())
        m_selectionService->clear();
    else
    {
        m_selectionService->clear();
        m_selectionService->select(m_newId.toStdString());
    }
}