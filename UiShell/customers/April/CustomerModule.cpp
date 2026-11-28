#include "UiShell/Contract/CustomerModule.h"

#include "UiShell/Contract/BuildFingerprint.h"

#include <QFile>

// Step 0 基线契约补齐：April 与其他客户一样注册共享默认对话框工厂，
// 让 UiShell 的通用集成测试（每个客户都注册 sample/about 对话框）可运行。
#include "UiDialogs/AboutDialogs.h"
#include "UiDialogs/SampleDialogs.h"

#include "panels/PanelFactory.h"
#include "commands/Commands.h"
#include "commands/OperationCommandSink.h"
#include "CompositionRoot.h"   // CompositionRootWiring：adapter/viewportFactory 的持有者
#include "ports/DrawToolSwitchRegistry.h"  // 左侧工具栏 → OperationBus → 视口

// 静态链接时 qrc 资源对象需要显式引用一次，否则可能被链接器丢弃。
// Q_INIT_RESOURCE 会就地声明资源初始化函数，必须在全局命名空间里调用。
static void UiInitAprilResources()
{
    Q_INIT_RESOURCE(UiAprilProfile);  // 内嵌 profile（ui.json），见 cmake/UiCustomerProfile.cmake
}

namespace Apr
{
    namespace
    {
        // 客户显示名：固定为产品名，不翻译。
        QString customerDisplayName()
        {
            return QStringLiteral("April");
        }

        // 本客户的全部部件：一个堆对象，CustomerModule 里的指针都指向它的成员。
        // 宿主经 CustomerModule::owner 接管，在运行期销毁后调用 destroy()；不使用任何全局变量。
        class ModuleParts final : public AA::IApiObject
        {
        public:
            CenterView centerView;
            PanelFactory panels;
            Commands commands;
            OperationCommandSink commandSink;
            // 组合根 + adapter + viewportFactory 必须与 centerView 同寿命：
            // 早期把 adapter 放在栈上，导致 AprilShell 一加载 profile 就悬空崩溃。
            CompositionRootWiring wiring;
            Apr::DrawToolSwitchRegistry toolSwitchRegistry;
            AA::about::AboutDialogFactory aboutDialog;
            AA::samples::SampleParamsDialogFactory sampleParams;

            void destroy() override
            {
                delete this;  // 在客户库内释放，不跨 CRT
            }
        };
    }  // namespace
}  // namespace Apr

namespace AA
{
    const char* customerBuildFingerprint() noexcept
    {
        return UI_BUILD_FINGERPRINT;
    }

    CustomerModule createCustomerModule()
    {
        UiInitAprilResources();

        auto* parts = new Apr::ModuleParts();

        CustomerModule module;
        module.owner = parts;
        module.strId = QStringLiteral("april");
        module.strDisplayName = Apr::customerDisplayName();
        module.displayNameFn = &Apr::customerDisplayName;

        module.strProfilePath = QString::fromUtf8(UI_CUSTOMER_PROFILE);
        module.strStyleSeries = QStringLiteral("april");
        module.centerView = &parts->centerView;
        module.listWidgetFactoriesBind.append({ QStringLiteral("april.panel"), &parts->panels });  // M4：旧 UI 界面部件
        module.cmdCatalog = &parts->commands;    // M4
        module.cmdInvoker = &parts->commands;    // M4
        module.cmdState = &parts->commands;      // M4
        module.cmdProfileRegistrar = &parts->commands;  // 外置 --profile 也能补齐菜单命令

        // M4：从客户自己的 ui.json 登记菜单引用的命令。AssemblyPlanner 只认目录里
        // 已有的命令，少了这一步整个 April 菜单都会被当“未知命令”过滤掉。
        // 读的是内嵌 profile（与外置部署的 ui.json 同源），读不到不会影响启动。
        {
            QFile profileFile(module.strProfilePath);
            if (profileFile.open(QIODevice::ReadOnly))
                parts->commands.addCommandsFromProfile(profileFile.readAll());
        }

        // 通用契约：每个客户都注册共享默认对话框工厂；April 不覆盖具体实现。
        module.listDlgFactoriesBind.append({ QString::fromLatin1(AA::about::kAboutDlgId), &parts->aboutDialog });
        module.listDlgFactoriesBind.append({ QString::fromLatin1(AA::samples::kSampleParamsDlgId), &parts->sampleParams });
        // 示例命令仅登记元数据，使共享集成测试可发现；实际执行由客户侧命令出口分发。
        parts->commands.addCommand(AA::CommandView{ QString::fromLatin1(AA::samples::kSampleRunCmdId) });

        // #2 组合根接线：April -> ApplicationCompositionRoot::build()
        //   -> AprilWorkbench::bindViewportServices + Commands::setSink(OperationCommandSink)
        // 不抛；build()/接线失败返回 built/wired=false（见 CompositionRoot.h）。
        // 状态栏 sink 由面板工厂注册到 workbench，后续每次视口刷新都会自动带上。
        parts->panels.setWorkbench(&parts->centerView);
        attachCompositionRoot(parts->centerView, parts->commands, parts->wiring,
            parts->commandSink, parts->toolSwitchRegistry);
        // M3：centerView 是 Apr::CenterView（: Apr::AprilWorkbench），
        // createCenterView 返回现有 RenderViewport2D / Viewport3D；
        // 视口服务由本客户持有并注入；与 UiShell ShellServices 互转。
        // M6：命令表用 Apr::Commands 适配现有 OperationBus + UiShell 外壳命令；
        // 同时提供 ICommandCatalog / ICommandInvoker / ICommandState。
        // dispatcher 不再来自旧 WorkbenchWindow/UiWorkbench。

        return module;
    }
}  // namespace AA
