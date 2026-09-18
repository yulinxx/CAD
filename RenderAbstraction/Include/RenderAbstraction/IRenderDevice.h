#pragma once
#include "IRenderTypes.h"

namespace RenderAbstraction {

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    virtual const char* backendName() const = 0;
    virtual float maxLineWidth() const = 0;
    virtual int maxTextureSize() const = 0;

    virtual BufferHandle createBuffer(uint64_t sizeBytes, bool cpuWritable) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;
    virtual void uploadBuffer(BufferHandle buffer, uint64_t offset, uint64_t sizeBytes, const void* data) = 0;

    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    virtual void updateTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) = 0;

    virtual uint32_t getDefaultPipeline(VertexFormat format, RenderSpace space, PrimitiveType topology) = 0;
    virtual uint32_t createPipeline(const PipelineDesc& desc) = 0;
};

} // namespace RenderAbstraction
