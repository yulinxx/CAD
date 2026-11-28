#pragma once

class OperationBus;

namespace Apr
{
    class AprilWorkbench;

    // Customer-owned bridge between the 2D command hub and the active viewport.
    // Registers Tool_* operations from CommandCatalog on OperationBus; each operation
    // forwards the tool name to the viewport currently produced by AprilWorkbench.
    // No legacy Workbench2D / RenderViewport2D headers are needed here.
    class DrawToolSwitchRegistry final
    {
    public:
        void setBus(OperationBus* bus) { m_bus = bus; }
        void setWorkbench(AprilWorkbench* workbench) { m_workbench = workbench; }

        void registerAll();

    private:
        OperationBus* m_bus = nullptr;
        AprilWorkbench* m_workbench = nullptr;
    };
}  // namespace Apr
