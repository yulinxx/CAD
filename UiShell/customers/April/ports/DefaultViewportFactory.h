#pragma once

#include "ICadViewport.h"

namespace Apr
{
    // 默认 2D/3D viewport factory：UI 只拿 Apr::ICadViewport。
    //   create2D -> Apr::RenderViewport2DAdapter (AprilUi::RenderViewport2D 藏后)
    //   create3D -> Apr::Viewport3DAdapter       (AprilUi::Viewport3D + RenderWidget3DAdapter 藏后)
    // 算法层注入留 adapter；UiShell 不知道 RenderViewport2D/Viewport3D。
    class DefaultViewportFactory final : public IViewportFactory
    {
    public:
        ICadViewport* create2D(QWidget* parent) override;
        ICadViewport* create3D(QWidget* parent) override;
    };
}  // namespace Apr
