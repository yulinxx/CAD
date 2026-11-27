#pragma once

#include "ICadViewport.h"

class RenderViewport2D;   // AprilUi；只在 .cpp 里 include

namespace Apr
{
    // 把 AprilUi::RenderViewport2D 藏在 port 后面；UI 不 include 它。
    class RenderViewport2DAdapter final : public ICadViewport
    {
    public:
        explicit RenderViewport2DAdapter(QWidget* parent = nullptr);
        ~RenderViewport2DAdapter() override;

        QWidget* widget() override;
        void resetView() override;
        void zoomToSelection() override;
        void setDrawingEnabled(bool enabled) override;
        void setMeasureMode(bool enabled) override;
        bool setActiveTool(const QString& toolName) override;

        // 算法层注入仍在 customer/adapter 内；UI 不接触。
        RenderViewport2D* raw() const;   // 仅 adapter 内部/测试；不暴露给 UiShell

    private:
        RenderViewport2D* m_vp = nullptr;
    };
}  // namespace Apr
