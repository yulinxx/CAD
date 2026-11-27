#include "CompositionRoot.h"

#include "panels/AprilWorkbench.h"
#include "ports/CompositionRootAdapter.h"
#include "commands/Commands.h"

// ---- AprilCore ----
#include "Composition/ApplicationCompositionRoot.h"

// ---- AprilUi ----
#include "UI/Services/UiShellHost.h"          // UiShellHost::mainWindow
#include "UI/Workbench/WorkbenchWindow.h"     // WorkbenchWindow::currentWorkbench
#include "UI/Workbench/UiWorkbench.h"          // UiWorkbench : public IUiCommandDispatcher

namespace Apr
{
    void attachCommandDispatcher(Commands& commands, IUiCommandDispatcher* dispatcher)
    {
        commands.setDispatcher(dispatcher);
    }

    bool wireCompositionRoot(ApplicationCompositionRoot& root,
                             AprilWorkbench& workbench,
                             Commands& commands)
    {
        if (!root.isBuilt())
            return false;

        // 视口服务注入：低耦合，AprilWorkbench 只见 Apr::IViewportServices，不见 ApplicationCompositionRoot 具体对象。
        Apr::CompositionRootAdapter adapter(root);
        workbench.bindViewportServices(adapter);
        workbench.setServiceFactory(&adapter);                 // Apr::ICadServiceFactory
        // 低耦合转发：AprilCore -> Apr::DefaultViewportFactory -> Apr::RenderViewport2DAdapter -> RenderViewport2D；不暴露 raw 给 UiShell。
        if (auto* dvf = dynamic_cast<Apr::DefaultViewportFactory*>(adapter.viewportFactory()))
        {
            dvf->setSceneDocument2D(root.document2D());
            dvf->setOperationBus(root.operationBus());
            dvf->setSelectionService(root.selectionService());
            dvf->setLayerManager(root.layerManager());
        }
        workbench.setViewportFactory(adapter.viewportFactory()); // Apr::IViewportFactory -> Apr::DefaultViewportFactory
        const bool viewportWired = adapter.hasSceneDocument2D() || adapter.hasOperationBus() || adapter.hasUiServices();

        // 命令分发注入：现有 UiWorkbench（: public IUiCommandDispatcher）
        IUiCommandDispatcher* dispatcher = nullptr;
        UiWorkbench* wb = nullptr;
        if (root.shellHost() && root.shellHost()->mainWindow()
            && root.shellHost()->mainWindow()->currentWorkbench())
        {
            wb = root.shellHost()->mainWindow()->currentWorkbench();
        }
        if (wb)
            dispatcher = dynamic_cast<IUiCommandDispatcher*>(wb);

        attachCommandDispatcher(commands, dispatcher);

        return viewportWired;   // 命令 dispatcher 可为 null（未 setWorkbench），不算失败
    }

    CompositionRootWiring attachCompositionRoot(AprilWorkbench& workbench,
                                                Commands& commands,
                                                ApplicationCompositionRoot* existing)
    {
        CompositionRootWiring wiring;

        ApplicationCompositionRoot* root = existing;
        if (!root)
            root = ApplicationCompositionRoot::instance();
        if (!root)
        {
            wiring.ownedRoot = std::make_unique<ApplicationCompositionRoot>();
            root = wiring.ownedRoot.get();
        }

        // 尝试装配（幂等；已 build 直接返回 true）
        if (!root->isBuilt())
            root->build();

        wiring.root = root;
        wiring.built = root->isBuilt();
        if (wiring.built)
            wiring.wired = wireCompositionRoot(*root, workbench, commands);

        return wiring;
    }
}  // namespace Apr
