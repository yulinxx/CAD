#include "AlgorithmOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

#include "Operation/ReliefEngravingOperation2D.h"

AlgorithmOperationRegistry::AlgorithmOperationRegistry(OperationBus* bus, AlgorithmRunner* algorithmRunner, QWidget* parentWidget)
    : m_bus(bus)
    , m_algorithmRunner(algorithmRunner)
    , m_parentWidget(parentWidget)
{
}

void AlgorithmOperationRegistry::registerAll()
{
    if (!m_bus || !m_algorithmRunner)
        return;

    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();

    const auto registerAlgoOp = [&reg](OperationId id) {
        reg.registerOperation(std::make_unique<ParamLambdaOperation>(id, [id](const QVariantMap& params) {
            // 由 AlgorithmRunner 统一调度，AlgorithmRunner 通过 OperationId 路由到具体算法任务
            // 具体实现在 AlgorithmRunner::runForOperation 中
        }));
    };

    registerAlgoOp(OperationId::Algo_Fill);
    registerAlgoOp(OperationId::Algo_FillColor);
#ifdef ENABLE_NESTING
    registerAlgoOp(OperationId::Algo_Nesting);
#endif
    registerAlgoOp(OperationId::Algo_Offset);
    registerAlgoOp(OperationId::Algo_Array);
    registerAlgoOp(OperationId::Algo_BooleanUnion);
    registerAlgoOp(OperationId::Algo_BooleanIntersection);
    registerAlgoOp(OperationId::Algo_BooleanDifference);
    registerAlgoOp(OperationId::Algo_BooleanXor);

    reg.registerOperation(
        std::make_unique<LambdaOperation>(OperationId::Algo_ReliefEngravingFromImage, [parentWidget = m_parentWidget] {
            ReliefEngravingOperation2D::run(parentWidget);
        }));
}
