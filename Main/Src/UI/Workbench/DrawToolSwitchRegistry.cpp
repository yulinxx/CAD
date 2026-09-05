#include "DrawToolSwitchRegistry.h"

#include "RenderViewport2D.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/IOperation.h"

#include "Log/SyLogger.h"

#include <memory>

DrawToolSwitchRegistry::DrawToolSwitchRegistry(OperationBus* bus, RenderViewport2D** viewportPtr)
    : m_bus(bus)
    , m_viewportPtr(viewportPtr)
{
}

void DrawToolSwitchRegistry::registerAll()
{
    if (!m_bus || !m_viewportPtr || !*m_viewportPtr)
    {
        SY_WARNF("[DrawToolSwitchRegistry] skip registration: bus=%s viewport=%s",
            m_bus ? "ok" : "null",
            (m_viewportPtr && *m_viewportPtr) ? "ok" : "null");
        return;
    }

    OperationRegistry& registry = m_bus->registry();
    int registered = 0;

    // 以 CommandCatalog 为唯一数据源：工具栏显示哪些工具，就注册哪些激活操作，
    // 避免手工维护第二份 operationId → toolName 映射表。
    for (const CommandEntry2D& entry : CommandCatalog::commands())
    {
        if (!hasSurface(entry.surfaces, CommandSurface2DValues::LeftToolbar))
        {
            continue;
        }
        if (entry.operationId == OperationId::None || !entry.toolName || !*entry.toolName)
        {
            continue;
        }

        const QString toolName = QString::fromUtf8(entry.toolName);

        // LambdaOperation 是框架推荐的依赖注入方式（见 OperationBus.cpp 头注释）：
        // 捕获指向 m_viewport 的间接引用，而非视口裸指针本身。
        // OperationRegistry 跳过重复注册，因此首次注册的 lambda 会贯穿整个应用生命周期；
        // 间接引用确保工作台切换后 lambda 始终访问当前视口（或安全跳过 null）。
        registry.registerOperation(
            std::make_unique<LambdaOperation>(entry.operationId, [vpPtr = m_viewportPtr, toolName]() {
                if (vpPtr && *vpPtr)
                {
                    (*vpPtr)->setActiveTool(toolName);
                }
            }));

        ++registered;
    }

    SY_DEBUGF("[DrawToolSwitchRegistry] registered %d draw tool switch operations", registered);
}