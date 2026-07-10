#pragma once

#include <memory>
#include <QString>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace Eg { class SceneManager; class SyEntity; }

/**
 * @brief 2D 场景文档 — 围绕 Eg::SceneManager 的 UI 层适配
 *
 * 取代已移除的 EntityDocument2D。
 * 内部托管 Eg::SceneManager，提供 UI 层便利方法（QPointF 接口）。
 * 长期目标：UI 层直接使用 Eg::SceneManager + Ut::Vec2d。
 */
class SceneDocument2D
{
public:
    SceneDocument2D();
    ~SceneDocument2D();

    SceneDocument2D(const SceneDocument2D&) = delete;
    SceneDocument2D& operator=(const SceneDocument2D&) = delete;

    Eg::SceneManager* sceneManager() const { return m_scene; }

    // ---- 图元创建 (返回 Eg 实体 ID) ----

    QString createLine(const QPointF& start, const QPointF& end);
    QString createCircle(const QPointF& center, double radius);
    QString createArc(const QPointF& center, double radius, double startDeg, double endDeg);

    // ---- 查询 ----

    QString entityIdAt(const QPointF& point, double tolerance = 5.0) const;
    QVector<QString> allEntityIds() const;
    Eg::SyEntity* entityByStringId(const QString& id) const;

    // ---- 选择 ----

    void selectEntity(const QString& id);
    void clearSelection();
    QVector<QString> selectedIds() const;

    // ---- 编辑 ----

    void removeEntity(const QString& id);
    void clear();

private:
    Eg::SceneManager* m_scene;
};
