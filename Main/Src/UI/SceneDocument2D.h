#pragma once

#include <memory>
#include <QString>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <string>
#include <vector>

#include "UI/SceneDocumentBase.h"

namespace Eg
{
    class SceneManager; class SyEntity;
}

/**
 * @brief 2D 场景文档 — 围绕 Eg::SceneManager 的 UI 层适配
 *
 * 取代已移除的 EntityDocument2D。
 * 内部托管 Eg::SceneManager，提供 UI 层便利方法（QPointF 接口）。
 * 长期目标：UI 层直接使用 Eg::SceneManager + Ut::Vec2d。
 */
class SceneDocument2D : public UI::SceneDocumentBase
{
public:
    SceneDocument2D();
    ~SceneDocument2D() override;

    SceneDocument2D(const SceneDocument2D&) = delete;
    SceneDocument2D& operator=(const SceneDocument2D&) = delete;

    Eg::SceneManager* sceneManager() const
    {
        return m_scene;
    }

    // ---- 图元创建 (返回 Eg 实体 ID) ----

    QString createLine(const QPointF& start, const QPointF& end);
    QString createPolyline(const QVector<QPointF>& points);
    QString createCircle(const QPointF& center, double radius);
    QString createArc(const QPointF& center, double radius, double startDeg, double endDeg);
    QString createPolygon(const QVector<QPointF>& vertices);
    QString createBezier2(const QPointF& start, const QPointF& control, const QPointF& end);
    QString createBezier(const QPointF& start, const QPointF& control1, const QPointF& control2, const QPointF& end);
    QString createNurbs(const QVector<QPointF>& controlPoints);
    QString createSmartLine(const QVector<QPointF>& points);

    // ---- 查询 ----

    QString entityIdAt(const QPointF& point, double tolerance = 5.0) const;
    // Qt 类型便利方法，供 UI 层使用
    QVector<QString> allEntityIdsQ() const;
    Eg::SyEntity* entityByStringId(const QString& id) const;

    // ---- 选择 ----

    void selectEntity(const QString& id);
    void setSelectedEntityId(const QString& id);
    void setSelectedEntityIds(const QVector<QString>& ids);
    // Qt 类型便利方法，供 UI 层使用
    QVector<QString> selectedIdsQ() const;

    // ---- 编辑 ----

    void removeEntity(const QString& id);

    // ---- SceneDocumentBase 接口 ----

    std::vector<std::string> allEntityIds() const override;
    void selectEntity(const std::string& id) override;
    void clearSelection() override;
    std::vector<std::string> selectedIds() const override;
    void removeEntity(const std::string& id) override;
    void clear() override;

private:
    Eg::SceneManager* m_scene;
};
