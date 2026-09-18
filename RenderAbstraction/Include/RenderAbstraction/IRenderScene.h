#pragma once
#include "IRenderTypes.h"
#include <memory>
#include <vector>

namespace RenderAbstraction {

class IRenderScene {
public:
    virtual ~IRenderScene() = default;

    virtual bool initialize(IRenderDevice& device) = 0;

    virtual CommandListHandle createCommandList(uint32_t initialCapacity = 4096) = 0;
    virtual void destroyCommandList(CommandListHandle list) = 0;
    virtual void clearCommandList(CommandListHandle list) = 0;

    virtual BufferHandle uploadGeometry(const void* vertices, uint64_t sizeBytes, bool persistent = true) = 0;
    virtual void uploadGeometryBatch(const void* const* vertexArrays, const uint64_t* sizeBytes, int count, bool persistent = true) = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual void submitToScreen(CommandListHandle list) = 0;
    virtual void submitToSurface(CommandListHandle list, SurfaceHandle surface) = 0;

    virtual TextureHandle createRenderTarget(uint32_t width, uint32_t height) = 0;
    virtual void readPixels(TextureHandle target, void* outPixels, uint32_t width, uint32_t height) = 0;

    virtual void setCamera(const CameraDesc& camera) = 0;
    virtual void setLighting(const LightingDesc& lighting) = 0;

    virtual FrameStatistics getFrameStatistics() const = 0;
    virtual bool isValid() const = 0;
};

} // namespace RenderAbstraction
