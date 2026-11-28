#include "DrawToolSwitchRegistry.h"

#include <memory>

#include "UI/Command/CommandSurfaceFlags.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/IOperation.h"
#include "UI2D/Operation/OperationBus.h"
#include "panels/AprilWorkbench.h"

namespace Apr
{
    void DrawToolSwitchRegistry::registerAll()
    {
        if (!m_bus || !m_workbench)
            return;

        OperationRegistry& registry = m_bus->registry();

        // CommandCatalog is the single source of truth for the left toolbar.
        // Registering exactly those entries keeps menu, toolbar and shortcut paths
        // on the same OperationBus route as the tool activation itself.
        for (const CommandEntry2D& entry : CommandCatalog::commands())
        {
            if (!Cmd::hasSurface(entry.surfaces, CommandSurface2DValues::LeftToolbar))
                continue;
            if (entry.operationId == OperationId::None || !entry.toolName || !*entry.toolName)
                continue;

            const QString toolName = QString::fromUtf8(entry.toolName);
            registry.registerOperation(std::make_unique<LambdaOperation>(entry.operationId, [workbench = m_workbench, toolName]() {
                // The workbench may currently host a 3D viewport or no viewport at all.
                // Reading the current port at execution time avoids dangling pointers
                // across workbench switches.
                if (ICadViewport* viewport = workbench->cadViewport())
                    viewport->setActiveTool(toolName);
            }));
        }
    }
}  // namespace Apr
