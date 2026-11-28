#include "CompositionRoot.h"

#include "panels/AprilWorkbench.h"
#include "ports/CompositionRootAdapter.h"
#include "commands/Commands.h"
#include "commands/OperationCommandSink.h"

// ---- AprilCore ----
#include "Composition/ApplicationCompositionRoot.h"

// ---- UiShell ----
#include "UiShell/Assembly/HostWindow.h"

namespace Apr
{
    void attachCommandSink(Commands& commands, ICommandSink* sink)
    {
        commands.setSink(sink);
    }

    bool wireCompositionRoot(ApplicationCompositionRoot& root,
                             AprilWorkbench& workbench,
                             Commands& commands,
                             CompositionRootAdapter& adapter,
                             OperationCommandSink& sink)
    {
        if (!root.isBuilt())
            return false;

        // 视口服务注入：低耦合，AprilWorkbench 只见 Apr::IViewportServices，不见 ApplicationCompositionRoot 具体对象。
        // adapter 由调用方（customer 的模块部件）持有，函数返回后不能留下悬空指针。
        workbench.bindViewportServices(*adapter.viewportServices());
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

        // M6：命令不再经 WorkbenchWindow/UiWorkbench。客户侧 OperationCommandSink
        // 直接把 2D 命令交给 OperationBus，把 theme/language/help 交回 UiShell。
        sink.setBus(root.operationBus());
        sink.setWorkbenchSwitch([&workbench](const QString& commandId) {
            workbench.setViewportKind(commandId == QLatin1String("view.switch_to_3d")
                ? ViewportKind::ThreeD
                : ViewportKind::TwoD);
        });
        adapter.setCommandSink(&sink);
        attachCommandSink(commands, &sink);

        return viewportWired;
    }

    bool attachCompositionRoot(AprilWorkbench& workbench,
                               Commands& commands,
                               CompositionRootWiring& wiring,
                               OperationCommandSink& sink,
                               ApplicationCompositionRoot* existing)
    {
        ApplicationCompositionRoot* root = existing;
        if (!root)
            root = ApplicationCompositionRoot::instance();
        if (!root)
        {
            wiring.ownedRoot = std::make_unique<ApplicationCompositionRoot>();
            root = wiring.ownedRoot.get();
        }

        // M6：主窗口由 UiShell 在 createCustomerModule() 之前发布；组合根只借用指针。
        root->setMainWindow(RD::hostMainWindow());

        // 尝试装配（幂等；已 build 直接返回 true）
        if (!root->isBuilt())
            root->build();

        wiring.root = root;
        wiring.built = root->isBuilt();
        if (!wiring.built)
            return false;

        // adapter 与它内部的 DefaultViewportFactory 必须和 workbench 同寿命：
        // 它们由 customer 的 ModuleParts 持有（wiring 成员），不是本函数栈对象。
        if (!wiring.adapter)
            wiring.adapter = std::make_unique<CompositionRootAdapter>(*root);

        wiring.wired = wireCompositionRoot(*root, workbench, commands, *wiring.adapter, sink);
        return wiring.wired;
    }
}  // namespace Apr
