#include "SceneDocument2D.h"

#include "Log/SyLogger.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"
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
    m_scene->addEntity(line.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createLine: id=%s, start=(%f,%f), end=(%f,%f)",
        id.toUtf8().constData(), start.x(), start.y(), end.x(), end.y());
    return id;
}

// 创建多段折线（SyLine 支持 vPoints 存储多个顶点）
QString SceneDocument2D::createPolyline(const QVector<QPointF>& points)
{
    if (points.size() < 2)
        return {};
    std::vector<Ut::Vec2d> pts;
    pts.reserve(points.size());
    for (const auto& p : points)
        pts.push_back(toVec2d(p));
    auto line = std::make_unique<Eg::SyLine>(pts);
    m_scene->addEntity(line.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createPolyline: id=%s, points=%d",
        id.toUtf8().constData(), points.size());
    return id;
}

QString SceneDocument2D::createCircle(const QPointF& center, double radius)
{
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = toVec2d(center);
    circle->dRadius = radius;
    m_scene->addEntity(circle.release());
    auto* added = m_scene->getAllEntities().back();
    return QString::number(added->id);
}

QString SceneDocument2D::createArc(const QPointF& center, double radius,
    double startDeg, double endDeg)
{
    auto arc = std::make_unique<Eg::SyArc>();
    arc->basePoint = toVec2d(center);
    arc->dRadius = radius;
    arc->dStartAngle = startDeg;
    arc->dEndAngle = endDeg;
    m_scene->addEntity(arc.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createArc: id=%s, center=(%f,%f), radius=%f, start=%f, end=%f",
        id.toUtf8().constData(), center.x(), center.y(), radius, startDeg, endDeg);
    return id;
}

QString SceneDocument2D::createPolygon(const QVector<QPointF>& vertices)
{
    if (vertices.size() < 3)
        return {};
    auto polygon = std::make_unique<Eg::SyPolygon>();
    polygon->nSides = static_cast<int>(vertices.size());
    polygon->vVertices.reserve(vertices.size());
    for (const auto& v : vertices)
        polygon->vVertices.push_back(toVec2d(v));
    polygon->basePoint = polygon->vVertices.front();
    m_scene->addEntity(polygon.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createPolygon: id=%s, sides=%d",
        id.toUtf8().constData(), vertices.size());
    return id;
}

QString SceneDocument2D::createBezier2(const QPointF& start, const QPointF& control, const QPointF& end)
{
    auto bezier = std::make_unique<Eg::SyBezier2>();
    bezier->basePoint = toVec2d(start);
    bezier->ptCtrl = toVec2d(control);
    bezier->ptEnd = toVec2d(end);
    m_scene->addEntity(bezier.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createBezier2: id=%s", id.toUtf8().constData());
    return id;
}

QString SceneDocument2D::createBezier(const QPointF& start, const QPointF& control1,
    const QPointF& control2, const QPointF& end)
{
    auto bezier = std::make_unique<Eg::SyBezier>();
    bezier->basePoint = toVec2d(start);
    bezier->ptCtrl0 = toVec2d(control1);
    bezier->ptCtrl1 = toVec2d(control2);
    bezier->ptEnd = toVec2d(end);
    m_scene->addEntity(bezier.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createBezier: id=%s", id.toUtf8().constData());
    return id;
}

QString SceneDocument2D::createNurbs(const QVector<QPointF>& controlPoints)
{
    if (controlPoints.size() < 2)
        return {};
    auto nurbs = std::make_unique<Eg::SyNurbs>();
    nurbs->nDegree = std::min(3, static_cast<int>(controlPoints.size()) - 1);
    nurbs->vControlPoints.reserve(controlPoints.size());
    for (const auto& p : controlPoints)
        nurbs->vControlPoints.push_back(toVec2d(p));
    nurbs->updateKnots();
    m_scene->addEntity(nurbs.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createNurbs: id=%s, controlPoints=%d",
        id.toUtf8().constData(), controlPoints.size());
    return id;
}

QString SceneDocument2D::createSmartLine(const QVector<QPointF>& points)
{
    if (points.size() < 2)
        return {};
    auto smartLine = std::make_unique<Eg::SySmartLine>();
    for (int i = 0; i < points.size() - 1; ++i)
    {
        auto segment = std::make_unique<Eg::SyLine>(
            std::vector<Ut::Vec2d>{ toVec2d(points[i]), toVec2d(points[i + 1]) });
        smartLine->addSegment(std::move(segment), true);
    }
    m_scene->addEntity(smartLine.release());
    auto* added = m_scene->getAllEntities().back();
    QString id = QString::number(added->id);
    SY_INFOF("[SceneDocument2D] createSmartLine: id=%s, points=%d",
        id.toUtf8().constData(), points.size());
    return id;
}

QString SceneDocument2D::entityIdAt(const QPointF& point, double tolerance) const
{
    auto hits = m_scene->queryByPoint(toVec2d(point), tolerance);
    if (hits.empty())
        return {};
    return QString::number(hits.front()->id);
}

QVector<QString> SceneDocument2D::allEntityIdsQ() const
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

void SceneDocument2D::setSelectedEntityId(const QString& id)
{
    bool ok = false;
    Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
    if (!ok) return;
    auto* entity = m_scene->findEntityById(eid);
    if (entity)
        m_scene->selectEntity(entity);
}

void SceneDocument2D::setSelectedEntityIds(const QVector<QString>& ids)
{
    m_scene->clearSelection();
    for (const QString& id : ids)
    {
        bool ok = false;
        Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
        if (!ok) continue;
        auto* entity = m_scene->findEntityById(eid);
        if (entity)
            m_scene->selectEntity(entity);
    }
}

QVector<QString> SceneDocument2D::selectedIdsQ() const
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
    SY_INFOF("[SceneDocument2D] removeEntity: id=%s", id.toUtf8().constData());
}

Eg::SyEntity* SceneDocument2D::entityByStringId(const QString& id) const
{
    bool ok = false;
    Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
    if (!ok) return nullptr;
    return m_scene->findEntityById(eid);
}

std::vector<std::string> SceneDocument2D::allEntityIds() const
{
    auto all = m_scene->getAllEntities();
    std::vector<std::string> ids;
    ids.reserve(all.size());
    for (const auto& e : all)
        ids.push_back(std::to_string(e->id));
    return ids;
}

void SceneDocument2D::selectEntity(const std::string& id)
{
    try
    {
        Eg::EntityId eid = static_cast<Eg::EntityId>(std::stoull(id));
        auto* entity = m_scene->findEntityById(eid);
        if (entity)
            m_scene->selectEntity(entity);
    }
    catch (...)
    {
    }
}

void SceneDocument2D::clearSelection()
{
    m_scene->clearSelection();
}

std::vector<std::string> SceneDocument2D::selectedIds() const
{
    auto selected = m_scene->getSelectedEntities();
    std::vector<std::string> ids;
    ids.reserve(selected.size());
    for (const auto& e : selected)
        ids.push_back(std::to_string(e->id));
    return ids;
}

void SceneDocument2D::removeEntity(const std::string& id)
{
    try
    {
        Eg::EntityId eid = static_cast<Eg::EntityId>(std::stoull(id));
        auto* entity = m_scene->findEntityById(eid);
        if (entity)
            m_scene->deleteEntity(entity);
    }
    catch (...)
    {
    }
}

void SceneDocument2D::clear()
{
    SY_INFO("[SceneDocument2D] clear: clearing all entities");
    m_scene->clearScene();
}