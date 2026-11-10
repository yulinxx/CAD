/**
 * @file SceneDocument2D.cpp
 * @brief 2D 场景文档实现
 *
 * 管理 2D 文档的创建、加载和保存。
 */
#include "SceneDocument2D.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine/EntityIdUtils.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprArc.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/AprEntity/AprBezier2.h"
#include "Engine2D/AprEntity/AprBezier.h"
#include "Engine2D/AprEntity/AprNurbs.h"
#include "Engine2D/AprEntity/AprSmartLine.h"
#include "Engine2D/AprEntity/AprText.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine/ISceneManager.h"
#include "Ut/Vec.h"
#include "Log/AprLogger.h"

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
    // 文档的「已修改」直接取场景的脏标记：引擎层（SceneManager）是唯一事实源。
    // 文档层以前另存了一份 m_isModified，只在 create* 里置 true，既不被读取、
    // 也不会在保存后清除 —— 是纯粹的死状态。
    return m_scene ? m_scene->isDirty() : false;
}

void SceneDocument2D::setModified(bool modified)
{
    if (!m_scene)
    {
        return;
    }
    if (modified)
    {
        m_scene->markDirty();
    }
    else
    {
        m_scene->clearDirty();
    }
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
    auto line = std::make_unique<Eg::AprLine>(std::vector<Ut::Vec2d>{ toVec2d(start), toVec2d(end) });
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(line), "Create Line");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(line), "Create Entity", CommitMode::Direct);
    }

    if (!added)
    {
        return {};
    }

    return QString::number(added->id);
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
    auto line = std::make_unique<Eg::AprLine>(pts);
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(line), "Create Polyline");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(line), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createCircle(const QPointF& center, double radius)
{
    auto circle = std::make_unique<Eg::AprCircle>();
    circle->basePoint = toVec2d(center);
    circle->dRadius = radius;
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(circle), "Create Circle");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(circle), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createArc(const QPointF& center, double radius, double startDeg, double endDeg)
{
    auto arc = std::make_unique<Eg::AprArc>();
    arc->basePoint = toVec2d(center);
    arc->dRadius = radius;
    arc->dStartAngle = startDeg;
    arc->dEndAngle = endDeg;
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(arc), "Create Arc");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(arc), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createPolygon(const QVector<QPointF>& vertices)
{
    if (vertices.size() < 3)
    {
        return {};
    }
    auto polygon = std::make_unique<Eg::AprPolygon>();
    polygon->nSides = static_cast<int>(vertices.size());
    // 通过 verticesMutable() 可写接口填充多边形顶点
    auto& verts = polygon->verticesMutable();
    verts.reserve(vertices.size());
    for (const auto& v : vertices)
    {
        verts.push_back(toVec2d(v));
    }
    polygon->basePoint = verts.front();
    Eg::AprEntity* added = nullptr;

    // 优先通过编辑服务添加（支持撤销），否则直接添加到场景
    if (m_editService)
    {
        added = m_editService->addEntity(std::move(polygon), "Create Polygon");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(polygon), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createBezier2(const QPointF& start, const QPointF& control, const QPointF& end)
{
    auto bezier = std::make_unique<Eg::AprBezier2>();
    bezier->basePoint = toVec2d(start);
    bezier->ptCtrl = toVec2d(control);
    bezier->ptEnd = toVec2d(end);
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(bezier), "Create Bezier2");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(bezier), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createBezier(
    const QPointF& start, const QPointF& control1, const QPointF& control2, const QPointF& end)
{
    auto bezier = std::make_unique<Eg::AprBezier>();
    bezier->basePoint = toVec2d(start);
    bezier->ptCtrl0 = toVec2d(control1);
    bezier->ptCtrl1 = toVec2d(control2);
    bezier->ptEnd = toVec2d(end);
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(bezier), "Create Bezier");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(bezier), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createNurbs(const QVector<QPointF>& controlPoints)
{
    if (controlPoints.size() < 2)
    {
        return {};
    }
    auto nurbs = std::make_unique<Eg::AprNurbs>();
    nurbs->nDegree = std::min(3, static_cast<int>(controlPoints.size()) - 1);
    nurbs->reserveControlPoints(controlPoints.size());
    for (const auto& p : controlPoints)
    {
        nurbs->addControlPoint(toVec2d(p));
    }
    nurbs->updateKnots();
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(nurbs), "Create NURBS");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(nurbs), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createSmartLine(const QVector<QPointF>& points)
{
    if (points.size() < 2)
    {
        return {};
    }
    auto smartLine = std::make_unique<Eg::AprSmartLine>();
    for (int i = 0; i < static_cast<int>(points.size()) - 1; ++i)
    {
        auto segment =
            std::make_unique<Eg::AprLine>(std::vector<Ut::Vec2d>{ toVec2d(points[i]), toVec2d(points[i + 1]) });
        smartLine->addSegment(segment.release(), true);
    }
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(smartLine), "Create SmartLine");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(smartLine), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createText(const QPointF& position, const QString& text, double height)
{
    if (text.isEmpty() || height <= 0.0)
    {
        return {};
    }
    auto textEntity = std::make_unique<Eg::AprText>();
    const auto strText = text.toStdString();
    textEntity->basePoint = toVec2d(position);
    textEntity->setText(strText.c_str());
    textEntity->dHeight = height;
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(textEntity), "Create Text");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(textEntity), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
    return id;
}

QString SceneDocument2D::createSpline(const QVector<QPointF>& points)
{
    if (points.size() < 2)
    {
        return {};
    }
    auto nurbs = std::make_unique<Eg::AprNurbs>();
    nurbs->nDegree = std::min(3, static_cast<int>(points.size()) - 1);
    nurbs->reserveControlPoints(points.size());
    for (const auto& p : points)
    {
        nurbs->addControlPoint(toVec2d(p));
    }
    nurbs->updateKnots();
    Eg::AprEntity* added = nullptr;

    if (m_editService)
    {
        added = m_editService->addEntity(std::move(nurbs), "Create Spline");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        added = directEdit.addEntity(std::move(nurbs), "Create Entity", CommitMode::Direct);
    }

    QString id = added ? QString::number(added->id) : QString();
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
        APR_WARNF("[SceneDocument2D] remove rejected: invalid entity id '%s'", qPrintable(id));
        return false;
    }

    if (!m_scene->findAprEntityById(eid))
    {
        APR_WARNF("[SceneDocument2D] remove rejected: entity not found '%s'", qPrintable(id));
        return false;
    }

    if (m_editService)
    {
        m_editService->deleteEntities({ eid }, "Delete Entity");
    }
    else
    {
        SceneEditService directEdit(m_scene, nullptr);
        directEdit.deleteEntitiesDirect({ m_scene->findAprEntityById(eid) });
    }

    APR_DEBUGF("[SceneDocument2D] entity removed: id=%s via=%s",
        qPrintable(id),
        m_editService ? "SceneEditService" : "SceneManager");
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
        APR_WARN("[SceneDocument2D] remove rejected: null entity id");
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
    APR_DEBUGF("[SceneDocument2D] document cleared: entities=%lld", static_cast<long long>(count));
}

Eg::ISceneContext* SceneDocument2D::sceneContext() const
{
    if (m_editService && m_editService->sceneManager())
    {
        return m_editService->sceneManager();
    }
    return m_scene;
}