#include "UiShell/Contract/CustomerModule.h"
#include "UiShell/Contract/BuildFingerprint.h"

#include "panels/PanelFactory.h"
#include "commands/Commands.h"
#include "dialogs/AboutDialogFactory.h"
#include "dialogs/GoodLaserSampleDialog.h"
#include "UiDialogs/AboutDialogs.h"
#include "UiDialogs/SampleDialogs.h"
#include "qml/QmlPanelFactory.h"

static void UiInitGoodLaserResources()
{
    Q_INIT_RESOURCE(qml);
    Q_INIT_RESOURCE(GoodLaser);
    Q_INIT_RESOURCE(UiGoodLaserProfile);
}

namespace GL
{
    namespace
    {
        // 客户显示名
        QString customerDisplayName()
        {
            return QStringLiteral("GoodLaser");
        }

        // 全部部件
        class ModuleParts final : public RD::IApiObject
        {
        public:
            PanelFactory panels;
            QmlPanelFactory qmlPanels;
            CenterView centerView;
            Commands commands;
            AboutDialogFactory aboutDialog;

            GoodLaserSampleDialogFactory sampleParams;  // 示例参数话框

            void destroy() override
            {
                delete this;  // 在DLL内释放，不跨 CRT
            }
        };
    }  // namespace
}  // namespace GL

namespace RD
{
    const char* customerBuildFingerprint() noexcept
    {
        return UI_BUILD_FINGERPRINT;
    }

    CustomerModule createCustomerModule()
    {
        UiInitGoodLaserResources();

        auto* parts = new GL::ModuleParts();

        CustomerModule module;
        module.owner = parts;
        module.strId = QStringLiteral("goodlaser");
        module.strDisplayName = GL::customerDisplayName();
        module.displayNameFn = &GL::customerDisplayName;

        module.strProfilePath = QString::fromUtf8(UI_CUSTOMER_PROFILE);
        module.strStyleSeries = QStringLiteral("goodlaser");
        module.listWidgetFactoriesBind.append({ QStringLiteral("goodlaser.panel"), &parts->panels });
        module.listWidgetFactoriesBind.append({ QStringLiteral("goodlaser.qml"), &parts->qmlPanels });

        module.centerView = &parts->centerView;
        module.cmdCatalog = &parts->commands;
        module.cmdInvoker = &parts->commands;
        module.cmdState = &parts->commands;

        module.listDlgFactoriesBind.append({ QString::fromLatin1(RD::about::kAboutDlgId), &parts->aboutDialog });
        module.listDlgFactoriesBind.append({ QString::fromLatin1(RD::samples::kSampleParamsDlgId), &parts->sampleParams });

        // 命令→对话框绑定
        module.listDialogBind.append({ QString::fromLatin1(RD::about::kAboutCmdId),
            QString::fromLatin1(RD::about::kAboutDlgId),
            QJsonObject{ { QStringLiteral("logoPath"), QStringLiteral("assets/logo.png") } },
            DialogContinuePolicy::Never });

        const QJsonObject sampleParams{
            { QStringLiteral("target"), 3 },
            { QStringLiteral("maxIterations"), 30 },
            { QStringLiteral("tolerance"), 1e-8 }
        };

        module.listDialogBind.append({ QString::fromLatin1(RD::samples::kSampleRunCmdId),
            QString::fromLatin1(RD::samples::kSampleParamsDlgId), sampleParams });
        return module;
    }
}  // namespace RD