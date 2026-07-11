#pragma once

#include "Render3D/IRenderer3D.h"
#include "RenderContext.h"
#include "RenderFrame.h"
#include "ViewCamera3D.h"

#include <memory>

class SceneDocument3D;
class CameraController3D;
class SceneCompiler;
class SoftwareRenderer;

/**
 * @file RenderCoreRenderer.h
 * @brief UI → RenderCore 渲染管线桥接器
 *
 * 实现 IRenderer3D 接口，将 Viewport3D 的渲染请求桥接到 RenderCore 统一管线。
 *
 * 职责边界（严格遵循桥接模式，仅做调度与转发）：
 * - 生命周期管理 → 初始化/关闭状态维护
 * - 场景绑定 → 转发给 SceneCompiler
 * - 编译调度 → 调用 SceneCompiler::compile()，不做编译决策
 * - 渲染派发 → 委托 SoftwareRenderer 执行实际渲染
 * - 选择管理 → 存储选中节点 ID，触发回调
 * - 相机姿态 → 委托给 ViewCamera3D
 * - 输入事件 → 完全委托给 ViewCamera3D
 *
 * 不承担：
 * - 相机投影计算（由 ViewCamera3D 负责）
 * - 场景遍历与批次生成（由 SceneCompiler 负责）
 * - 渲染实现（由 SoftwareRenderer/IRenderBackend 负责）
 * - 输入事件策略（完全委托给 ViewCamera3D）
 * - 场景编译决策（增量/全量由 SceneCompiler 内部决定）
 */
class RenderCoreRenderer : public IRenderer3D
{
public:
    RenderCoreRenderer();
    ~RenderCoreRenderer() override;

    // ========== IRenderer3D 接口 ==========

    bool initialize(void* windowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;
    void setRenderLoopEnabled(bool enabled) override;
    bool isRenderLoopRunning() const override;
    bool isOpenGL() const override;

    void setScene(SceneDocument3D* document) override;
    void setCamera(CameraController3D* controller) override;
    void render(QPainter& painter, int width, int height) override;
    void resize(int width, int height) override;
    void resetView() override;
    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    bool isOrbitMode() const override;

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

private:
    /// 编译场景 → RenderFrame（委托 SceneCompiler，不做编译决策）
    RenderFrame compileScene();

    /// 发射状态文本到回调
    void emitStatus(const QString& text);

private:
    // 场景数据
    SceneDocument3D* m_document{ nullptr };
    CameraController3D* m_cameraController{ nullptr };

    // 渲染管线核心
    std::unique_ptr<SceneCompiler> m_compiler;
    std::unique_ptr<SoftwareRenderer> m_softwareRenderer;
    ViewCamera3D m_camera;
    RenderContext m_context;

    // 生命周期
    bool m_ready{ false };
    bool m_renderLoopEnabled{ false };

    // 上一帧缓存（用于增量编译）
    RenderFrame m_lastFrame;

    // 选中
    QString m_selectedNodeId;
    QStringList m_selectedPathNames;

    // 回调
    StatusCallback m_statusCallback;
    SelectionCallback m_selectionCallback;
    PathCallback m_pathCallback;
};