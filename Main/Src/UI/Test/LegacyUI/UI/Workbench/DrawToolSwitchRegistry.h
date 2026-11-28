#pragma once

/**
 * @file DrawToolSwitchRegistry.h
 * @brief 2D 绘图工具栏 → OperationBus → 视口的工具激活注册器
 *
 * 职责：把 CommandCatalog 中标记为 LeftToolbar 的 Tool_* 操作注册到
 * OperationBus。注册的操作统一转发到 RenderViewport2D::setActiveTool()，
 * 从而接通「工具栏按钮 → 操作总线 → 视口工具」这条框架规定的路由
 * （见 WorkbenchWindow.cpp 头注释：UI → OperationBus → 交互式命令）。
 *
 * 为什么放在 Main 层：
 *   - 激活目标（RenderViewport2D）由 Main 工作台在运行时创建，
 *     组合根注册操作时视口尚不存在；
 *   - 因此本注册器在 Workbench2D::createToolbars() 中、视口就绪后调用。
 *
 * 与 Core/File/Pending 注册器遵循同一模式：构造注入依赖，registerAll() 统一注册。
 * 生命周期安全：操作通过 LambdaOperation 捕获指向视口指针的间接引用
 * (RenderViewport2D**)，指向 Workbench2D::m_viewport 成员。
 * 工作台切换时 m_viewport 被置空，lambda 通过间接引用安全跳过；
 * 重新激活后 m_viewport 指向新视口，lambda 自动获得新指针。
 * 由于 OperationRegistry 跳过重复注册，间接引用确保旧 lambda 始终使用当前视口。
 */

class OperationBus;
class RenderViewport2D;

class DrawToolSwitchRegistry
{
public:
    DrawToolSwitchRegistry(OperationBus* bus, RenderViewport2D** viewportPtr);

public:
    /// 注册所有出现在左侧绘图工具栏上的工具激活操作
    void registerAll();

private:
    OperationBus* m_bus{ nullptr };
    RenderViewport2D** m_viewportPtr{ nullptr };
};
