#include "UiShell/Contract/CustomerModule.h"
#include "UiShell/Contract/BuildFingerprint.h"

#include "panels/PanelFactory.h"
#include "commands/Commands.h"
#include "widgets/Dialogs/AboutDialog.h"            // MantiSoft 客户定制的 About 对话框
#include "UiDialogs/SampleDialogs.h"

// 静态链接时 qrc 资源对象需要被显式引用一次，否则可能被链接器丢弃。
// Q_INIT_RESOURCE 会就地声明资源初始化函数，必须在全局命名空间里调用，因此这个函数不放进客户命名空间。
static void UiInitMantiSoftResources()
{
    Q_INIT_RESOURCE(MantiSoft);
    Q_INIT_RESOURCE(UiMantiSoftProfile);  // 内嵌 profile（ui.json + assets/），见 cmake/UiCustomerProfile.cmake
}

namespace MS
{
    namespace
    {
        // 客户显示名：固定为工程名，不翻译；语言切换后宿主仍经 displayNameFn 重新读取以刷新窗口标题。
        QString customerDisplayName()
        {
            return QStringLiteral("MantiSoft");
        }

        // 本客户的全部部件：一个堆对象，CustomerModule 里的指针都指向它的成员。
        // 宿主经 CustomerModule::owner 接管，在运行期销毁后调用 destroy()；不使用任何全局变量。
        class ModuleParts final : public RD::IApiObject
        {
        public:
            PanelFactory panels;
            CenterView centerView;
            Commands commands;
            MS::AboutDialogFactory aboutDialog;                    // 客户定制的关于框（覆盖共享默认实现）
            RD::samples::SampleParamsDialogFactory sampleParams;  // 共享默认的示例参数对话框。

            void destroy() override
            {
                delete this;  // 在客户库内释放，不跨 CRT
            }
        };
    }  // namespace
}  // namespace MS

namespace RD
{
    const char* customerBuildFingerprint() noexcept
    {
        return UI_BUILD_FINGERPRINT;
    }

    CustomerModule createCustomerModule()
    {
        UiInitMantiSoftResources();

        auto* parts = new MS::ModuleParts();

        CustomerModule module;
        module.owner = parts;
        module.strId = QStringLiteral("mantisoft");
        module.strDisplayName = MS::customerDisplayName();
        module.displayNameFn = &MS::customerDisplayName;

        module.strProfilePath = QString::fromUtf8(UI_CUSTOMER_PROFILE);
        // 皮肤品牌系列：对话框（共享默认的与本客户自己的）都经它套用本客户的品牌色。
        module.strStyleSeries = QStringLiteral("mantisoft");
        module.listWidgetFactoriesBind.append({ QStringLiteral("mantisoft.panel"), &parts->panels });
        module.centerView = &parts->centerView;
        module.cmdCatalog = &parts->commands;
        module.cmdInvoker = &parts->commands;
        module.cmdState = &parts->commands;  // 命令表同时提供可用 / 勾选状态。
        module.cmdProfileRegistrar = &parts->commands;  // 外置 --profile 也能补齐菜单命令

        // 关于对话框：注册本客户定制实现；产品/版本等内容由 AboutDialogFactory 自翻译。
        module.listDlgFactoriesBind.append({ QString::fromLatin1(RD::about::kAboutDlgId), &parts->aboutDialog });
        // 示例参数对话框：注册共享默认实现（UiDialogs）；需要不同界面的客户用同一 id 注册自己的工厂（见 GoodLaser）。
        module.listDlgFactoriesBind.append({ QString::fromLatin1(RD::samples::kSampleParamsDlgId), &parts->sampleParams });

        // 命令→对话框绑定（客户代码拥有，取代 ui.json 的 dialogBindings）。
        // help.about：关于框不需要参数，全部数据由 AboutDialogFactory 自翻译。
        module.listDialogBind.append({ QString::fromLatin1(RD::about::kAboutCmdId),
            QString::fromLatin1(RD::about::kAboutDlgId),
            QJsonObject{ { QStringLiteral("logoPath"), QStringLiteral("assets/logo.svg") } },
            DialogContinuePolicy::Never });
        // sample.run：先弹参数对话框（默认值写在 params），确定后把数据交给 runSample 命令。
        const QJsonObject sampleParams{
            { QStringLiteral("target"), 2 },
            { QStringLiteral("maxIterations"), 20 },
            { QStringLiteral("tolerance"), 1e-6 },
            { QStringLiteral("method"), QStringLiteral("newton") }
        };
        module.listDialogBind.append({ QString::fromLatin1(RD::samples::kSampleRunCmdId),
            QString::fromLatin1(RD::samples::kSampleParamsDlgId), sampleParams });
        return module;
    }
}  // namespace RD