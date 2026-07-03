/**
 * @file UiEntities.h
 * @brief UI 层实体定义 — 2D/3D 图元、场景节点、选择集与文档模型
 *
 * 定义了画布上所有可显示、可交互的 UI 实体类型，
 * 以及管理这些实体的文档类（EntityDocument2D / SceneDocument3D）。
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPointF>
#include <QRectF>

#include <memory>

 // ============================================================
 //  UiEntity — 所有 UI 实体的抽象基类
 // ============================================================

 /**
  * @brief UI 实体抽象基类
  *
  * 提供统一的实体标识、名称、类型查询以及选中/高亮状态管理。
  * 所有 2D 图元（线段、多段线、圆、弧）和 3D 场景节点均继承此类。
  */
class UiEntity
{
public:
    virtual ~UiEntity() = default;

    /// 返回实体唯一标识符
    virtual QString id() const = 0;

    /// 返回实体显示名称
    virtual QString name() const = 0;

    /// 返回实体类型名称（如 "LineEntity2D"）
    virtual QString typeName() const = 0;

    /// 返回是否处于选中状态
    virtual bool selected() const = 0;

    /// 设置选中状态
    virtual void setSelected(bool selected) = 0;

    /// 设置高亮状态
    virtual void setHighlighted(bool highlighted) = 0;

    /// 返回是否处于高亮状态
    virtual bool highlighted() const = 0;
};

// ============================================================
//  LineEntity2D — 2D 线段实体
// ============================================================

/**
 * @brief 2D 线段实体
 *
 * 表示由起点和终点定义的线段，支持长度、方向、参数化采样及距离计算。
 */
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

    /// 返回线段的轴对齐包围盒
    QRectF bounds() const;

    /// 返回线段长度
    double length() const;

    /// 返回单位方向向量（从起点指向终点）
    QPointF direction() const;

    /// 参数化采样：t=0 返回起点，t=1 返回终点
    QPointF pointAt(double t) const;

    /// 返回点到线段的最短距离
    double distanceToPoint(const QPointF& point) const;

    /// 返回点到起点的欧氏距离
    double distanceToStart(const QPointF& point) const;

    /// 返回点到终点的欧氏距离
    double distanceToEnd(const QPointF& point) const;

private:
    QString m_id;
    QPointF m_start;
    QPointF m_end;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================
//  PolylineEntity2D — 2D 多段线实体
// ============================================================

/**
 * @brief 2D 多段线实体
 *
 * 表示由有序顶点列表构成的折线，支持整体平移和包围盒计算。
 */
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

    /// 返回所有顶点的轴对齐包围盒
    QRectF bounds() const;

    /// 将所有顶点平移指定偏移量
    void translate(const QPointF& delta);

private:
    QString m_id;
    QVector<QPointF> m_points;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================
//  CircleEntity2D — 2D 圆实体
// ============================================================

/**
 * @brief 2D 圆实体
 *
 * 表示由圆心和半径定义的圆。
 */
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

    /// 返回圆的轴对齐包围盒
    QRectF bounds() const;

private:
    QString m_id;
    QPointF m_center;
    double m_radius{ 0.0 };
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================
//  ArcEntity2D — 2D 圆弧实体
// ============================================================

/**
 * @brief 2D 圆弧实体
 *
 * 表示由圆心、半径、起始角度和扫过角度定义的圆弧。
 */
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

    /// 返回起始角度（度，逆时针为正）
    double startAngleDeg() const;

    /// 返回扫过角度（度，逆时针为正）
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

// ============================================================
//  SceneNode — 3D 场景节点
// ============================================================

/**
 * @brief 3D 场景节点
 *
 * 支持树形层级结构，可递归查找子节点并收集路径信息。
 */
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

    /// 添加子节点
    void addChild(const std::shared_ptr<SceneNode>& child);

    /// 返回直接子节点列表
    QVector<std::shared_ptr<SceneNode>> children() const;

    /// 根据 ID 递归查找子节点（深度优先）
    std::shared_ptr<SceneNode> childByIdRecursive(const QString& id) const;

    /// 递归收集当前节点及所有后代的 ID 路径
    QVector<QString> pathIdsRecursive() const;

    /// 递归收集当前节点及所有后代的名称路径
    QStringList pathNamesRecursive() const;

private:
    QString m_id;
    QString m_name;
    QVector<std::shared_ptr<SceneNode>> m_children;
    bool m_selected{ false };
    bool m_highlighted{ false };
};

// ============================================================
//  SelectionSet — 选择集
// ============================================================

/**
 * @brief 选择集
 *
 * 管理一组被选中的 UI 实体，负责维护选中/高亮状态的一致性。
 */
class SelectionSet
{
public:
    /// 清空选择集，同时取消所有实体的选中和高亮状态
    void clear();

    /// 添加实体到选择集，同时设置其选中和高亮状态
    void add(const std::shared_ptr<UiEntity>& entity);

    /// 根据 ID 从选择集移除实体，同时取消其选中和高亮状态
    void remove(const QString& entityId);

    /// 判断选择集是否包含指定 ID 的实体
    bool contains(const QString& entityId) const;

    /// 返回选择集中的所有实体
    QVector<std::shared_ptr<UiEntity>> items() const;

    /// 判断选择集是否为空
    bool empty() const;

private:
    QVector<std::shared_ptr<UiEntity>> m_items;
};

// ============================================================
//  EntityDocument2D — 2D 实体文档
// ============================================================

/**
 * @brief 2D 实体文档
 *
 * 管理 2D 画布中所有图元（线段、多段线、圆、弧）的创建、查询、删除，
 * 并持有全局选择集。
 */
class EntityDocument2D
{
public:
    /// 创建线段并添加到文档，返回新实体
    std::shared_ptr<LineEntity2D> createLine(const QPointF& start, const QPointF& end);

    /// 创建多段线并添加到文档，返回新实体
    std::shared_ptr<PolylineEntity2D> createPolyline(const QVector<QPointF>& points);

    /// 创建圆并添加到文档，返回新实体
    std::shared_ptr<CircleEntity2D> createCircle(const QPointF& center, double radius);

    /// 创建圆弧并添加到文档，返回新实体
    std::shared_ptr<ArcEntity2D> createArc(const QPointF& center, double radius, double startAngleDeg, double spanDeg);

    /// 根据 ID 在所有图元类型中查找实体
    std::shared_ptr<UiEntity> entityById(const QString& id) const;

    /// 根据 ID 查找线段
    std::shared_ptr<LineEntity2D> lineById(const QString& id) const;

    /// 根据 ID 查找多段线
    std::shared_ptr<PolylineEntity2D> polylineById(const QString& id) const;

    /// 根据 ID 查找圆
    std::shared_ptr<CircleEntity2D> circleById(const QString& id) const;

    /// 根据 ID 查找圆弧
    std::shared_ptr<ArcEntity2D> arcById(const QString& id) const;

    /// 根据 ID 从文档中删除实体，同时从选择集中移除
    void removeEntity(const QString& id);

    /// 删除指定实体
    void removeEntity(const std::shared_ptr<UiEntity>& entity);

    /// 返回文档中所有实体（合并四种类型）
    QVector<std::shared_ptr<UiEntity>> entities() const;

    QVector<std::shared_ptr<LineEntity2D>> lines() const;
    QVector<std::shared_ptr<PolylineEntity2D>> polylines() const;
    QVector<std::shared_ptr<CircleEntity2D>> circles() const;
    QVector<std::shared_ptr<ArcEntity2D>> arcs() const;

    /// 返回全局选择集（可读写）
    SelectionSet& selection();

    /// 返回全局选择集（只读）
    const SelectionSet& selection() const;

    /// 清空文档中所有实体和选择集
    void clear();

private:
    QVector<std::shared_ptr<LineEntity2D>> m_lines;
    QVector<std::shared_ptr<PolylineEntity2D>> m_polylines;
    QVector<std::shared_ptr<CircleEntity2D>> m_circles;
    QVector<std::shared_ptr<ArcEntity2D>> m_arcs;
    SelectionSet m_selection;
};

// ============================================================
//  SceneDocument3D — 3D 场景文档
// ============================================================

/**
 * @brief 3D 场景文档
 *
 * 管理 3D 场景中的节点树和选择集。
 */
class SceneDocument3D
{
public:
    /// 创建根节点并添加到场景，返回新节点
    std::shared_ptr<SceneNode> createNode(const QString& name);

    /// 根据 ID 查找实体（委托给 nodeById）
    std::shared_ptr<UiEntity> entityById(const QString& id) const;

    /// 根据 ID 在根节点中查找节点
    std::shared_ptr<SceneNode> nodeById(const QString& id) const;

    /// 根据 ID 从场景中删除根节点，同时从选择集中移除
    void removeNode(const QString& id);

    /// 删除指定节点
    void removeNode(const std::shared_ptr<UiEntity>& entity);

    /// 返回场景中所有实体（即根节点列表）
    QVector<std::shared_ptr<UiEntity>> entities() const;

    /// 返回根节点列表
    QVector<std::shared_ptr<SceneNode>> rootNodes() const;

    /// 返回全局选择集（可读写）
    SelectionSet& selection();

    /// 返回全局选择集（只读）
    const SelectionSet& selection() const;

private:
    QVector<std::shared_ptr<SceneNode>> m_roots;
    SelectionSet m_selection;
};

// ============================================================
//  CameraController3D / DefaultCameraController3D — 3D 相机控制
// ============================================================

/**
 * @brief 3D 相机控制器抽象接口
 *
 * 定义轨道旋转、缩放、平移和重置操作。
 */
class CameraController3D
{
public:
    virtual ~CameraController3D() = default;

    /// 轨道旋转：deltaYaw 水平旋转，deltaPitch 垂直旋转
    virtual void orbit(double deltaYaw, double deltaPitch) = 0;

    /// 缩放：delta > 0 拉近，delta < 0 拉远
    virtual void zoom(double delta) = 0;

    /// 平移：delta 为屏幕空间偏移量
    virtual void pan(const QPointF& delta) = 0;

    /// 重置相机到默认视角
    virtual void reset() = 0;
};

/**
 * @brief 默认 3D 相机控制器
 *
 * 使用球坐标（yaw / pitch / distance）管理相机状态，
 * 并支持平移偏移。
 */
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
