#include "PendingOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Log/SyLogger.h"

#include <cstddef>

// 占位操作注册器 — 为尚未接入的操作注册占位 LambdaOperation（打印 WARN，避免菜单/工具栏点击时静默无响应）。
// 2026-08-14 起：编辑/算法/视图操作已全部接入（CoreOperationRegistry / AlgorithmRunner / ViewportActionHub），
// 当前已无占位操作；本注册器保留空实现，后续新增未接入操作时在此追加占位数组即可。
PendingOperationRegistry::PendingOperationRegistry(OperationBus* bus)
    : m_bus(bus)
{
}

void PendingOperationRegistry::registerAll()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();
    int totalRegistered = 0;

    auto registerPlaceholders = [&reg, &totalRegistered](const OperationId* ops, size_t count, const char* category) {
        int registered = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (!reg.has(ops[i]))
            {
                reg.registerOperation(std::make_unique<LambdaOperation>(ops[i], [opId = ops[i], category] {
                    SY_WARNF("[PendingOp] %s: OperationId=%d not yet implemented", category, static_cast<int>(opId));
                }));
                ++registered;
            }
        }
        if (registered > 0)
        {
            SY_INFOF("[Composition] Registered %d placeholder operations for %s", registered, category);
            totalRegistered += registered;
        }
    };

    // 当前无待占位操作；后续新增未接入操作时在此追加数组并调用 registerPlaceholders
    SY_INFOF("[Composition] Total %d placeholder operations registered across all categories", totalRegistered);
}