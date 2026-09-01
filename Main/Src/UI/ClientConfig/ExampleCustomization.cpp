/**
 * @file ExampleCustomization.cpp
 * @brief 示例：如何在启动时注册定制面板和命令
 *
 * 使用方法：
 * 1. 在应用入口（如 main.cpp 或 App.cpp）中调用 registerExampleCustomizations()
 * 2. 确保 example_client.json 已添加到 Qt 资源文件
 *
 * 注意事项：
 * - 面板工厂注册必须在 UiConfigurationManager::applyConfiguration() 之前调用
 * - 或者在配置加载后、首次显示面板前注册
 */

#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiPanelRegistry.h"
#include "ExampleCustomPanel.h"

#include <QDebug>

/**
 * @brief 注册所有示例定制内容
 *
 * 实际使用时，将此函数拆分为多个注册函数，
 * 按客户 ID 条件注册不同的定制内容。
 */
void registerExampleCustomizations()
{
    auto* registry = UiConfigurationManager::shared().panelRegistry();

    // ---- 注册示例面板 ----
    // widgetType 必须与 JSON 配置中的 widgetType 匹配
    registry->registerPanel("ExamplePanel", [](QWidget* parent) -> QWidget* {
        return new ExampleCustomPanel(parent);
    });

    qDebug() << "Example customizations registered: ExamplePanel";

    // ---- 更多面板注册示例 ----
    // registry->registerPanel("StatisticsPanel", [](QWidget* parent) {
    //     return new StatisticsPanel(parent);
    // });
    //
    // registry->registerPanel("ToolPalettePanel", [](QWidget* parent) {
    //     return new ToolPalettePanel(parent);
    // });
}

/**
 * @brief 示例：如何在 CommandActionHub 中处理自定义命令
 *
 * 在 CommandActionHubActions.cpp 或类似的命令处理文件中添加:
 *
 * ```cpp
 * case OperationId::ExampleInfo:
 * {
 *     QMessageBox::information(nullptr, "Info",
 *         QObject::tr("This is an example custom command."));
 *     break;
 * }
 * ```
 *
 * 对应的 OperationId 定义 (OperationId.h):
 * ```cpp
 * enum class OperationId : uint32_t
 * {
 *     // ... existing ...
 *     ExampleInfo = 0x1B00,
 *     ExampleSettingsPreferences = 0x1B01,
 *     ExampleSettingsAdvanced = 0x1B02,
 * };
 * ```
 *
 * 对应的命令目录条目 (CommandCatalog.cpp):
 * ```cpp
 * {OperationId::ExampleInfo, UI::MenuActionId::ExampleInfo, "Show Info",
 *  "shortcut.example.info", ":/ui/common/Icons/Help/about.svg",
 *  CommandSurface2D::Menu | CommandSurface2D::Toolbar,
 *  CommandEnable2D::Always, false}
 * ```
 */

/**
 * @brief 示例：如何根据客户 ID 条件注册定制
 *
 * ```cpp
 * void registerCustomerSpecificCustomizations()
 * {
 *     const QString& clientId = UiClientContext::instance().clientId();
 *
 *     if (clientId == "client_a") {
 *         // 客户A专属注册
 *         UiConfigurationManager::shared().panelRegistry()->registerPanel(
 *             "ClientAPanel", [](QWidget* p) { return new ClientAPanel(p); });
 *     }
 *     else if (clientId == "client_b") {
 *         // 客户B专属注册
 *         UiConfigurationManager::shared().panelRegistry()->registerPanel(
 *             "ClientBPanel", [](QWidget* p) { return new ClientBPanel(p); });
 *     }
 *     // ... 更多客户
 * }
 * ```
 */
