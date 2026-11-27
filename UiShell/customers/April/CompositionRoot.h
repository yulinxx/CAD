#pragma once

#include <memory>

// 需要完整类型（unique_ptr 成员）
#include "Composition/ApplicationCompositionRoot.h"

namespace Apr
{
    class AprilWorkbench;
    class Commands;

    // #2 组合根接线结果
    struct CompositionRootWiring
    {
        std::unique_ptr<ApplicationCompositionRoot> ownedRoot;  // 我们创建时持有；外部传入/单例为 nullptr
        ApplicationCompositionRoot* root = nullptr;             // 有效 root（ownedRoot 或 existing/instance）
        bool built = false;                                     // root->isBuilt()
        bool wired = false;                                     // bindCompositionRoot + setDispatcher 已调用
    };

    // 对已 build() 的 root 接线：
    //   AprilWorkbench::bindCompositionRoot(root)
    //   Commands::setDispatcher( root.shellHost()->mainWindow()->currentWorkbench() )   // UiWorkbench : IUiCommandDispatcher
    bool wireCompositionRoot(ApplicationCompositionRoot& root,
                             AprilWorkbench& workbench,
                             Commands& commands);

    // #2 全链路：AprilShell -> ApplicationCompositionRoot::build() -> bindCompositionRoot + setDispatcher。
    //   existing 非空用它；否则优先 ApplicationCompositionRoot::instance()；都没有则自建一个。
    //   若 root 尚未 build()，尝试 root->build()（需 UiShellHost::mainWindow() 非空 + AppInitializer/PersistenceService 已就绪）。
    //   build()/接线失败不抛，返回 built/wired=false，调用方自行决定。
    CompositionRootWiring attachCompositionRoot(AprilWorkbench& workbench,
                                                Commands& commands,
                                                ApplicationCompositionRoot* existing = nullptr);
}  // namespace Apr
