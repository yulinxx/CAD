#include "SceneDocument2D.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Ut/Vec.h"

#include <QUuid>

static Ut::Vec2d toVec2d(const QPointF& p)
{
    return { p.x(), p.y() };
}

SceneDocument2D::SceneDocument2D()
    : m_scene(new Eg::SceneManager())
{
}

SceneDocument2D::~SceneDocument2D()
{
    delete m_scene;
}

QString SceneDocument2D::createLine(const QPointF& start, const QPointF& end)
{
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ toVec2d(start), toVec2d(end) });
    auto id = line->id;
    m_scene->addEntity(line.release());
    return QString::number(id);
}

QString SceneDocument2D::createCircle(const QPointF& center, double radius)
{
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = toVec2d(center);
    circle->dRadius = radius;
    auto id = circle->id;
    m_scene->addEntity(circle.release());
    return QString::number(id);
}

QString SceneDocument2D::createArc(const QPointF& center, double radius,
                                     double startDeg, double endDeg)
{
    auto arc = std::make_unique<Eg::SyArc>();
    arc->basePoint = toVec2d(center);
    arc->dRadius = radius;
    arc->dStartAngle = startDeg;
    arc->dEndAngle = endDeg;
    auto id = arc->id;
    m_scene->addEntity(arc.release());
    return QString::number(id);
}

QString SceneDocument2D::entityIdAt(const QPointF& point, double tolerance) const
{
    auto hits = m_scene->queryByPoint(toVec2d(point), tolerance);
    if (hits.empty())
        return {};
    return QString::number(hits.front()->id);
}

QVector<QString> SceneDocument2D::allEntityIds() const
{
    auto all = m_scene->getAllEntities();
    QVector<QString> ids;
    ids.reserve(static_cast<int>(all.size()));
    for (const auto& e : all)
        ids.push_back(QString::number(e->id));
    return ids;
}

void SceneDocument2D::selectEntity(const QString& id)
{
    bool ok = false;
    Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
    if (!ok) return;
    auto* entity = m_scene->findEntityById(eid);
    if (entity)
        m_scene->selectEntity(entity);
}

void SceneDocument2D::clearSelection()
{
    m_scene->clearSelection();
}

QVector<QString> SceneDocument2D::selectedIds() const
{
    auto selected = m_scene->getSelectedEntities();
    QVector<QString> ids;
    ids.reserve(static_cast<int>(selected.size()));
    for (const auto& e : selected)
        ids.push_back(QString::number(e->id));
    return ids;
}

void SceneDocument2D::removeEntity(const QString& id)
{
    bool ok = false;
    Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
    if (!ok) return;
    auto* entity = m_scene->findEntityById(eid);
    if (entity)
        m_scene->deleteEntity(entity);
}

Eg::SyEntity* SceneDocument2D::entityByStringId(const QString& id) const
{
    bool ok = false;
    Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
    if (!ok) return nullptr;
    return m_scene->findEntityById(eid);
}

void SceneDocument2D::clear()
{
    m_scene->clearScene();
}
