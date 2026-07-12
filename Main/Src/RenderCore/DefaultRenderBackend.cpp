#include "DefaultRenderBackend.h"

#include <chrono>

// ============================================================================
// 构造/析构
// ============================================================================

DefaultRenderBackend::DefaultRenderBackend(std::string name, BackendCapability caps)
    : m_name(std::move(name))
    , m_capabilities(caps)
{
    m_context.backendName = m_name;
}

// ============================================================================
// 生命周期
// ============================================================================

bool DefaultRenderBackend::initialize(void* nativeWindowHandle)
{
    (void)nativeWindowHandle;
    m_ready = true;
    m_context.markDirty();
    return true;
}

void DefaultRenderBackend::shutdown()
{
    m_ready = false;
}

bool DefaultRenderBackend::isReady() const
{
    return m_ready;
}

// ============================================================================
// 上下文绑定
// ============================================================================

void DefaultRenderBackend::bindContext(const RenderContext& context)
{
    m_context = context;
    m_context.markDirty();
}

const RenderContext& DefaultRenderBackend::context() const
{
    return m_context;
}

// ============================================================================
// 场景绑定
// ============================================================================

void DefaultRenderBackend::setScene(Eg::SceneManager* scene)
{
    m_scene = scene;
    m_context.markDirty();
}

void DefaultRenderBackend::setCamera(CameraController3D* controller)
{
    m_camera = controller;
    m_context.markDirty();
}

// ============================================================================
// 渲染管线
// ============================================================================

void DefaultRenderBackend::compile()
{
    m_stats.compileTimeMs = 0.0f;
}

void DefaultRenderBackend::submitFrame(const RenderFrame& frame)
{
    m_lastFrame = frame;
    m_stats.batchCount = frame.batchCount();
    m_stats.totalVertexCount = frame.totalVertexCount();
    m_stats.entityCount = frame.entityCount();
}

void DefaultRenderBackend::render()
{
}

void DefaultRenderBackend::beginFrame()
{
    m_context.advanceFrame();
    m_stats.frameId = m_context.frameId;
    m_stats.timestamp = std::chrono::steady_clock::now();
}

void DefaultRenderBackend::endFrame()
{
    m_context.clearDirty();
}

// ============================================================================
// 视口控制
// ============================================================================

void DefaultRenderBackend::resize(const Size2D& size)
{
    m_context.viewportSize = size;
    m_context.markDirty();
}

void DefaultRenderBackend::resetView()
{
    m_context.markDirty();
}

// ============================================================================
// 模式切换
// ============================================================================

void DefaultRenderBackend::setOrbitMode(bool enabled)
{
    m_context.orbitMode = enabled;
}

void DefaultRenderBackend::setMeasureMode(bool enabled)
{
    m_context.measureMode = enabled;
}

void DefaultRenderBackend::setRenderMode(RenderMode mode)
{
    m_context.renderMode = mode;
    m_context.markDirty();
}

RenderMode DefaultRenderBackend::renderMode() const
{
    return m_context.renderMode;
}

// ============================================================================
// 帧输出
// ============================================================================

ImageBuffer DefaultRenderBackend::captureFrame() const
{
    ImageBuffer result;
    result.width = m_context.viewportSize.width;
    result.height = m_context.viewportSize.height;
    result.channels = 4;
    result.data.resize(result.width * result.height * 4);
    return result;
}

RenderStatistics DefaultRenderBackend::getStatistics() const
{
    return m_stats;
}

// ============================================================================
// 后端信息
// ============================================================================

std::string DefaultRenderBackend::backendName() const
{
    return m_name;
}

bool DefaultRenderBackend::supportsCapability(BackendCapability cap) const
{
    return hasCapability(m_capabilities, cap);
}

BackendCapability DefaultRenderBackend::capabilities() const
{
    return m_capabilities;
}