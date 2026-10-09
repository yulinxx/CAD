/**
 * @file UiStateBridge2D.cpp
 * @brief 2D 命令 UI 状态桥接器实现
 */
#include "UiStateBridge2D.h"

#include "Workbench2D.h"
#include "UiWorkbench.h"

#include "RenderViewport2D.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Service/SceneMonitor.h"

#include <QObject>
#include <QTimer>

void UiStateBridge2D::refreshAll(Workbench2D* workbench)
{
    if (workbench)
    {
        workbench->refreshCommandUiState();
    }
}

QObject* UiStateBridge2D::install(Workbench2D* workbench,
    RenderViewport2D* viewport,
    OperationBus* bus,
    QtLayerManagerBridge* layerBridge,
    SceneMonitor* sceneMonitor)
{
    if (!workbench)
    {
        return nullptr;
    }

    // 本次安装的生命周期句柄：所有连线都以它为 context object，销毁它即整批断开。
    // 不能用 workbench 自己当 context —— 它跨工作台长寿，而 bus / layerBridge 同样长寿，
    // 两端都不死的连线永远不会被 Qt 自动回收（见头文件「连线的寿命」）。
    auto* guard = new QObject(workbench);
    guard->setObjectName(QStringLiteral("UiStateBridge2DConnections"));

    // 合并触发：一次操作成功常在同一轮事件循环里连发 undoStateChanged +
    // operationCompleted（+ 场景变化）信号，各自直连会做 N 轮完整快照与
    // 菜单/工具栏遍历。这里统一收敛到一个 间隔 0 的单发定时器：
    // 同轮只排一次尾随刷新，执行时取最新状态（晚到的状态不会丢）。
    auto* coalescer = new QTimer(guard);
    coalescer->setSingleShot(true);
    coalescer->setInterval(0);
    const auto requestRefresh = [coalescer]() {
        if (!coalescer->isActive())
        {
            coalescer->start();
        }
    };
    QObject::connect(coalescer, &QTimer::timeout, guard, [workbench, guard]() {
        refreshAll(workbench);
        // 场景变化附带的场景树刷新（结构签名判定，纯几何变更不会重建树）
        const bool treePending = guard->property("pendingSceneTreeRefresh").toBool();
        guard->setProperty("pendingSceneTreeRefresh", false);
        if (treePending)
        {
            workbench->refreshSceneTreeIfNeeded("sceneMonitor");
        }
    });

    // 选择变化（点选/框选/绘制后自动选中/撤销等所有路径）
    if (viewport)
    {
        QObject::connect(viewport, &RenderViewport2D::selectionChanged, guard, [requestRefresh]() {
            requestRefresh();
        });
    }

    // 图层锁定/属性变更（锁定图层后其中图元的 Delete/Mirror/Align/Group 应变灰）
    if (layerBridge)
    {
        QObject::connect(layerBridge, &QtLayerManagerBridge::sigLayerChanged, guard, [requestRefresh](int) {
            requestRefresh();
        });
    }

    // 图层顺序 → 绘制次序（z-order）：换序只改 LayerManager 的图层顺序，图元本身没变，
    // 因此增量刷新不会重算 sortKey。必须显式走一次全量装配，否则画面上的重叠关系
    // 不会跟着图层顺序变（见 RenderSceneBuilder 的 sortKey 组装）。
    // 注意这一路是渲染侧直连，不并入 UI 状态合并器（两者刷新对象不同）。
    if (layerBridge && viewport)
    {
        QObject::connect(layerBridge, &QtLayerManagerBridge::sigLayerOrderChanged, guard, [viewport]() {
            viewport->requestFullRefresh();
        });
    }

    if (bus)
    {
        // 撤销/重做栈变化（含经 LayerEditService 直接入栈的图层操作）
        QObject::connect(bus, &OperationBus::undoStateChanged, guard, [requestRefresh]() {
            requestRefresh();
        });

        // 任意操作成功完成后刷新一次：替代原来仅监听特定操作的白名单，
        // 新增写剪贴板或改变选择的操作无需再回来改这里
        QObject::connect(bus, &OperationBus::operationCompleted, guard,
            [requestRefresh](OperationId, bool success) {
                if (success)
                {
                    requestRefresh();
                }
            });
    }

    // 场景变化：图元级 setLocked / setVisible 等只发 notifySceneChanged、不改图元数量、
    // 也不经操作总线的变更走这一路。3D 侧（UiStateBridge3D 订阅 SceneMonitor3D::sceneChanged）
    // 早已覆盖，2D 此前缺失 —— 场景树锁定当前选中图元后 Delete/Align 仍可点即源于此。
    if (sceneMonitor)
    {
        QObject::connect(sceneMonitor, &SceneMonitor::sceneChanged, guard, [guard, requestRefresh]() {
            guard->setProperty("pendingSceneTreeRefresh", true);
            requestRefresh();
        });
    }

    refreshAll(workbench);
    return guard;
}