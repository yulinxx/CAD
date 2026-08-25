/**
 * @file UiStateBridge2D.cpp
 * @brief 2D 命令 UI 状态桥接器实现
 */
#include "UiStateBridge2D.h"

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

void UiStateBridge2D::install(Workbench2D* workbench,
    RenderViewport2D* viewport,
    OperationBus* bus,
    QtLayerManagerBridge* layerBridge,
    SceneMonitor* sceneMonitor)
{
    if (!workbench)
    {
        return;
    }

    // 选择变化（点选/框选/绘制后自动选中/撤销等所有路径）
    if (viewport)
    {
        QObject::connect(viewport, &RenderViewport2D::selectionChanged, workbench, [workbench]() {
            refreshAll(workbench);
        });
    }

    // 图层锁定/属性变更（锁定图层后其中图元的 Delete/Mirror/Align/Group 应变灰）
    if (layerBridge)
    {
        QObject::connect(layerBridge, &QtLayerManagerBridge::sigLayerChanged, workbench, [workbench](int) {
            refreshAll(workbench);
        });
    }

    if (bus)
    {
        // 撤销/重做栈变化（含经 LayerEditService 直接入栈的图层操作）
        QObject::connect(bus, &OperationBus::undoStateChanged, workbench, [workbench]() {
            refreshAll(workbench);
        });

        // 任意操作成功完成后刷新一次：替代原先仅监听 Edit_Copy / Edit_Cut 的硬编码白名单，
        // 新增写剪贴板或改变选择的操作无需再回来改这里。
        QObject::connect(bus, &OperationBus::operationCompleted, workbench, [workbench](OperationId, bool success) {
            if (success)
            {
                refreshAll(workbench);
            }
        });
    }

    // 场景变化：图元级 setLocked / setVisible 等只发 notifySceneChanged、不改图元数量、
    // 也不经操作总线的变更走这一路。3D 侧（UiStateBridge3D 订阅 SceneMonitor3D::sceneChanged）
    // 早已覆盖，2D 此前缺失 —— 场景树锁定当前选中图元后 Delete/Align 仍可点即源于此。
    // 延后到事件循环下一轮，确保引擎侧批量变更已全部落地。
    if (sceneMonitor)
    {
        QObject::connect(sceneMonitor, &SceneMonitor::sceneChanged, workbench, [workbench]() {
            QTimer::singleShot(0, workbench, [workbench]() {
                refreshAll(workbench);
            });
        });
    }

    refreshAll(workbench);
}
