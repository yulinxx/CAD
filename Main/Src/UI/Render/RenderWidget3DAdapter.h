#pragma once

#include "Render3D/IRenderer3D.h"
#include <QString>
#include <memory>
#include <QWidget>

class RenderWidget3D;
class SceneDocument3DAdapter;
class CameraController3D;

namespace Eg
{
    class SceneManager3D;
}

/**
 * @file RenderWidget3DAdapter.h
 * @brief RenderWidget3D 到 IRenderer3D 的适配器（过渡层）
 *
 * 将现有的 OpenGL 渲染器 RenderWidget3D 适配到统一的 IRenderer3D 接口。
 *
 * ⚠️ 过渡期标记：此适配器是临时桥接方案。
 * 当统一渲染链路（RenderCore）完全稳定后，此适配器将逐步退场。
 *
 * 退场条件：
 * - RenderCore 统一渲染管线跑稳
 * - 新路径成为默认渲染路线
 * - UI 层不再依赖旧 RenderWidget3D 实现
 *
 * 过渡期间约束：
 * - 不新增复杂业务逻辑
 * - 不负责主渲染链路
 * - 仅做接口适配和事件转发
 */

/**
 * @class RenderWidget3DAdapter
 * @brief RenderWidget3D 的 IRenderer3D 适配器（过渡层）
 *
 * 使用组合模式，内部持有 RenderWidget3D 实例，
 * 将 IRenderer3D 接口调用转换为 RenderWidget3D 的对应方法。
 *
 * 父窗口通过 initialize(void*) 传入，适配器负责创建内部控件。
 */
class RenderWidget3DAdapter : public IRenderer3D
{
public:
    RenderWidget3DAdapter();
    ~RenderWidget3DAdapter() override;

public:
    // ========== IRenderer3D 接口实现 ==========

    bool initialize(void* windowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;
    void setRenderLoopEnabled(bool enabled) override;
    bool isRenderLoopRunning() const override;

    void setScene(SceneDocument3DAdapter* document) override;
    void setCamera(CameraController3D* controller) override;
    void render(QPainter& painter, int width, int height) override;
    void resize(int width, int height) override;
    void resetView() override;
    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    bool isOrbitMode() const override;

    /// 判断是否使用 OpenGL 渲染
    bool isOpenGL() const override
    {
        return true;
    }

    void selectNodeById(const QString& nodeId) override;
    QString selectedNodeId() const override;
    QStringList selectedPathNames() const override;

    void setStatusCallback(StatusCallback callback) override;
    void setSelectionCallback(SelectionCallback callback) override;
    void setPathCallback(PathCallback callback) override;

    // ========== 特殊访问器 ==========

    /// 获取内部的 RenderWidget3D 实例（过渡期使用）
    RenderWidget3D* widget() const;

private:
    /// 创建内部渲染控件
    bool ensureWidgetCreated();
    /// 绑定内部控件信号
    void bindWidgetSignals();
    /// 触发状态回调
    void emitStatus(const QString& text);

private:
    /// 父窗口（由 initialize() 传入）
    QWidget* m_parentWidget{ nullptr };
    /// 内部 RenderWidget3D 实例
    std::unique_ptr<RenderWidget3D> m_renderWidget;
    /// 场景管理器引用（在 setScene() 时保存）
    Eg::SceneManager3D* m_sceneManager{ nullptr };
    /// 是否就绪
    bool m_ready{ false };
    /// 是否启用渲染循环
    bool m_renderLoopEnabled{ false };
    /// 当前选中节点 ID
    QString m_selectedNodeId;
    /// 当前选中路径
    QStringList m_selectedPathNames;

    /// 回调
    StatusCallback m_statusCallback;
    SelectionCallback m_selectionCallback;
    PathCallback m_pathCallback;
};