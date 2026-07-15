#pragma once

#include <memory>
#include <QString>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <string>
#include <vector>

#include "UI/SceneDocumentBase.h"

class SceneEditService;

namespace Eg
{
    class SceneManager; struct SyEntity;
}

/**
 * @brief 2D 场景文档 — UI 层对 Engine2D 的适配层
 *
 * 内部托管 Eg::SceneManager，通过 SceneEditService 执行所有编辑操作，
 * 确保事务和撤销支持。提供 Qt 类型便利方法（QPointF 接口）供 UI 层使用。
 *
 * 新代码应优先通过 SceneEditService 操作，本类仅作为兼容旧 API 的适配层。
 */
class SceneDocument2D : public UI::SceneDocumentBase
{
public:
    SceneDocument2D();
    explicit SceneDocument2D(SceneEditService* editService);
    ~SceneDocument2D() override;

    SceneDocument2D(const SceneDocument2D&) = delete;
    SceneDocument2D& operator=(const SceneDocument2D&) = delete;

    Eg::SceneManager* sceneManager() const
    {
        return m_scene;
    }

    void setEditService(SceneEditService* editService);
    SceneEditService* editService() const
    {
        return m_editService;
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
    QVector<QString> allEntityIdsQ() const;
    Eg::SyEntity* entityByStringId(const QString& id) const;

    

    // ---- 编辑 ----

    void removeEntity(const QString& id);

    // ---- SceneDocumentBase 接口 (选择方法已废弃，迁移至 SelectionService) ----

    std::vector<std::string> allEntityIds() const override;
    [[deprecated("Use SelectionService::select() instead")]]
    void selectEntity(const std::string& id) override;
    [[deprecated("Use SelectionService::clear() instead")]]
    void clearSelection() override;
    [[deprecated("Use SelectionService::selectedIds() instead")]]
    std::vector<std::string> selectedIds() const override;
    void removeEntity(const std::string& id) override;
    void clear() override;

private:
    Eg::SceneManager* m_scene{ nullptr };
    SceneEditService* m_editService{ nullptr };
};
