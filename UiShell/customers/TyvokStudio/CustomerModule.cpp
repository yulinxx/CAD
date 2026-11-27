#include "UiShell/Contract/CustomerModule.h"
#include "UiShell/Contract/BuildFingerprint.h"

#include <QJsonArray>

#include "panels/PanelFactory.h"
#include "commands/Commands.h"
#include "dialogs/AboutDialogFactory.h"
#include "UiDialogs/AboutDialogs.h"
#include "UiDialogs/SampleDialogs.h"

// 静态链接时 qrc 资源对象需要被显式引用一次，否则可能被链接器丢弃。
static void UiInitTyvokStudioResources()
{
    Q_INIT_RESOURCE(TyvokStudio);
    Q_INIT_RESOURCE(UiTyvokStudioProfile);  // 内嵌 profile（ui.json + assets/）
}

namespace TS
{
    namespace
    {
        // 客户显示名
        QString customerDisplayName()
        {
            return QStringLiteral("TyvokStudio");
        }

        // 客户的全部部件
        class ModuleParts final : public RD::IApiObject
        {
        public:
            PanelFactory panelsFactory;
            CenterView centerView;
            Commands commands;
            AboutDialogFactory aboutDlgFactory;                   // 本客户的关于框
            RD::samples::SampleParamsDialogFactory sampleParams;  // 测试用: 示例参数对话框

            void destroy() override
            {
                delete this;  // 在客户库内释放，不跨 CRT
            }
        };
    }  // namespace
}  // namespace TS

namespace RD
{
    const char* customerBuildFingerprint() noexcept
    {
        return UI_BUILD_FINGERPRINT;
    }

    CustomerModule createCustomerModule()
    {
        UiInitTyvokStudioResources();

        auto* parts = new TS::ModuleParts();

        CustomerModule module;
        module.owner = parts;
        module.strId = QStringLiteral("tyvokstudio");
        module.strDisplayName = TS::customerDisplayName();
        module.displayNameFn = &TS::customerDisplayName;

        module.strProfilePath = QString::fromUtf8(UI_CUSTOMER_PROFILE);
        module.strStyleSeries = QStringLiteral("tyvokstudio");

        module.centerView = &parts->centerView;
        module.cmdCatalog = &parts->commands;
        module.cmdInvoker = &parts->commands;
        module.cmdState = &parts->commands;
        module.cmdProfileRegistrar = &parts->commands;  // 外置 --profile 也能补齐菜单命令

        // 注册面板工厂
        module.listWidgetFactoriesBind.append({ QStringLiteral("tyvokstudio.panel"), &parts->panelsFactory });

        // 注册关于对话框工厂
        module.listDlgFactoriesBind.append({ QString::fromLatin1(RD::about::kAboutDlgId), &parts->aboutDlgFactory });
        // 注册示例参数对话框工厂
        module.listDlgFactoriesBind.append({ QString::fromLatin1(RD::samples::kSampleParamsDlgId), &parts->sampleParams });

        // 命令→对话框绑定（客户代码拥有，取代 ui.json 的 dialogBindings）。
        // help.about：关于框不需要参数，全部数据由 AboutDialogFactory 自翻译。
        module.listDialogBind.append({ QString::fromLatin1(RD::about::kAboutCmdId),
            QString::fromLatin1(RD::about::kAboutDlgId),
            QJsonObject{ { QStringLiteral("logoPath"), QStringLiteral("assets/logo.png") } },
            DialogContinuePolicy::Never });
        // sample.run：先弹参数对话框（默认值写在 params），确定后把数据交给 runSample 命令；
        // methods 限制本客户只开放二分法。
        const QJsonObject sampleParams{
            { QStringLiteral("target"), 10 },
            { QStringLiteral("maxIterations"), 60 },
            { QStringLiteral("tolerance"), 1e-4 },
            { QStringLiteral("method"), QStringLiteral("bisection") },
            { QStringLiteral("methods"), QJsonArray{ QStringLiteral("bisection") } }
        };
        module.listDialogBind.append({ QString::fromLatin1(RD::samples::kSampleRunCmdId),
            QString::fromLatin1(RD::samples::kSampleParamsDlgId), sampleParams });
        return module;
    }
}  // namespace RD