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
struct SceneEntityInfo2D
{
    /// 图元稳定 ID
    QString id;
    /// 图元类型名称
    QString type;
};

class SceneDocument2D : public UI::SceneDocumentBase
{
public:
    SceneDocument2D();
    explicit SceneDocument2D(SceneEditService* editService);
    ~SceneDocument2D() override;

    SceneDocument2D(const SceneDocument2D&) = delete;
    SceneDocument2D& operator=(const SceneDocument2D&) = delete;

    /// 阶段1收口：不再向 UI 暴露底层 SceneManager。
    /// 需要场景对象的渲染桥接层请通过 editService()->sceneManager() 获取，
    /// 其余 UI 一律通过本类外观方法或 SceneEditService 操作场景。
    void setEditService(SceneEditService* editService);
    SceneEditService* editService() const
    {
        return m_editService;
    }

    // ---- 图元创建 (返回 Eg 图元 ID) ----

    QString createLine(const QPointF& start, const QPointF& end);
    QString createPolyline(const QVector<QPointF>& points);
    QString createCircle(const QPointF& center, double radius);
    QString createArc(const QPointF& center, double radius, double startDeg, double endDeg);
    QString createPolygon(const QVector<QPointF>& vertices);
    QString createBezier2(const QPointF& start, const QPointF& control, const QPointF& end);
    QString createBezier(const QPointF& start, const QPointF& control1, const QPointF& control2, const QPointF& end);
    QString createNurbs(const QVector<QPointF>& controlPoints);
    QString createSmartLine(const QVector<QPointF>& points);
    QString createText(const QPointF& position, const QString& text, double height);
    QString createSpline(const QVector<QPointF>& points);

    // ---- 查询 ----

    QString entityIdAt(const QPointF& point, double tolerance = 5.0) const;
    QVector<QString> allEntityIdsQ() const;
    QVector<SceneEntityInfo2D> entityInfos() const;

    // ---- 编辑 ----

    /// 通过稳定 ID 删除图元，统一经过编辑服务以保留撤销语义。
    bool tryRemoveEntity(const QString& id);
    void removeEntity(const QString& id);

    // ---- SceneDocumentBase 接口 ----

    void forEachEntityId(void(*visitor)(const char*, void*), void* ctx) const override;
    void removeEntity(const char* id) override;
    void clear() override;

private:
    Eg::SceneManager* m_scene{ nullptr };
    SceneEditService* m_editService{ nullptr };
};
