#include "SceneDocument2D.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine/EntityIdUtils.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"
#include "Engine2D/SyEntity/SyText.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Ut/Vec.h"
#include "Log/SyLogger.h"

#include <QUuid>
#include <typeinfo>

// ========== SceneDocumentBase 元数据覆盖 ==========

const char* SceneDocument2D::documentName() const
{
    return m_documentName.c_str();
}

void SceneDocument2D::setDocumentName(const char* name)
{
    m_documentName = name ? name : "";
}

const char* SceneDocument2D::filePath() const
{
    return m_filePath.c_str();
}

void SceneDocument2D::setFilePath(const char* path)
{
    m_filePath = path ? path : "";
}

bool SceneDocument2D::isModified() const
{
    return m_isModified;
}

void SceneDocument2D::setModified(bool modified)
{
    m_isModified = modified;
}

static Ut::Vec2d toVec2d(const QPointF& p)
{
    return { p.x(), p.y() };
}

SceneDocument2D::SceneDocument2D()
    : m_scene(new Eg::SceneManager())
    , m_editService(nullptr)
{
}

SceneDocument2D::SceneDocument2D(SceneEditService* editService)
    : m_scene(editService ? editService->sceneManager() : new Eg::SceneManager())
    , m_editService(editService)
{
}

SceneDocument2D::~SceneDocument2D()
{
    if (!m_editService)
    {
        delete m_scene;
    }
}

void SceneDocument2D::setEditService(SceneEditService* editService)
{
    m_editService = editService;
    if (editService && !m_scene)
    {
        m_scene = editService->sceneManager();
    }
}

QString SceneDocument2D::createLine(const QPointF& start, const QPointF& end)
{
    auto line = std::make_unique<Eg::SyLine>(std::vector<Ut::Vec2d>{ toVec2d(start), toVec2d(end) });
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(line), "Create Line");
    }
    else
    {
        m_scene->addEntity(line.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createPolyline(const QVector<QPointF>& points)
{
    if (points.size() < 2)
    {
        return {};
    }
    std::vector<Ut::Vec2d> pts;
    pts.reserve(points.size());
    for (const auto& p : points)
    {
        pts.push_back(toVec2d(p));
    }
    auto line = std::make_unique<Eg::SyLine>(pts);
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(line), "Create Polyline");
    }
    else
    {
        m_scene->addEntity(line.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createCircle(const QPointF& center, double radius)
{
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = toVec2d(center);
    circle->dRadius = radius;
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(circle), "Create Circle");
    }
    else
    {
        m_scene->addEntity(circle.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createArc(const QPointF& center, double radius, double startDeg, double endDeg)
{
    auto arc = std::make_unique<Eg::SyArc>();
    arc->basePoint = toVec2d(center);
    arc->dRadius = radius;
    arc->dStartAngle = startDeg;
    arc->dEndAngle = endDeg;
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(arc), "Create Arc");
    }
    else
    {
        m_scene->addEntity(arc.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createPolygon(const QVector<QPointF>& vertices)
{
    if (vertices.size() < 3)
    {
        return {};
    }
    auto polygon = std::make_unique<Eg::SyPolygon>();
    polygon->nSides = static_cast<int>(vertices.size());
    // 通过 verticesMutable() 可写接口填充多边形顶点
    auto& verts = polygon->verticesMutable();
    verts.reserve(vertices.size());
    for (const auto& v : vertices)
    {
        verts.push_back(toVec2d(v));
    }
    polygon->basePoint = verts.front();
    Eg::SyEntity* added = nullptr;

    // 优先通过编辑服务添加（支持撤销），否则直接添加到场景
    if (m_editService)
    {
        added = m_editService->addEntity(std::move(polygon), "Create Polygon");
    }
    else
    {
        m_scene->addEntity(polygon.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createBezier2(const QPointF& start, const QPointF& control, const QPointF& end)
{
    auto bezier = std::make_unique<Eg::SyBezier2>();
    bezier->basePoint = toVec2d(start);
    bezier->ptCtrl = toVec2d(control);
    bezier->ptEnd = toVec2d(end);
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(bezier), "Create Bezier2");
    }
    else
    {
        m_scene->addEntity(bezier.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createBezier(
    const QPointF& start, const QPointF& control1, const QPointF& control2, const QPointF& end)
{
    auto bezier = std::make_unique<Eg::SyBezier>();
    bezier->basePoint = toVec2d(start);
    bezier->ptCtrl0 = toVec2d(control1);
    bezier->ptCtrl1 = toVec2d(control2);
    bezier->ptEnd = toVec2d(end);
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(bezier), "Create Bezier");
    }
    else
    {
        m_scene->addEntity(bezier.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createNurbs(const QVector<QPointF>& controlPoints)
{
    if (controlPoints.size() < 2)
    {
        return {};
    }
    auto nurbs = std::make_unique<Eg::SyNurbs>();
    nurbs->nDegree = std::min(3, static_cast<int>(controlPoints.size()) - 1);
    nurbs->reserveControlPoints(controlPoints.size());
    for (const auto& p : controlPoints)
    {
        nurbs->addControlPoint(toVec2d(p));
    }
    nurbs->updateKnots();
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(nurbs), "Create NURBS");
    }
    else
    {
        m_scene->addEntity(nurbs.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createSmartLine(const QVector<QPointF>& points)
{
    if (points.size() < 2)
    {
        return {};
    }
    auto smartLine = std::make_unique<Eg::SySmartLine>();
    for (int i = 0; i < points.size() - 1; ++i)
    {
        auto segment =
            std::make_unique<Eg::SyLine>(std::vector<Ut::Vec2d>{ toVec2d(points[i]), toVec2d(points[i + 1]) });
        smartLine->addSegment(segment.release(), true);
    }
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(smartLine), "Create SmartLine");
    }
    else
    {
        m_scene->addEntity(smartLine.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createText(const QPointF& position, const QString& text, double height)
{
    if (text.isEmpty() || height <= 0.0)
    {
        return {};
    }
    auto textEntity = std::make_unique<Eg::SyText>();
    const auto strText = text.toStdString();
    textEntity->basePoint = toVec2d(position);
    textEntity->setText(strText.c_str());
    textEntity->dHeight = height;
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(textEntity), "Create Text");
    }
    else
    {
        m_scene->addEntity(textEntity.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::createSpline(const QVector<QPointF>& points)
{
    if (points.size() < 2)
    {
        return {};
    }
    auto nurbs = std::make_unique<Eg::SyNurbs>();
    nurbs->nDegree = std::min(3, static_cast<int>(points.size()) - 1);
    nurbs->reserveControlPoints(points.size());
    for (const auto& p : points)
    {
        nurbs->addControlPoint(toVec2d(p));
    }
    nurbs->updateKnots();
    Eg::SyEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(nurbs), "Create Spline");
    }
    else
    {
        m_scene->addEntity(nurbs.release());
        added = m_scene->getAllEntities().back();
    }

    QString id = added ? QString::number(added->id) : QString();
    if (added)
    {
        m_isModified = true;
    }
    return id;
}

QString SceneDocument2D::entityIdAt(const QPointF& point, double tolerance) const
{
    auto hits = m_scene->queryByPoint(toVec2d(point), tolerance);
    if (hits.empty())
    {
        return {};
    }
    return QString::number(hits.front()->id);
}

QVector<QString> SceneDocument2D::allEntityIdsQ() const
{
    auto all = m_scene->getAllEntities();
    QVector<QString> ids;
    ids.reserve(static_cast<int>(all.size()));
    for (const auto& e : all)
    {
        ids.push_back(QString::number(e->id));
    }
    return ids;
}

QVector<SceneEntityInfo2D> SceneDocument2D::entityInfos() const
{
    auto all = m_scene->getAllEntities();
    QVector<SceneEntityInfo2D> infos;
    infos.reserve(static_cast<int>(all.size()));
    for (const auto& e : all)
    {
        SceneEntityInfo2D info;
        info.id = QString::number(e->id);
        info.type = QString::fromLatin1(typeid(*e).name());
        infos.push_back(info);
    }
    return infos;
}

bool SceneDocument2D::tryRemoveEntity(const QString& id)
{
    bool ok = false;
    const Eg::EntityId eid = static_cast<Eg::EntityId>(id.toULongLong(&ok));
    if (!ok || !m_scene)
    {
        SY_WARNF("[SceneDocument2D] remove rejected: invalid entity id '%s'", qPrintable(id));
        return false;
    }

    if (!m_scene->findSyEntityById(eid))
    {
        SY_WARNF("[SceneDocument2D] remove rejected: entity not found '%s'", qPrintable(id));
        return false;
    }

    if (m_editService)
    {
        m_editService->deleteEntities({ eid }, "Delete Entity");
    }
    else
    {
        m_scene->deleteEntity(m_scene->findSyEntityById(eid));
    }

    SY_INFOF("[SceneDocument2D] entity removed: id=%s via=%s",
        qPrintable(id),
        m_editService ? "SceneEditService" : "SceneManager");
    m_isModified = true;
    return true;
}

void SceneDocument2D::removeEntity(const QString& id)
{
    (void)tryRemoveEntity(id);
}

void SceneDocument2D::forEachEntityId(void (*visitor)(const char*, void*), void* ctx) const
{
    auto all = m_scene->getAllEntities();
    for (const auto& e : all)
    {
        std::string idStr = std::to_string(e->id);
        visitor(idStr.c_str(), ctx);
    }
}

void SceneDocument2D::removeEntity(const char* id)
{
    if (!id)
    {
        SY_WARN("[SceneDocument2D] remove rejected: null entity id");
        return;
    }

    (void)tryRemoveEntity(QString::fromUtf8(id));
}

void SceneDocument2D::clear()
{
    if (!m_scene)
    {
        return;
    }

    const auto count = m_scene->getAllEntities().size();
    m_scene->clearScene();
    m_isModified = true;
    SY_INFOF("[SceneDocument2D] document cleared: entities=%zu", count);
}