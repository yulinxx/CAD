/**
 * @file AlgorithmOperationRegistry.cpp
 * @brief 算法操作注册实现
 *
 * 注册和管理算法相关的操作命令。
 */
#include "AlgorithmOperationRegistry.h"
#include "OpRegistryTypes.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "UI2D/Operation/AlgorithmRunner.h"

#include "Operation/ReliefEngravingOperation2D.h"
#ifdef BUILD_RASTER
#include "Operation/RasterProcessOperation2D.h"
#endif

AlgorithmOperationRegistry::AlgorithmOperationRegistry(
    OperationBus* bus, AlgorithmRunner* algorithmRunner, SceneEditService* sceneEdit, QWidget* parentWidget)
    : m_bus(bus)
    , m_algorithmRunner(algorithmRunner)
    , m_sceneEdit(sceneEdit)
    , m_parentWidget(parentWidget)
{
}

void AlgorithmOperationRegistry::registerAll()
{
    if (!m_bus || !m_algorithmRunner)
        return;

    QWidget* parentWidget = m_parentWidget;
    auto& reg = m_bus->registry();

    AlgorithmRunner* runner = m_algorithmRunner;
    // 算法是异步执行的：OperationBus 在 dispatch 返回时只看到"已受理"，
    // 真实完成后再由 declareParam 注册的闭包链路补发 operationCompleted，
    // 让状态栏/场景树等统一刷新，无需 AlgorithmRunner 手工调用 notifySceneChanged。
    if (runner)
    {
        runner->setCompletionCallback([bus = m_bus](OperationId id, bool success, const QString& message) {
            if (bus)
            {
                bus->notifyOperationCompleted(id, success, message);
            }
        });
    }
    const auto registerAlgoOp = [&reg, runner](OperationId id) {
        Ops::declareParam(reg, id, [id, runner](const QVariantMap& params) {
            if (!runner)
                return;
            OperationRequest req;
            req.id = id;
            req.params = params;
            req.source = OperationSource::Menu;
            runner->runForOperation(id, req);
        });
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
    registerAlgoOp(OperationId::Algo_GenerateTestShapes);

    Ops::declare(reg, OperationId::Algo_ReliefEngravingFromImage, [parentWidget = m_parentWidget] {
        ReliefEngravingOperation2D::run(parentWidget);
    });

#ifdef BUILD_RASTER
    // 位图工艺处理：弹对话框 → Raster 处理 → SyImage 入场景 → Undo 压栈
    if (m_sceneEdit)
    {
        Ops::declare(reg, OperationId::Algo_BitmapProcess,
            [sceneEdit = m_sceneEdit, parentWidget = m_parentWidget] {
                RasterProcessOperation2D::runProcess(sceneEdit, parentWidget);
            });

        Ops::declare(reg, OperationId::Algo_VectorizeBitmap,
            [sceneEdit = m_sceneEdit, parentWidget = m_parentWidget] {
                RasterProcessOperation2D::runVectorize(sceneEdit, parentWidget);
            });
    }
#endif
}