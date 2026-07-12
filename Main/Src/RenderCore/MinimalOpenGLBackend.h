#pragma once

#include "IRenderBackend.h"
#include "RenderContext.h"
#include "RenderFrame.h"

#include <memory>
#include <string>

namespace Eg
{
    class SceneManager;
}

class MinimalOpenGLBackend final : public IRenderBackend
{
public:
    MinimalOpenGLBackend();
    ~MinimalOpenGLBackend() override;

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
    bool ensureGLContext();
    bool ensureFBO();
    void renderTestGrid();

private:
    struct GLResources;
    std::unique_ptr<GLResources> m_glResources;

    RenderContext m_context;
    RenderStatistics m_stats;

    Eg::SceneManager* m_scene{ nullptr };
    CameraController3D* m_camera{ nullptr };

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
