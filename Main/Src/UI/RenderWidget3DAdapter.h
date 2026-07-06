#pragma once

#include "Render3D/IRenderer3D.h"
#include <QString>
#include <memory>
#include <QWidget>

class RenderWidget3D;
class SceneDocument3D;
class CameraController3D;

/**
 * @file RenderWidget3DAdapter.h
 * @brief RenderWidget3D 到 IRenderer3D 的适配器
 *
 * 将现有的 OpenGL 渲染器 RenderWidget3D 适配到统一的 IRenderer3D 接口。
 * 这样 Viewport3D 可以在 SimpleRenderer3D（软件渲染）和 RenderWidget3D（OpenGL）之间切换。
 * RenderWidget3DAdapter 负责：
 * - 将 IRenderer3D 接口调用转发到 RenderWidget3D
 * - 管理 RenderWidget3D 的生命周期
 * - 转换 SceneDocument3D 到 Eg::SceneManager3D
 */

/**
 * @class RenderWidget3DAdapter
 * @brief RenderWidget3D 的 IRenderer3D 适配器
 *
 * 使用组合模式，内部持有 RenderWidget3D 实例，
 * 将 IRenderer3D 接口调用转换为 RenderWidget3D 的对应方法。
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

    void setScene(SceneDocument3D* document) override;
    void setCamera(CameraController3D* controller) override;
    void render(QPainter& painter, int width, int height) override;
    void resize(int width, int height) override;
    void resetView() override;
    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    bool isOrbitMode() const override;

    /// 判断是否使用 OpenGL 渲染
    bool isOpenGL() const override { return true; }

    void onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH) override;
    void onMouseMove(int x, int y, int buttons, int viewW, int viewH) override;
    void onMouseRelease(int x, int y, int button, int viewW, int viewH) override;
    void onWheel(int delta, int viewW, int viewH) override;

    void selectNodeById(const QString& nodeId) override;
    QString selectedNodeId() const override;
    QStringList selectedPathNames() const override;

    void setStatusCallback(StatusCallback callback) override;
    void setSelectionCallback(SelectionCallback callback) override;
    void setPathCallback(PathCallback callback) override;

    // ========== 特殊访问器 ==========

    /// 获取内部的 RenderWidget3D 实例（用于嵌入到 Qt 布局中）
    RenderWidget3D* widget() const;

    /// 设置父 widget（必须在 initialize 之前调用）
    void setParentWidget(QWidget* parent);

private:
    /// 创建内部渲染控件
    bool ensureWidgetCreated();
    /// 绑定内部控件信号
    void bindWidgetSignals();
    /// 触发状态回调
    void emitStatus(const QString& text);

private:
    /// 父 widget
    QWidget* m_parentWidget{ nullptr };
    /// 内部 RenderWidget3D 实例
    std::unique_ptr<RenderWidget3D> m_renderWidget;
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