#pragma once

#include "ICadViewport.h"

class RenderViewport2D;   // AprilUi；只在 .cpp 里 include
class SceneDocument2D;    // AprilUi/AprilCore；不暴露 raw 给 UiShell
class OperationBus;       // UI2D/AprilCore
class ISelectionService;  // AprilUi/AprilCore
class LayerManager;       // Engine2D

namespace Apr
{
    // 把 AprilUi::RenderViewport2D 藏在 port 后面；UI 不 include 它。
    class RenderViewport2DAdapter final : public ICadViewport
    {
    public:
        explicit RenderViewport2DAdapter(QWidget* parent = nullptr);
        ~RenderViewport2DAdapter() override;

        QWidget* widget() override;
        void setPositionCallback(PositionCallback callback) override;
        void resetView() override;
        void zoomToSelection() override;
        void setDrawingEnabled(bool enabled) override;
        void setMeasureMode(bool enabled) override;
        bool setActiveTool(const QString& toolName) override;

        // 算法层注入（adapter 内部转发到 RenderViewport2D；UI/UiShell 不接触 raw）。
        void setSceneDocument2D(::SceneDocument2D* document);
        void setOperationBus(::OperationBus* bus);
        void setSelectionService(::ISelectionService* service);
        void setLayerManager(::LayerManager* manager);
        void initializeTools();

        RenderViewport2D* raw() const;   // 仅 adapter 内部/测试；不暴露给 UiShell

    private:
        RenderViewport2D* m_vp = nullptr;
    };
}  // namespace Apr
