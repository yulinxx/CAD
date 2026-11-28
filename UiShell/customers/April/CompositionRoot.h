#pragma once

#include <memory>

// 需要完整类型（unique_ptr 成员）
#include "Composition/ApplicationCompositionRoot.h"
#include "ports/CompositionRootAdapter.h"

namespace Apr
{
    class AprilWorkbench;
    class Commands;
    class DrawToolSwitchRegistry;
    class ICommandSink;
    class OperationCommandSink;

    // #2 组合根接线结果
    struct CompositionRootWiring
    {
        std::unique_ptr<ApplicationCompositionRoot> ownedRoot;  // 我们创建时持有；外部传入/单例为 nullptr
        ApplicationCompositionRoot* root = nullptr;             // 有效 root（ownedRoot 或 existing/instance）

        // 关键：adapter 及其 viewportFactory 必须长期存活。
        // 早期版本在 wireCompositionRoot() 栈上构造 adapter，函数返回后
        // AprilWorkbench 持有的是悬空指针（AprilShell 启动即崩）。
        std::unique_ptr<CompositionRootAdapter> adapter;

        bool built = false;                                     // root->isBuilt()
        bool wired = false;                                     // bindViewportServices + setSink 已调用
    };

    // 对已 build() 的 root 接线：视口服务 + 客户侧 OperationCommandSink。
    // 不再读取 WorkbenchWindow/UiWorkbench（M6 起它们只存在于测试专用目录）。
    bool wireCompositionRoot(ApplicationCompositionRoot& root,
                             AprilWorkbench& workbench,
                             Commands& commands,
                             CompositionRootAdapter& adapter,
                             OperationCommandSink& sink,
                             DrawToolSwitchRegistry& toolSwitchRegistry);

    // 低耦合：UiShell 不知道 Main。customers/April 自己提供命令出口并注入 Commands。
    // 传 nullptr 可清空。不要求 ApplicationCompositionRoot::instance()->isBuilt()。
    void attachCommandSink(Commands& commands, ICommandSink* sink);

    // #2 全链路：April/AprilShell -> ApplicationCompositionRoot::build() -> bindViewportServices + setSink。
    //   existing 非空用它；否则优先 ApplicationCompositionRoot::instance()；都没有则自建一个。
    //   若 root 尚未 build()，尝试 root->build()（需 UiShell 已发布宿主主窗口；持久化服务允许为空）。
    //   build()/接线失败不抛，返回 built/wired=false，调用方自行决定。
    bool attachCompositionRoot(AprilWorkbench& workbench,
                               Commands& commands,
                               CompositionRootWiring& wiring,
                               OperationCommandSink& sink,
                               DrawToolSwitchRegistry& toolSwitchRegistry,
                               ApplicationCompositionRoot* existing = nullptr);
}  // namespace Apr
