#pragma once

#include "ICadViewport.h"

class Viewport3D;   // AprilUi；只在 .cpp 里 include

namespace Apr
{
    // 把 AprilUi::Viewport3D 藏在 port 后面；UI 不 include 它。
    class Viewport3DAdapter final : public ICadViewport
    {
    public:
        explicit Viewport3DAdapter(QWidget* parent = nullptr);
        ~Viewport3DAdapter() override;

        QWidget* widget() override;
        void resetView() override;        // Viewport3D::resetCamera
        void zoomToSelection() override;  // Viewport3D::fitAll
        void setDrawingEnabled(bool enabled) override;
        void setMeasureMode(bool enabled) override;
        bool setActiveTool(const QString& toolName) override;

        Viewport3D* raw() const;

    private:
        Viewport3D* m_vp = nullptr;
    };
}  // namespace Apr
