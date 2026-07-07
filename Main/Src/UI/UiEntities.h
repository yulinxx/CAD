#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <QRectF>

#include <memory>

/**
 * @deprecated UI 层实体基类。新功能应直接使用 Engine 层的 Eg::SyEntity / IEntity。
 *             EntityDocument2D + UiEntity 系列将在文档模型统一后移除。
 *             参见 Docs/refactoring-baseline.md 章节 1-2。
 */
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

/**
 * @deprecated 见 UiEntity。变换操作将逐步迁移到 Engine 层。
 */
class ITransformable
{
public:
    virtual ~ITransformable() = default;
    virtual void rotate(const QPointF& center, double cosAngle, double sinAngle) = 0;
    virtual void translate(const QPointF& delta) = 0;
    virtual void scale(const QPointF& center, double factor) = 0;
    virtual QPointF center() const = 0;
    virtual QVector<QPointF> keyPoints() const = 0;
    virtual void setKeyPoints(const QVector<QPointF>& points) = 0;
};

// ============================================================ 
class LineEntity2D final : public UiEntity, public ITransformable
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

    void rotate(const QPointF& center, double cosAngle, double sinAngle) override;
    void translate(const QPointF& delta) override;
    void scale(const QPointF& center, double factor) override;
    QPointF center() const override;
    QVector<QPointF> keyPoints() const override;
    void setKeyPoints(const QVector<QPointF>& points) override;
private:
    QString m_id;
    QPointF m_start;
    QPointF m_end;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================ 
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

// ============================================================ 
class CircleEntity2D final : public UiEntity, public ITransformable
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

    void rotate(const QPointF& center, double cosAngle, double sinAngle) override;
    void translate(const QPointF& delta) override;
    void scale(const QPointF& center, double factor) override;
    QVector<QPointF> keyPoints() const override;
    void setKeyPoints(const QVector<QPointF>& points) override;
private:
    QString m_id;
    QPointF m_center;
    double m_radius{ 0.0 };
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================ 
class ArcEntity2D final : public UiEntity, public ITransformable
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

    void rotate(const QPointF& center, double cosAngle, double sinAngle) override;
    void translate(const QPointF& delta) override;
    void scale(const QPointF& center, double factor) override;
    QVector<QPointF> keyPoints() const override;
    void setKeyPoints(const QVector<QPointF>& points) override;
private:
    QString m_id;
    QPointF m_center;
    double m_radius{ 0.0 };
    double m_startAngleDeg{ 0.0 };
    double m_spanDeg{ 0.0 };
    bool m_selected{ false };
    bool m_highlighted{ false };
};


// ============================================================ 
class SceneNode final : public UiEntity
{
public:
    SceneNode(QString id, QString name);

public:
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

// ============================================================ 
/**
 * @brief 选择集容器
 *
 * 短期事实源 — 当前 UI 层选择状态以此为准。
 * 长期目标：迁移到 Eg::SceneManager + Engine 层实体选择模型。
 * 视口不再维护选择副本，所有选择读写均通过此容器。
 */
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

/**
 * @deprecated UI 层 2D 文档模型。与 Engine 层 Eg::SceneManager 职责完全重叠。
 *             新功能禁止新增业务能力到此类型。
 *             迁移目标: Eg::SceneManager (Engine/2D/Core/SceneManager.h)
 *             EntityDocument2D 将在重构完成后移除。
 *
 * 短期选择事实源 — selection() 作为 UI 层唯一选择状态来源。
 * 长期目标：迁移到 Eg::SceneManager 的选择模型。
 * 视口、属性面板、状态中心均从此处读取选择状态。
 */
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
    std::shared_ptr<UiEntity> hitTest(const QPointF& point, double tolerance = 5.0) const;
private:
    QVector<std::shared_ptr<LineEntity2D>> m_lines;
    QVector<std::shared_ptr<PolylineEntity2D>> m_polylines;
    QVector<std::shared_ptr<CircleEntity2D>> m_circles;
    QVector<std::shared_ptr<ArcEntity2D>> m_arcs;
    SelectionSet m_selection;
};

// ============================================================ 
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

// ============================================================ 
class CameraController3D
{
public:
    virtual ~CameraController3D() = default;
    virtual void orbit(double deltaYaw, double deltaPitch) = 0;
    virtual void zoom(double delta) = 0;
    virtual void pan(const QPointF& delta) = 0;
    virtual void reset() = 0;
};

// ============================================================ 
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