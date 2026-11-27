#include "UiShell/Contract/CustomerModule.h"

#include "UiShell/Contract/BuildFingerprint.h"

#include "panels/PanelFactory.h"

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
        class ModuleParts final : public RD::IApiObject
        {
        public:
            CenterView centerView;

            void destroy() override
            {
                delete this;  // 在客户库内释放，不跨 CRT
            }
        };
    }  // namespace
}  // namespace Apr

namespace RD
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
        // M3：centerView 是 Apr::CenterView（: Apr::AprilWorkbench），
        // createCenterView 返回现有 RenderViewport2D / Viewport3D；
        // 视口服务由本客户持有并注入；与 UiShell ShellServices 互转。
        // 命令表尚未接入（M4）；留空。

        return module;
    }
}  // namespace RD
