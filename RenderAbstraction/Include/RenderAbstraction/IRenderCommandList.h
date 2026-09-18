#pragma once
#include "IRenderTypes.h"

namespace RenderAbstraction {

class IRenderCommandList {
public:
    virtual ~IRenderCommandList() = default;

    virtual void submit(const DrawInstruction& cmd) = 0;
    virtual void submitBatch(const DrawInstruction* cmds, uint32_t count) = 0;

    virtual void setViewMatrix(const Matrix4x4& view) = 0;
    virtual void setProjectionMatrix(const Matrix4x4& proj) = 0;
    virtual void setModelMatrix(const Matrix4x4& model) = 0;

    virtual void setLighting(const LightingDesc& lighting) = 0;
    virtual void setClearColor(const ColorF& color) = 0;
    virtual void setEnableCulling(bool enable) = 0;

    virtual void enableDepthTest(bool enable) = 0;
    virtual void enableBlend(bool enable) = 0;

    virtual FrameStatistics getStatistics() const = 0;
};

} // namespace RenderAbstraction
