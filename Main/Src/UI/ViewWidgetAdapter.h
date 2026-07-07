/**
 * @file ViewWidgetAdapter.h
 * @brief ViewWidget 适配器 — 让 CanvasViewport2D 能被 OperationBus 使用
 *
 * 这是一个轻量级适配器，将 CanvasViewport2D 的接口转换为 ViewWidget 的接口，
 * 使得 OperationBus 中的操作可以在旧系统中执行。
 *
 * 过渡期设计：
 * - 仅提供 OperationBus 所需的最小接口
 * - 将操作调用转发到 CanvasViewport2D
 * - 后续随 CanvasViewport2D 被替换而删除
 */
#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <functional>

class CanvasViewport2D;
class EntityDocument2D;
class SceneEditServiceAdapter;

namespace Eg
{
    class SceneManager;
}

/**
 * @brief ViewWidget 适配器 — 让 CanvasViewport2D 能被 OperationBus 使用
 *
 * 过渡期设计：
 * - 仅提供 OperationBus 所需的最小接口
 * - 将操作调用转发到 CanvasViewport2D
 * - 后续随 CanvasViewport2D 被替换而删除
 */
class ViewWidgetAdapter : public QObject
{
    Q_OBJECT

public:
    explicit ViewWidgetAdapter(CanvasViewport2D* viewport, QObject* parent = nullptr);
    ~ViewWidgetAdapter() override = default;

    /**
     * @brief 设置活动工具
     * @param toolName 工具名称
     *
     * 将工具名称映射到 CanvasViewport2D 的 enter*Mode 函数
     */
    void setActiveTool(const QString& toolName);

    /**
     * @brief 同步选择状态
     *
     * 从场景同步选择状态到活动工具
     */
    void syncSelectionFromScene();

    /**
     * @brief 更新渲染数据
     */
    void updateRenderData();

    /**
     * @brief 重置视图
     */
    void resetView();

    /**
     * @brief 缩放到适合
     */
    void zoomToFit();

    /**
     * @brief 获取当前鼠标世界坐标
     */
    QPointF getCurrentMouseWorldPos() const;

    /**
     * @brief 屏幕坐标到世界坐标转换
     */
    QPointF screenToWorld(const QPoint& screenPos) const;

    /**
     * @brief 获取场景管理器（过渡期返回 nullptr）
     */
    Eg::SceneManager* getSceneManager() const { return nullptr; }

    /**
     * @brief 获取文档
     */
    EntityDocument2D* document() const;

    /**
     * @brief 获取场景编辑服务适配器
     */
    SceneEditServiceAdapter* sceneEditService() const { return m_sceneEditAdapter; }

    /**
     * @brief 获取底层视口
     */
    CanvasViewport2D* viewport() const { return m_viewport; }

private:
    CanvasViewport2D* m_viewport{ nullptr };
    SceneEditServiceAdapter* m_sceneEditAdapter{ nullptr };
};
