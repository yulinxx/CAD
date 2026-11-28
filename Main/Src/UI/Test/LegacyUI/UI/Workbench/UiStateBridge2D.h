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
 *
 * ## 连线的寿命
 *
 * 触发源分两类：viewport / sceneMonitor 随每次 attach 重建并销毁，连线随之自动断开；
 * 而 bus / layerBridge 来自 UiServices，**跨工作台长寿**，接收方 Workbench2D 也长寿，
 * 两端都不死 → 连线永不断。往返切换 N 次就累积 N 份，一次 undoStateChanged 触发
 * N 次 refreshAll（幂等，但随使用时长线性增长）。
 *
 * 因此 install 返回一个**本次安装的生命周期句柄**：全部连线都以它为 context object，
 * 销毁它即整批断开。调用方只需持有一个指针，不需要知道 install 内部连了几路——
 * 否则每新增一路触发源，卸载侧都要跟着改一次，漏一路就是又一次静默累积。
 */

class Workbench2D;
class RenderViewport2D;
class OperationBus;
class QtLayerManagerBridge;
class SceneMonitor;
class QObject;

class UiStateBridge2D
{
public:
    /**
     * @brief 安装全部触发源连线，并立即刷新一次
     *
     * 各参数允许为 nullptr（对应 UI/服务可选或尚未就绪），逐一探测后连接。
     *
     * @return 本次安装的生命周期句柄（parent 为 workbench）。卸载时销毁它即断开
     *         全部连线；不销毁则长寿触发源上的连线会跨 attach 累积（见文件头说明）。
     */
    [[nodiscard]] static QObject* install(Workbench2D* workbench,
        RenderViewport2D* viewport,
        OperationBus* bus,
        QtLayerManagerBridge* layerBridge,
        SceneMonitor* sceneMonitor);

    /// 立即刷新命令 UI 状态（工具栏 / 右键菜单 / 面板 / 状态栏）
    static void refreshAll(Workbench2D* workbench);
};
