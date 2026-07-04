#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <QRectF>

#include <memory>

class UiEntity
{
public:
    virtual ~UiEntity() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString typeName() const = 0;
    virtual bool selected() const = 0;
    virtual void setSelected(bool selected) = 0;
    virtual void setHighlighted(bool highlighted) = 0;
    virtual bool highlighted() const = 0;
};

class LineEntity2D final : public UiEntity
{
public:
    LineEntity2D(QString id, QPointF start, QPointF end);
    QString id() const override;
    QString name() const override;
    QString typeName() const override;
    bool selected() const override;
    void setSelected(bool selected) override;
    void setHighlighted(bool highlighted) override;
    bool highlighted() const override;
    QPointF start() const;
    QPointF end() const;
    void setStart(const QPointF& start);
    void setEnd(const QPointF& end);
    QRectF bounds() const;
    double length() const;
    QPointF direction() const;
    QPointF pointAt(double t) const;
    double distanceToPoint(const QPointF& point) const;
    double distanceToStart(const QPointF& point) const;
    double distanceToEnd(const QPointF& point) const;
private:
    QString m_id;
    QPointF m_start;
    QPointF m_end;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

class PolylineEntity2D final : public UiEntity
{
public:
    PolylineEntity2D(QString id, QVector<QPointF> points);
    QString id() const override;
    QString name() const override;
    QString typeName() const override;
    bool selected() const override;
    void setSelected(bool selected) override;
    void setHighlighted(bool highlighted) override;
    bool highlighted() const override;
    QVector<QPointF> points() const;
    void setPoints(const QVector<QPointF>& points);
    QRectF bounds() const;
    void translate(const QPointF& delta);
private:
    QString m_id;
    QVector<QPointF> m_points;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

class CircleEntity2D final : public UiEntity
{
public:
    CircleEntity2D(QString id, QPointF center, double radius);
    QString id() const override;
    QString name() const override;
    QString typeName() const override;
    bool selected() const override;
    void setSelected(bool selected) override;
    void setHighlighted(bool highlighted) override;
    bool highlighted() const override;
    QPointF center() const;
    double radius() const;
    void setCenter(const QPointF& center);
    void setRadius(double radius);
    QRectF bounds() const;
private:
    QString m_id;
    QPointF m_center;
    double m_radius{ 0.0 };
    bool m_selected{ false };
    bool m_highlighted{ false };
};

class ArcEntity2D final : public UiEntity
{
public:
    ArcEntity2D(QString id, QPointF center, double radius, double startAngleDeg, double spanDeg);
    QString id() const override;
    QString name() const override;
    QString typeName() const override;
    bool selected() const override;
    void setSelected(bool selected) override;
    void setHighlighted(bool highlighted) override;
    bool highlighted() const override;
    QPointF center() const;
    double radius() const;
    double startAngleDeg() const;
    double spanDeg() const;
private:
    QString m_id;
    QPointF m_center;
    double m_radius{ 0.0 };
    double m_startAngleDeg{ 0.0 };
    double m_spanDeg{ 0.0 };
    bool m_selected{ false };
    bool m_highlighted{ false };
};

class SceneNode final : public UiEntity
{
public:
    SceneNode(QString id, QString name);
    QString id() const override;
    QString name() const override;
    QString typeName() const override;
    bool selected() const override;
    void setSelected(bool selected) override;
    void setHighlighted(bool highlighted) override;
    bool highlighted() const override;
    void addChild(const std::shared_ptr<SceneNode>& child);
    QVector<std::shared_ptr<SceneNode>> children() const;
    std::shared_ptr<SceneNode> childByIdRecursive(const QString& id) const;
    QVector<QString> pathIdsRecursive() const;
    QStringList pathNamesRecursive() const;
private:
    QString m_id;
    QString m_name;
    QVector<std::shared_ptr<SceneNode>> m_children;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

class SelectionSet
{
public:
    void clear();
    void add(const std::shared_ptr<UiEntity>& entity);
    void remove(const QString& entityId);
    bool contains(const QString& entityId) const;
    QVector<std::shared_ptr<UiEntity>> items() const;
    bool empty() const;
private:
    QVector<std::shared_ptr<UiEntity>> m_items;
};

class EntityDocument2D
{
public:
    std::shared_ptr<LineEntity2D> createLine(const QPointF& start, const QPointF& end);
    std::shared_ptr<PolylineEntity2D> createPolyline(const QVector<QPointF>& points);
    std::shared_ptr<CircleEntity2D> createCircle(const QPointF& center, double radius);
    std::shared_ptr<ArcEntity2D> createArc(const QPointF& center, double radius, double startAngleDeg, double spanDeg);
    std::shared_ptr<UiEntity> entityById(const QString& id) const;
    std::shared_ptr<LineEntity2D> lineById(const QString& id) const;
    std::shared_ptr<PolylineEntity2D> polylineById(const QString& id) const;
    std::shared_ptr<CircleEntity2D> circleById(const QString& id) const;
    std::shared_ptr<ArcEntity2D> arcById(const QString& id) const;
    void removeEntity(const QString& id);
    void removeEntity(const std::shared_ptr<UiEntity>& entity);
    QVector<std::shared_ptr<UiEntity>> entities() const;
    QVector<std::shared_ptr<LineEntity2D>> lines() const;
    QVector<std::shared_ptr<PolylineEntity2D>> polylines() const;
    QVector<std::shared_ptr<CircleEntity2D>> circles() const;
    QVector<std::shared_ptr<ArcEntity2D>> arcs() const;
    SelectionSet& selection();
    const SelectionSet& selection() const;
    void clear();
private:
    QVector<std::shared_ptr<LineEntity2D>> m_lines;
    QVector<std::shared_ptr<PolylineEntity2D>> m_polylines;
    QVector<std::shared_ptr<CircleEntity2D>> m_circles;
    QVector<std::shared_ptr<ArcEntity2D>> m_arcs;
    SelectionSet m_selection;
};

class SceneDocument3D
{
public:
    std::shared_ptr<SceneNode> createNode(const QString& name);
    std::shared_ptr<UiEntity> entityById(const QString& id) const;
    std::shared_ptr<SceneNode> nodeById(const QString& id) const;
    void removeNode(const QString& id);
    void removeNode(const std::shared_ptr<UiEntity>& entity);
    QVector<std::shared_ptr<UiEntity>> entities() const;
    QVector<std::shared_ptr<SceneNode>> rootNodes() const;
    SelectionSet& selection();
    const SelectionSet& selection() const;
private:
    QVector<std::shared_ptr<SceneNode>> m_roots;
    SelectionSet m_selection;
};

class CameraController3D
{
public:
    virtual ~CameraController3D() = default;
    virtual void orbit(double deltaYaw, double deltaPitch) = 0;
    virtual void zoom(double delta) = 0;
    virtual void pan(const QPointF& delta) = 0;
    virtual void reset() = 0;
};

class DefaultCameraController3D final : public CameraController3D
{
public:
    void orbit(double deltaYaw, double deltaPitch) override;
    void zoom(double delta) override;
    void pan(const QPointF& delta) override;
    void reset() override;
    double yaw() const;
    double pitch() const;
    double distance() const;
private:
    double m_yaw{ 0.0 };
    double m_pitch{ 15.0 };
    double m_distance{ 10.0 };
    QPointF m_panOffset;
};