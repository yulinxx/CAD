#pragma once

#include "AprilWorkbench.h"

namespace Apr
{
    // M2/M3：April 客户的中央视图。
    // 继承 M3 的 AprilWorkbench：把现有 CAD 工作台/视口框架移植到 UiShell。
    // createCenterView 返回现有 RenderViewport2D / Viewport3D 宿主。
    class CenterView : public AprilWorkbench
    {
    };
}  // namespace Apr
