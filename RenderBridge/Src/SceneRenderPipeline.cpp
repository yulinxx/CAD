#include "RenderBridge/SceneRenderPipeline.h"

#include "RenderBridge/GeometryPipeline.h"
#include "RenderBridge/PersistentGeometryStore.h"

#include "RenderAbstraction/IRenderDevice.h"
#include "RenderAbstraction/IRenderScene.h"
#include "RenderAbstraction/IRenderTypes.h"

#include <cstring>

namespace RenderBridge
{
    bool SceneRenderPipeline::initialize(RenderAbstraction::IRenderDevice& device,
                                            RenderAbstraction::IRenderScene& scene)
    {
        shutdown();
        m_device = &device;
        m_scene = &scene;
        return true;
    }

    void SceneRenderPipeline::shutdown()
    {
        m_device = nullptr;
        m_scene = nullptr;
        m_transientBuffer.clear();
        m_overlayBuffer.clear();
        m_geometryDrawList = {};
        m_frameStarted = false;
    }

    bool SceneRenderPipeline::beginFrame()
    {
        if (!m_scene)
        {
            return false;
        }
        m_frameStarted = true;
        return m_scene->beginFrame();
    }

    void SceneRenderPipeline::endFrame()
    {
        if (!m_frameStarted || !m_scene)
        {
            return;
        }
        // 提交瞬态命令
        if (!m_transientBuffer.empty())
        {
            RenderAbstraction::DrawPacket packet{};
            packet.commands = m_transientBuffer.data();
            packet.commandCount = static_cast<uint32_t>(m_transientBuffer.size());
            packet.enableCulling = false;
            m_scene->submit(packet);
            m_transientBuffer.clear();
        }
        // 提交覆盖层命令
        if (!m_overlayBuffer.empty())
        {
            RenderAbstraction::DrawPacket packet{};
            packet.commands = m_overlayBuffer.data();
            packet.commandCount = static_cast<uint32_t>(m_overlayBuffer.size());
            packet.enableCulling = false;
            m_scene->submit(packet);
            m_overlayBuffer.clear();
        }
        m_scene->endFrame();
        m_frameStarted = false;
    }

    void SceneRenderPipeline::setModelMatrix(const RenderAbstraction::Matrix4x4* matrix)
    {
        if (m_scene)
        {
            m_scene->setModelMatrix(matrix);
        }
    }

    void SceneRenderPipeline::setCamera(const RenderAbstraction::CameraDesc& camera)
    {
        if (m_scene)
        {
            m_scene->setCamera(camera);
        }
    }

    void SceneRenderPipeline::setLighting(const RenderAbstraction::LightingDesc& lighting)
    {
        if (m_scene)
        {
            m_scene->setLighting(lighting);
        }
    }

    void SceneRenderPipeline::submitGeometry(RenderAbstraction::DrawListHandle drawList,
                                                const RenderAbstraction::ViewVolume* view)
    {
        m_geometryDrawList = drawList;
        if (view)
        {
            m_scene->submitDrawList(drawList, view);
        }
    }

    void SceneRenderPipeline::submitTransient(
        const std::vector<RenderAbstraction::DrawInstruction>& commands)
    {
        m_transientBuffer.insert(m_transientBuffer.end(), commands.begin(), commands.end());
    }

    void SceneRenderPipeline::submitOverlay(const OverlayScene& overlay)
    {
        overlay.submit(*m_scene, m_overlayBuffer);
    }

    void SceneRenderPipeline::submitOverlayCommands(
        const std::vector<RenderAbstraction::DrawInstruction>& commands)
    {
        m_overlayBuffer.insert(m_overlayBuffer.end(), commands.begin(), commands.end());
    }

    RenderAbstraction::TextureHandle SceneRenderPipeline::createRenderTarget(
        uint32_t width, uint32_t height)
    {
        if (!m_scene)
        {
            return {};
        }
        return m_scene->createRenderTarget(width, height);
    }

    bool SceneRenderPipeline::setRenderTarget(const RenderAbstraction::RenderTargetBinding& target)
    {
        if (!m_scene)
        {
            return false;
        }
        return m_scene->setRenderTarget(target);
    }

    bool SceneRenderPipeline::readPixelsFromTexture(RenderAbstraction::TextureHandle texture,
                                                        uint32_t x, uint32_t y,
                                                        uint32_t width, uint32_t height,
                                                        void* outPixels, uint64_t capacity)
    {
        if (!m_scene || !outPixels || capacity < static_cast<uint64_t>(width * height * 4))
        {
            return false;
        }
        return m_scene->readPixelsFromTexture(texture, x, y, width, height, outPixels, capacity);
    }

    RenderAbstraction::FrameStatistics SceneRenderPipeline::getFrameStatistics() const
    {
        if (m_scene)
        {
            return m_scene->getFrameStatistics();
        }
        return {};
    }
}
