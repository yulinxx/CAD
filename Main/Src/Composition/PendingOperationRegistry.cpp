#include "PendingOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Log/SyLogger.h"

#include <cstddef>

// 注册尚未接入的算法/编辑/视图操作
// 占位策略：注册 LambdaOperation 打印警告，避免菜单/工具栏点击时静默无响应
//
// 分层管理原则：
// - Algorithm 类：接入 AlgorithmApplicationService 后移除
// - Edit 类：接入 GeometryEditService 后移除
// - View 类：接入 ViewController 后移除
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

    // ---- 算法操作占位（待接入 AlgorithmApplicationService）----
    const OperationId algoOps[] = {
        OperationId::Algo_Fill,
        OperationId::Algo_Nesting,
        OperationId::Algo_Offset,
        OperationId::Algo_Array,
        OperationId::Algo_BooleanUnion,
        OperationId::Algo_BooleanIntersection,
        OperationId::Algo_BooleanDifference,
        OperationId::Algo_BooleanXor,
        OperationId::Algo_ReliefEngravingFromImage,
    };

    // ---- 编辑操作占位（待接入 GeometryEditService）----
    const OperationId editOps[] = {
        OperationId::Edit_Trim,
        OperationId::Edit_Extend,
        OperationId::Edit_Align,
        OperationId::Edit_Cut,
        OperationId::Edit_Paste,
        OperationId::Edit_MirrorH,
        OperationId::Edit_MirrorV,
    };

    // ---- 视图操作占位（待接入 ViewController）----
    const OperationId viewOps[] = {
        OperationId::View_ZoomFit,
        OperationId::View_ZoomIn,
        OperationId::View_ZoomOut,
        OperationId::View_ZoomSelection,
        OperationId::View_Pan,
        OperationId::View_Reset,
        OperationId::View_GridVisible,
        OperationId::View_SnapEnabled,
        OperationId::View_OrthoMode,
        OperationId::View_AngleSnap,
        OperationId::View_LayerManager,
        OperationId::View_NewLayer,
        OperationId::View_DeleteLayer,
        OperationId::View_SetDisplayUnit,
    };

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

    registerPlaceholders(algoOps, std::size(algoOps), "Algorithm");
    registerPlaceholders(editOps, std::size(editOps), "Edit");
    registerPlaceholders(viewOps, std::size(viewOps), "View");

    SY_INFOF("[Composition] Total %d placeholder operations registered across all categories", totalRegistered);
}