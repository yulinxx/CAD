#pragma once

#include "IRenderBackend.h"
#include "RenderContext.h"
#include "RenderFrame.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <memory>

/**
 * @file MinimalOpenGLBackend.h
 * @brief 最小可运行 OpenGL 后端骨架
 *
 * 实现 IRenderBackend 接口，使用 Qt 的 OpenGL 4.5 Core Profile 封装：
 * - 支持离屏渲染（FBO）
 * - 支持帧捕获（glReadPixels）
 * - 支持基本图元渲染（后续接入 VBO + Shader）
 * - 支持视图变换
 *
 * 这是后续完整 OpenGL 后端的起点。
 * 当前阶段仅证明渲染管线可跑通，不追求性能。
 */
class MinimalOpenGLBackend final : public IRenderBackend
{
public:
    MinimalOpenGLBackend();
    ~MinimalOpenGLBackend() override;

    // ============ 生命周期 ============

    bool initialize(void* nativeWindowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;

    // ============ 上下文绑定 ============

    void bindContext(const RenderContext& context) override;
    const RenderContext& context() const override;

    // ============ 场景绑定 ============

    void setScene(EntityDocument2D* document) override;
    void setScene(SceneDocument3D* document) override;
    void setCamera(CameraController3D* controller) override;

    // ============ 渲染管线 ============

    void compile() override;
    void submitFrame(const RenderFrame& frame) override;
    void render() override;
    void beginFrame() override;
    void endFrame() override;

    // ============ 视口控制 ============

    void resize(const QSize& size) override;
    void resetView() override;

    // ============ 模式切换 ============

    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    void setRenderMode(RenderMode mode) override;
    RenderMode renderMode() const override;

    // ============ 帧输出 ============

    QImage captureFrame() const override;
    RenderStatistics getStatistics() const override;

    // ============ 后端信息 ============

    QString backendName() const override;
    bool supportsCapability(BackendCapability cap) const override;
    BackendCapability capabilities() const override;

private:
    /// 确保 OpenGL 上下文已创建
    bool ensureGLContext();

    /// 确保 FBO 尺寸正确
    bool ensureFBO();

    /// 清屏并绘制测试网格
    void renderTestGrid();

private:
    // Qt OpenGL 资源
    std::unique_ptr<QOpenGLContext> m_glContext;
    std::unique_ptr<QOffscreenSurface> m_offscreenSurface;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;

    // 渲染上下文
    RenderContext m_context;
    RenderStatistics m_stats;

    // 场景引用
    EntityDocument2D* m_document2D{ nullptr };
    SceneDocument3D* m_document3D{ nullptr };
    CameraController3D* m_camera{ nullptr };

    // 状态
    bool m_ready{ false };
    bool m_initialized{ false };
    RenderFrame m_lastFrame;
    BackendCapability m_capabilities{
        BackendCapability::HardwareAccelerated
        | BackendCapability::AntiAliasing
        | BackendCapability::HighDPI
        | BackendCapability::OffscreenRendering
    };
};