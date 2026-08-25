#pragma once

/**
 * @file UiStateBridge2D.h
 * @brief 2D 命令 UI 状态桥接器
 *
 * 与 3D 的 UiStateBridge3D 对等：把"状态变化 → 命令 UI 刷新"的连线集中到一处，
 * 所有触发源（选择变化 / 图层锁变化 / 场景变化含图元锁 / 撤销栈 / 操作完成）
 * 统一收敛到 Workbench2D::refreshCommandUiState()。
 *
 * 收口的意义：此前这些 connect 散落在 UiWorkbench.cpp 的两个函数里，
 * 漏接一路就表现为"状态变了但按钮没变灰"（如场景树锁定图元后 Delete/Align 仍可点）。
 */

class Workbench2D;
class RenderViewport2D;
class OperationBus;
class QtLayerManagerBridge;
class SceneMonitor;

class UiStateBridge2D
{
public:
    /**
     * @brief 安装全部触发源连线，并立即刷新一次
     *
     * 各参数允许为 nullptr（对应 UI/服务可选或尚未就绪），逐一探测后连接。
     */
    static void install(Workbench2D* workbench,
        RenderViewport2D* viewport,
        OperationBus* bus,
        QtLayerManagerBridge* layerBridge,
        SceneMonitor* sceneMonitor);

    /// 立即刷新命令 UI 状态（工具栏 / 右键菜单 / 面板 / 状态栏）
    static void refreshAll(Workbench2D* workbench);
};
