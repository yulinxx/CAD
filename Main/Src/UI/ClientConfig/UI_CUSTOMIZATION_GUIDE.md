/**
 * @file UI_CUSTOMIZATION_GUIDE.md
 * @brief UI 定制改造指南
 *
 * 本文档说明如何通过配置系统定制 UI，无需修改核心代码。
 *
 * 目录：
 * 1. 新增面板 → UiPanelRegistry 注册
 * 2. 新增命令 → JSON 配置 + CommandActionHub
 * 3. 调整布局 → JSON 配置 docks 节
 */

// =============================================================================
// 1. 新增面板 → UiPanelRegistry 注册
// =============================================================================
/*
 * 步骤：
 * 1. 在 JSON 配置的 docks 节声明面板
 * 2. 在代码中注册面板工厂
 *
 * JSON 配置示例 (client_xxx.json):
 * {
 *   "docks": [
 *     {
 *       "id": "MyCustomDock",           // 唯一 ID
 *       "title": "My Custom Panel",     // 面板标题
 *       "position": "right",            // 停靠位置: left/right/top/bottom
 *       "widgetType": "MyCustomPanel",  // 工厂 ID（对应 registerPanel 的 key）
 *       "visible": true,
 *       "area": "right",                // Qt::DockWidgetArea
 *       "allowedAreas": ["right", "bottom"]
 *     }
 *   ]
 * }
 *
 * 代码注册示例:
 * ```cpp
 * #include "ClientConfig/UiPanelRegistry.h"
 * #include "MyCustomPanelWidget.h"
 *
 * auto& registry = UiConfigurationManager::shared().panelRegistry();
 * registry->registerPanel("MyCustomPanel", [](QWidget* parent) {
 *     return new MyCustomPanelWidget(parent);
 * });
 * ```
 *
 * 面板 Widget 示例:
 * ```cpp
 * class MyCustomPanelWidget : public QWidget
 * {
 *     Q_OBJECT
 * public:
 *     explicit MyCustomPanelWidget(QWidget* parent = nullptr)
 *         : QWidget(parent)
 *     {
 *         auto* layout = new QVBoxLayout(this);
 *         layout->addWidget(new QLabel("Custom Panel Content"));
 *     }
 * };
 * ```
 */

// =============================================================================
// 2. 新增命令 → JSON 配置 + CommandActionHub
// =============================================================================
/*
 * 步骤：
 * 1. 在 JSON 配置的 menus/toolbars/contextMenus 节声明命令
 * 2. 在 OperationId.h 中添加操作 ID
 * 3. 在 CommandCatalog 中添加目录条目
 * 4. 在 CommandActionHub 中实现命令处理
 *
 * JSON 配置示例:
 * {
 *   "menus": [{
 *     "id": "custom",
 *     "label": "Custom",
 *     "items": [
 *       {
 *         "type": "action",
 *         "id": "custom.my_command",     // 唯一命令 ID
 *         "label": "My Command",         // 显示文本
 *         "command": "custom.my_command", // 命令字符串（与 id 相同）
 *         "icon": ":/icons/my_command.svg",
 *         "shortcut": "Ctrl+Shift+M",
 *         "workbenches": ["2D", "3D"]
 *       }
 *     ]
 *   }]
 * }
 *
 * 添加操作 ID (OperationId.h):
 * ```cpp
 * enum class OperationId : uint32_t
 * {
 *     None = 0,
 *     // ... existing IDs ...
 *     CustomMyCommand = 0x1A00,  // 新增
 * };
 * ```
 *
 * 添加命令目录条目 (CommandCatalog.cpp):
 * ```cpp
 * {OperationId::CustomMyCommand, UI::MenuActionId::CustomMyCommand, "My Command",
 *  "shortcut.custom.my_command", ":/icons/my_command.svg",
 *  CommandSurface2DValues::Menu | CommandSurface2DValues::Toolbar | CommandSurface2DValues::ContextMenu,
 *  CommandEnable2D::RequiresSelection, false}
 * ```
 *
 * 实现命令处理 (CommandActionHubActions.cpp):
 * ```cpp
 * case OperationId::CustomMyCommand:
 *     if (auto* scene = sceneService->scene()) {
 *         // 执行命令逻辑
 *         qDebug() << "My command executed";
 *     }
 *     break;
 * ```
 */

// =============================================================================
// 3. 调整布局 → JSON 配置 docks 节
// =============================================================================
/*
 * 步骤：
 * 只需修改 JSON 配置，无需修改代码
 *
 * 布局配置示例:
 * {
 *   "docks": [
 *     {
 *       "id": "SceneDock",
 *       "title": "Scene Tree",
 *       "position": "left",
 *       "widgetType": "SceneTreePanel",
 *       "visible": true,
 *       "floating": false,              // 是否浮动
 *       "area": "left",                 // Qt::DockWidgetArea
 *       "allowedAreas": ["left", "right"],
 *       "size": { "width": 250, "height": 400 },
 *       "minimumSize": { "width": 150, "height": 200 }
 *     },
 *     {
 *       "id": "PropertiesDock",
 *       "title": "Properties",
 *       "position": "right",
 *       "widgetType": "PropertiesPanel",
 *       "visible": true,
 *       "area": "right"
 *     }
 *   ]
 * }
 *
 * 隐藏/显示面板:
 * - visible: true/false 控制初始显示
 * - 运行时可通过 QDockWidget::toggleViewAction() 控制
 *
 * 调整停靠顺序:
 * - position 控制停靠在哪一侧
 * - 配置顺序影响 Tab 顺序
 */

// =============================================================================
// 4. 客户特定配置继承
// =============================================================================
/*
 * 配置继承链:
 * base.json → san_yi.json → client_a.json / client_b.json
 *
 * client_a.json 示例:
 * {
 *   "meta": {
 *     "clientId": "client_a",
 *     "clientName": "客户A定制版",
 *     "version": "2.0",
 *     "inherits": "san_yi"        // 继承 san_yi 配置
 *   },
 *   // 覆盖/新增 docks
 *   "docks": [
 *     {
 *       "id": "ClientADock",     // 新增客户专属面板
 *       "title": "客户A专用",
 *       "position": "right",
 *       "widgetType": "ClientAPanel",
 *       "visible": true
 *     }
 *   ],
 *   // 覆盖/新增菜单
 *   "menus": [
 *     {
 *       "id": "custom.client_a",
 *       "label": "客户A菜单",
 *       "items": [...]
 *     }
 *   ]
 * }
 *
 * 运行时分客户加载:
 * - 环境变量: SANYI_CLIENT_ID=client_a
 * - 命令行: --client=client_a
 * - QSettings: Client/Id
 */

// =============================================================================
// 5. 右键菜单定制
// =============================================================================
/*
 * JSON 配置右键菜单:
 * {
 *   "contextMenus": [
 *     {
 *       "id": "canvas.2d",
 *       "workbench": "2D",
 *       "items": [
 *         { "type": "action", "id": "edit.delete", "label": "Delete", "command": "edit.delete" },
 *         { "type": "separator" },
 *         {
 *           "type": "submenu",
 *           "id": "custom.submenu",
 *           "label": "Custom Submenu",
 *           "items": [
 *             { "type": "action", "id": "custom.action1", "label": "Action 1", "command": "custom.action1" }
 *           ]
 *         }
 *       ],
 *       "dynamicSections": ["layer.actions"]  // 动态段落（根据上下文插入）
 *     }
 *   ]
 * }
 */

// =============================================================================
// 6. 主题定制
// =============================================================================
/*
 * 主题切换: Help → Theme 菜单
 * 主题文件位置: UI/Common/Resources/Themes/
 *
 * 预设主题:
 * - Light (浅色)
 * - Dark (深色)
 * - Blue (蓝色)
 * - Slate (石板色)
 * - HighContrast (高对比度)
 * - Default (默认样式)
 * - System (跟随系统)
 *
 * 自定义主题步骤:
 * 1. 复制现有 .qss 文件
 * 2. 修改颜色变量
 * 3. 在 ThemeManager 中添加新枚举值
 * 4. 可选：在 icon 目录添加对应 flavor 子目录
 */

// =============================================================================
// 7. 快捷键定制
// =============================================================================
/*
 * 快捷键单一来源: 菜单项的 shortcut 字段
 * 本节只用于声明「没有菜单入口」的命令快捷键
 *
 * {
 *   "shortcuts": [
 *     { "id": "shortcut.tool.select", "key": "S" },
 *     { "id": "shortcut.tool.line", "key": "L" }
 *   ]
 * }
 */
