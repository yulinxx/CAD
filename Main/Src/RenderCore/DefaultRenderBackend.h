#pragma once

#include "IRenderBackend.h"
#include "RenderContext.h"
#include "RenderFrame.h"

#include <memory>
#include <string>

class CameraController3D;

namespace Eg
{
    class SceneManager;
}

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
    explicit DefaultRenderBackend(std::string name, BackendCapability caps);
    ~DefaultRenderBackend() override = default;

    bool initialize(void* nativeWindowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;

    void bindContext(const RenderContext& context) override;
    const RenderContext& context() const override;

    void setScene(Eg::SceneManager* scene) override;
    void setCamera(CameraController3D* controller) override;

    void compile() override;
    void submitFrame(const RenderFrame& frame) override;
    void render() override;
    void beginFrame() override;
    void endFrame() override;

    void resize(const Size2D& size) override;
    void resetView() override;

    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    void setRenderMode(RenderMode mode) override;
    RenderMode renderMode() const override;

    ImageBuffer captureFrame() const override;
    RenderStatistics getStatistics() const override;

    std::string backendName() const override;
    bool supportsCapability(BackendCapability cap) const override;
    BackendCapability capabilities() const override;

private:
    std::string m_name;
    BackendCapability m_capabilities{ BackendCapability::None };
    bool m_ready{ false };

    RenderContext m_context;
    RenderStatistics m_stats;

    Eg::SceneManager* m_scene{ nullptr };
    CameraController3D* m_camera{ nullptr };
    RenderFrame m_lastFrame;
};
