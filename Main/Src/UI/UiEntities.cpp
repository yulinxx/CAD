/**
 * @file UiEntities.cpp
 * @brief UI 层实体实现 — 2D/3D 图元、场景节点、选择集与文档模型
 */

#include "UiEntities.h"

#include <algorithm>
#include <cmath>
#include <QLineF>
#include <QUuid>

namespace
{
    /// 计算点到线段的最短距离，用于命中测试
    double pointLineDistance(const QPointF& p, const QPointF& a, const QPointF& b)
    {
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double len2 = dx * dx + dy * dy;

        if (len2 <= 0.0)
            return std::hypot(p.x() - a.x(), p.y() - a.y());

        const double t = std::clamp(((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2, 0.0, 1.0);
        const QPointF proj(a.x() + t * dx, a.y() + t * dy);
        return std::hypot(p.x() - proj.x(), p.y() - proj.y());
    }
}

// ============================================================
//  LineEntity2D
// ============================================================

LineEntity2D::LineEntity2D(QString id, QPointF start, QPointF end)
    : m_id(std::move(id))
    , m_start(start)
    , m_end(end)
{
}

QString LineEntity2D::id() const
{
    return m_id;
}

QString LineEntity2D::name() const
{
    return QStringLiteral("Line");
}

QString LineEntity2D::typeName() const
{
    return QStringLiteral("LineEntity2D");
}

bool LineEntity2D::selected() const
{
    return m_selected;
}

void LineEntity2D::setSelected(bool selected)
{
    m_selected = selected;
}

bool LineEntity2D::highlighted() const
{
    return m_highlighted;
}

void LineEntity2D::setHighlighted(bool highlighted)
{
    m_highlighted = highlighted;
}

QPointF LineEntity2D::start() const
{
    return m_start;
}

QPointF LineEntity2D::end() const
{
    return m_end;
}

void LineEntity2D::setStart(const QPointF& start)
{
    m_start = start;
}

void LineEntity2D::setEnd(const QPointF& end)
{
    m_end = end;
}

QRectF LineEntity2D::bounds() const
{
    return QRectF(m_start, m_end).normalized();
}

double LineEntity2D::length() const
{
    return QLineF(m_start, m_end).length();
}

QPointF LineEntity2D::direction() const
{
    const QLineF line(m_start, m_end);
    if (line.length() <= 0.0)
        return {};
    return QPointF((m_end.x() - m_start.x()) / line.length(), (m_end.y() - m_start.y()) / line.length());
}

QPointF LineEntity2D::pointAt(double t) const
{
    return QPointF(m_start.x() + (m_end.x() - m_start.x()) * t, m_start.y() + (m_end.y() - m_start.y()) * t);
}

double LineEntity2D::distanceToPoint(const QPointF& point) const
{
    return pointLineDistance(point, m_start, m_end);
}

double LineEntity2D::distanceToStart(const QPointF& point) const
{
    return std::hypot(point.x() - m_start.x(), point.y() - m_start.y());
}

double LineEntity2D::distanceToEnd(const QPointF& point) const
{
    return std::hypot(point.x() - m_end.x(), point.y() - m_end.y());
}

// ============================================================
//  PolylineEntity2D
// ============================================================

PolylineEntity2D::PolylineEntity2D(QString id, QVector<QPointF> points)
    : m_id(std::move(id))
    , m_points(std::move(points))
{
}

QString PolylineEntity2D::id() const
{
    return m_id;
}

QString PolylineEntity2D::name() const
{
    return QStringLiteral("Polyline");
}

QString PolylineEntity2D::typeName() const
{
    return QStringLiteral("PolylineEntity2D");
}

bool PolylineEntity2D::selected() const
{
    return m_selected;
}

void PolylineEntity2D::setSelected(bool selected)
{
    m_selected = selected;
}

bool PolylineEntity2D::highlighted() const
{
    return m_highlighted;
}

void PolylineEntity2D::setHighlighted(bool highlighted)
{
    m_highlighted = highlighted;
}

QVector<QPointF> PolylineEntity2D::points() const
{
    return m_points;
}

void PolylineEntity2D::setPoints(const QVector<QPointF>& points)
{
    m_points = points;
}

QRectF PolylineEntity2D::bounds() const
{
    if (m_points.isEmpty())
        return {};

    QRectF rect(m_points.first(), QSizeF(0, 0));
    for (const auto& point : m_points)
        rect = rect.united(QRectF(point, QSizeF(0, 0)));
    return rect.normalized();
}

void PolylineEntity2D::translate(const QPointF& delta)
{
    for (auto& point : m_points)
        point += delta;
}

// ============================================================
//  CircleEntity2D
// ============================================================

CircleEntity2D::CircleEntity2D(QString id, QPointF center, double radius)
    : m_id(std::move(id))
    , m_center(center)
    , m_radius(radius)
{
}

QString CircleEntity2D::id() const
{
    return m_id;
}

QString CircleEntity2D::name() const
{
    return QStringLiteral("Circle");
}

QString CircleEntity2D::typeName() const
{
    return QStringLiteral("CircleEntity2D");
}

bool CircleEntity2D::selected() const
{
    return m_selected;
}

void CircleEntity2D::setSelected(bool selected)
{
    m_selected = selected;
}

bool CircleEntity2D::highlighted() const
{
    return m_highlighted;
}

void CircleEntity2D::setHighlighted(bool highlighted)
{
    m_highlighted = highlighted;
}

QPointF CircleEntity2D::center() const
{
    return m_center;
}

double CircleEntity2D::radius() const
{
    return m_radius;
}

void CircleEntity2D::setCenter(const QPointF& center)
{
    m_center = center;
}

void CircleEntity2D::setRadius(double radius)
{
    m_radius = radius;
}

QRectF CircleEntity2D::bounds() const
{
    return QRectF(m_center.x() - m_radius, m_center.y() - m_radius, m_radius * 2.0, m_radius * 2.0);
}

// ============================================================
//  ArcEntity2D
// ============================================================

ArcEntity2D::ArcEntity2D(QString id, QPointF center, double radius, double startAngleDeg, double spanDeg)
    : m_id(std::move(id))
    , m_center(center)
    , m_radius(radius)
    , m_startAngleDeg(startAngleDeg)
    , m_spanDeg(spanDeg)
{
}

QString ArcEntity2D::id() const
{
    return m_id;
}

QString ArcEntity2D::name() const
{
    return QStringLiteral("Arc");
}

QString ArcEntity2D::typeName() const
{
    return QStringLiteral("ArcEntity2D");
}

bool ArcEntity2D::selected() const
{
    return m_selected;
}

void ArcEntity2D::setSelected(bool selected)
{
    m_selected = selected;
}

bool ArcEntity2D::highlighted() const
{
    return m_highlighted;
}

void ArcEntity2D::setHighlighted(bool highlighted)
{
    m_highlighted = highlighted;
}

QPointF ArcEntity2D::center() const
{
    return m_center;
}

double ArcEntity2D::radius() const
{
    return m_radius;
}

double ArcEntity2D::startAngleDeg() const
{
    return m_startAngleDeg;
}

double ArcEntity2D::spanDeg() const
{
    return m_spanDeg;
}

// ============================================================
//  SceneNode
// ============================================================

SceneNode::SceneNode(QString id, QString name)
    : m_id(std::move(id))
    , m_name(std::move(name))
{
}

QString SceneNode::id() const
{
    return m_id;
}

QString SceneNode::name() const
{
    return m_name;
}

QString SceneNode::typeName() const
{
    return QStringLiteral("SceneNode");
}

bool SceneNode::selected() const
{
    return m_selected;
}

void SceneNode::setSelected(bool selected)
{
    m_selected = selected;
}

bool SceneNode::highlighted() const
{
    return m_highlighted;
}

void SceneNode::setHighlighted(bool highlighted)
{
    m_highlighted = highlighted;
}

void SceneNode::addChild(const std::shared_ptr<SceneNode>& child)
{
    if (child) m_children.push_back(child);
}

QVector<std::shared_ptr<SceneNode>> SceneNode::children() const
{
    return m_children;
}

std::shared_ptr<SceneNode> SceneNode::childByIdRecursive(const QString& id) const
{
    // 深度优先递归查找
    for (const auto& child : m_children)
    {
        if (!child)
            continue;
        if (child->id() == id)
            return child;
        auto nested = child->childByIdRecursive(id);
        if (nested)
            return nested;
    }
    return {};
}

QVector<QString> SceneNode::pathIdsRecursive() const
{
    // 返回当前节点以及所有子孙节点的 ID 路径，便于视图高亮和树展开
    QVector<QString> ids;
    ids.push_back(m_id);

    for (const auto& child : m_children)
    {
        if (!child)
            continue;

        const auto childIds = child->pathIdsRecursive();
        for (const auto& id : childIds)
            ids.push_back(id);
    }

    return ids;
}

QStringList SceneNode::pathNamesRecursive() const
{
    // 返回当前节点以及所有子孙节点名称，作为路径展示文本
    QStringList names;
    names.push_back(m_name);

    for (const auto& child : m_children)
    {
        if (!child)
            continue;

        const auto childNames = child->pathNamesRecursive();
        for (const auto& name : childNames)
            names.push_back(name);
    }

    return names;
}

// ============================================================
//  SelectionSet
// ============================================================

void SelectionSet::clear()
{
    for (auto& item : m_items)
    {
        if (!item)
            continue;
        item->setSelected(false);
        item->setHighlighted(false);
    }
    m_items.clear();
}

void SelectionSet::add(const std::shared_ptr<UiEntity>& entity)
{
    if (!entity || contains(entity->id()))
        return;
    m_items.push_back(entity);
    entity->setSelected(true);
    entity->setHighlighted(true);
}

void SelectionSet::remove(const QString& entityId)
{
    auto it = std::remove_if(m_items.begin(), m_items.end(), [&](const std::shared_ptr<UiEntity>& item) {
        if (!item || item->id() != entityId)
            return false;
        item->setSelected(false);
        item->setHighlighted(false);
        return true;
        });
    m_items.erase(it, m_items.end());
}

bool SelectionSet::contains(const QString& entityId) const
{
    return std::any_of(m_items.begin(), m_items.end(), [&](const std::shared_ptr<UiEntity>& item) {
        return item && item->id() == entityId;
        });
}

QVector<std::shared_ptr<UiEntity>> SelectionSet::items() const
{
    return m_items;
}

bool SelectionSet::empty() const
{
    return m_items.isEmpty();
}

// ============================================================
//  EntityDocument2D
// ============================================================

std::shared_ptr<LineEntity2D> EntityDocument2D::createLine(const QPointF& start, const QPointF& end)
{
    auto entity = std::make_shared<LineEntity2D>(QUuid::createUuid().toString(QUuid::WithoutBraces), start, end);
    m_lines.push_back(entity);
    return entity;
}

std::shared_ptr<PolylineEntity2D> EntityDocument2D::createPolyline(const QVector<QPointF>& points)
{
    auto entity = std::make_shared<PolylineEntity2D>(QUuid::createUuid().toString(QUuid::WithoutBraces), points);
    m_polylines.push_back(entity);
    return entity;
}

std::shared_ptr<CircleEntity2D> EntityDocument2D::createCircle(const QPointF& center, double radius)
{
    auto entity = std::make_shared<CircleEntity2D>(QUuid::createUuid().toString(QUuid::WithoutBraces), center, radius);
    m_circles.push_back(entity);
    return entity;
}

std::shared_ptr<ArcEntity2D> EntityDocument2D::createArc(const QPointF& center, double radius, double startAngleDeg, double spanDeg)
{
    auto entity = std::make_shared<ArcEntity2D>(QUuid::createUuid().toString(QUuid::WithoutBraces), center, radius, startAngleDeg, spanDeg);
    m_arcs.push_back(entity);
    return entity;
}

std::shared_ptr<UiEntity> EntityDocument2D::entityById(const QString& id) const
{
    // 依次在四种图元类型中查找
    if (auto line = lineById(id)) return line;
    if (auto polyline = polylineById(id)) return polyline;
    if (auto circle = circleById(id)) return circle;
    if (auto arc = arcById(id)) return arc;
    return {};
}

std::shared_ptr<LineEntity2D> EntityDocument2D::lineById(const QString& id) const
{
    for (const auto& line : m_lines)
    {
        if (line && line->id() == id)
            return line;
    }
    return {};
}

std::shared_ptr<PolylineEntity2D> EntityDocument2D::polylineById(const QString& id) const
{
    for (const auto& polyline : m_polylines)
    {
        if (polyline && polyline->id() == id)
            return polyline;
    }
    return {};
}

std::shared_ptr<CircleEntity2D> EntityDocument2D::circleById(const QString& id) const
{
    for (const auto& circle : m_circles)
    {
        if (circle && circle->id() == id)
            return circle;
    }
    return {};
}

std::shared_ptr<ArcEntity2D> EntityDocument2D::arcById(const QString& id) const
{
    for (const auto& arc : m_arcs)
    {
        if (arc && arc->id() == id)
            return arc;
    }
    return {};
}

void EntityDocument2D::removeEntity(const QString& id)
{
    // 先从选择集中移除
    m_selection.remove(id);

    // 再从各图元列表中移除
    auto lineIt = std::remove_if(m_lines.begin(), m_lines.end(), [&](const std::shared_ptr<LineEntity2D>& line) {
        return line && line->id() == id;
        });
    m_lines.erase(lineIt, m_lines.end());

    auto polyIt = std::remove_if(m_polylines.begin(), m_polylines.end(), [&](const std::shared_ptr<PolylineEntity2D>& polyline) {
        return polyline && polyline->id() == id;
        });
    m_polylines.erase(polyIt, m_polylines.end());

    auto circleIt = std::remove_if(m_circles.begin(), m_circles.end(), [&](const std::shared_ptr<CircleEntity2D>& circle) {
        return circle && circle->id() == id;
        });
    m_circles.erase(circleIt, m_circles.end());

    auto arcIt = std::remove_if(m_arcs.begin(), m_arcs.end(), [&](const std::shared_ptr<ArcEntity2D>& arc) {
        return arc && arc->id() == id;
        });
    m_arcs.erase(arcIt, m_arcs.end());
}

void EntityDocument2D::removeEntity(const std::shared_ptr<UiEntity>& entity)
{
    if (entity)
        removeEntity(entity->id());
}

QVector<std::shared_ptr<UiEntity>> EntityDocument2D::entities() const
{
    QVector<std::shared_ptr<UiEntity>> items;
    items.reserve(m_lines.size() + m_polylines.size() + m_circles.size() + m_arcs.size());
    for (const auto& e : m_lines) items.push_back(e);
    for (const auto& e : m_polylines) items.push_back(e);
    for (const auto& e : m_circles) items.push_back(e);
    for (const auto& e : m_arcs) items.push_back(e);
    return items;
}

QVector<std::shared_ptr<LineEntity2D>> EntityDocument2D::lines() const
{
    return m_lines;
}

QVector<std::shared_ptr<PolylineEntity2D>> EntityDocument2D::polylines() const
{
    return m_polylines;
}

QVector<std::shared_ptr<CircleEntity2D>> EntityDocument2D::circles() const
{
    return m_circles;
}

QVector<std::shared_ptr<ArcEntity2D>> EntityDocument2D::arcs() const
{
    return m_arcs;
}

SelectionSet& EntityDocument2D::selection()
{
    return m_selection;
}

const SelectionSet& EntityDocument2D::selection() const
{
    return m_selection;
}

void EntityDocument2D::clear()
{
    m_lines.clear();
    m_polylines.clear();
    m_circles.clear();
    m_arcs.clear();
    m_selection.clear();
}

// ============================================================
//  SceneDocument3D
// ============================================================

std::shared_ptr<SceneNode> SceneDocument3D::createNode(const QString& name)
{
    auto node = std::make_shared<SceneNode>(QUuid::createUuid().toString(QUuid::WithoutBraces), name);
    m_roots.push_back(node);
    return node;
}

std::shared_ptr<UiEntity> SceneDocument3D::entityById(const QString& id) const
{
    return nodeById(id);
}

std::shared_ptr<SceneNode> SceneDocument3D::nodeById(const QString& id) const
{
    for (const auto& node : m_roots)
    {
        if (node && node->id() == id)
            return node;
    }
    return {};
}

void SceneDocument3D::removeNode(const QString& id)
{
    m_selection.remove(id);

    auto it = std::remove_if(m_roots.begin(), m_roots.end(), [&](const std::shared_ptr<SceneNode>& node) {
        return node && node->id() == id;
        });
    m_roots.erase(it, m_roots.end());
}

void SceneDocument3D::removeNode(const std::shared_ptr<UiEntity>& entity)
{
    if (entity)
        removeNode(entity->id());
}

QVector<std::shared_ptr<UiEntity>> SceneDocument3D::entities() const
{
    QVector<std::shared_ptr<UiEntity>> items;
    items.reserve(m_roots.size());
    for (const auto& e : m_roots) items.push_back(e);
    return items;
}

QVector<std::shared_ptr<SceneNode>> SceneDocument3D::rootNodes() const
{
    return m_roots;
}

SelectionSet& SceneDocument3D::selection()
{
    return m_selection;
}

const SelectionSet& SceneDocument3D::selection() const
{
    return m_selection;
}

// ============================================================
//  DefaultCameraController3D
// ============================================================

void DefaultCameraController3D::orbit(double deltaYaw, double deltaPitch)
{
    m_yaw += deltaYaw;
    m_pitch = std::clamp(m_pitch + deltaPitch, -89.0, 89.0);
}

void DefaultCameraController3D::zoom(double delta)
{
    m_distance = std::clamp(m_distance + delta, 2.0, 500.0);
}

void DefaultCameraController3D::pan(const QPointF& delta)
{
    m_panOffset += delta;
}

void DefaultCameraController3D::reset()
{
    m_yaw = 0.0;
    m_pitch = 15.0;
    m_distance = 10.0;
    m_panOffset = QPointF();
}

double DefaultCameraController3D::yaw() const
{
    return m_yaw;
}

double DefaultCameraController3D::pitch() const
{
    return m_pitch;
}

double DefaultCameraController3D::distance() const
{
    return m_distance;
}