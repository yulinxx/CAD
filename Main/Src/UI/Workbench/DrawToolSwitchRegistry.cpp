#include "DrawToolSwitchRegistry.h"

#include "RenderViewport2D.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/IOperation.h"

#include "Log/SyLogger.h"

#include <memory>

DrawToolSwitchRegistry::DrawToolSwitchRegistry(OperationBus* bus, RenderViewport2D* viewport)
    : m_bus(bus)
    , m_viewport(viewport)
{
}

void DrawToolSwitchRegistry::registerAll()
{
    if (!m_bus || !m_viewport)
    {
        SY_WARNF("[DrawToolSwitchRegistry] skip registration: bus=%s viewport=%s",
            m_bus ? "ok" : "null",
            m_viewport ? "ok" : "null");
        return;
    }

    OperationRegistry& registry = m_bus->registry();
    int registered = 0;

    // 以 CommandCatalog 为唯一数据源：工具栏显示哪些工具，就注册哪些激活操作，
    // 避免手工维护第二份 operationId → toolName 映射表。
    for (const CommandEntry2D& entry : CommandCatalog::commands())
    {
        if (!hasSurface(entry.surfaces, CommandSurface2D::LeftToolbar))
            continue;
        if (entry.operationId == OperationId::None || !entry.toolName || !*entry.toolName)
            continue;

        const QString toolName = QString::fromUtf8(entry.toolName);

        // LambdaOperation 是框架推荐的依赖注入方式（见 OperationBus.cpp 头注释）：
        // 直接捕获视口，不依赖已废弃的 OperationContext。
        registry.registerOperation(std::make_unique<LambdaOperation>(
            entry.operationId,
            [viewport = m_viewport, toolName]() {
                if (viewport)
                    viewport->setActiveTool(toolName);
            }));

        ++registered;
    }

    SY_INFOF("[DrawToolSwitchRegistry] registered %d draw tool switch operations", registered);
}
