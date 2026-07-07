#pragma once

#include "IRenderBackend.h"
#include "RenderContext.h"
#include "RenderFrame.h"

#include <memory>

class EntityDocument2D;
class SceneDocument3D;
class CameraController3D;

namespace Eg { class SceneManager; }

/**
 * @file DefaultRenderBackend.h
 * @brief 默认渲染后端（占位实现）
 *
 * 实现完整的 IRenderBackend 接口，但不做实际 GPU 渲染。
 * 用于：
 * - 验证渲染管线接口完整性
 * - 作为未实现后端的占位符
 * - 软件渲染路径的兜底
 *
 * 后续完整后端（OpenGL/Vulkan/Metal）实现后，此占位实现可逐步替换。
 */
class DefaultRenderBackend final : public IRenderBackend
{
public:
    explicit DefaultRenderBackend(QString name, BackendCapability caps);
    ~DefaultRenderBackend() override = default;

    // ============ 生命周期 ============

    bool initialize(void* nativeWindowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;

    // ============ 上下文绑定 ============

    void bindContext(const RenderContext& context) override;
    const RenderContext& context() const override;

    // ============ 场景绑定 ============

    void setScene(EntityDocument2D* document) override;
    void setScene(Eg::SceneManager* scene) override;
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
    QString m_name;
    BackendCapability m_capabilities{ BackendCapability::None };
    bool m_ready{ false };

    RenderContext m_context;
    RenderStatistics m_stats;

    EntityDocument2D* m_document2D{ nullptr };
    Eg::SceneManager* m_scene{ nullptr };
    SceneDocument3D* m_document3D{ nullptr };
    CameraController3D* m_camera{ nullptr };
    RenderFrame m_lastFrame;
};