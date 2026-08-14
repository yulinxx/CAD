# UI 定制变更设计方案

## 1. 项目概况

| 项目 | 内容 |
|------|------|
| 项目名 | SanYiCAD（三益CAD） |
| 语言 | C++17 |
| 框架 | Qt 6（Widgets, OpenGLWidgets, Core, Gui） |
| 构建系统 | CMake 4.3+ |
| 类型 | 桌面 CAD 应用程序 |

---

## 2. 目录结构

```
SanYiCAD/
├── CMakeLists.txt              # 根 CMake，编排所有子项目
├── Config.txt                  # 构建配置（Qt 路径、vcpkg、模块开关）
├── CMake/                      # CMake 辅助脚本
├── Main/                       # 主可执行程序
│   ├── Src/
│   │   ├── main.cpp            # 入口
│   │   ├── Runtime/            # 运行时
│   │   ├── Bootstrap/          # 启动引导
│   │   ├── Composition/        # 组合根（手动 DI 容器）
│   │   ├── Common/             # 初始化、路径管理
│   │   ├── License/            # 许可管理
│   │   ├── RenderCore/         # 统一渲染抽象层
│   │   └── UI/                 # 应用层 UI 实现
│   │       └── ClientConfig/   # 客户配置目录（新增）
│   │           ├── UiConfigLoader.h          # 配置加载器（解析 JSON → 数据模型）
│   │           ├── UiConfigLoader.cpp
│   │           ├── UiClientConfigBase.h      # 配置数据结构定义
│   │           ├── UiLayoutBuilder.h         # 布局构建器（根据数据创建 Qt UI）
│   │           ├── UiLayoutBuilder.cpp
│   │           ├── UiPanelRegistry.h         # 面板工厂注册表
│   │           ├── UiPanelRegistry.cpp
│   │           ├── configs/                  # JSON 配置文件目录
│   │           │   ├── san_yi.json           # 默认客户配置（三益标准版）
│   │           │   ├── client_a.json         # 客户 A 配置
│   │           │   ├── client_b.json         # 客户 B 配置
│   │           │   └── ...                   # 其他客户 JSON 配置
│   │           ├── resources/
│   │           │   └── configs.qrc           # Qt 资源文件（按客户选择嵌入）
│   └── resources/
├── UI/                         # [Git Submodule] UI 组件库
├── Engine/                     # [Git Submodule] 引擎核心
├── Render/                     # [Git Submodule] 渲染层
├── Utility/                    # [Git Submodule] 基础工具库
├── Log/                        # [Git Submodule] 日志
├── CrashHandler/               # [Git Submodule] 崩溃捕获
├── FileIO/                     # [Git Submodule] 文件导入导出
├── Nesting/                    # [Git Submodule] 排样优化
├── Engraving/                  # [Git Submodule] 激光雕刻
├── GeoModelCore/               # [Git Submodule] OpenCASCADE 几何内核
├── Hardware/                   # [Git Submodule] 激光控制、材料库
├── Network/                    # [Git Submodule] HTTP、WebSocket、云同步
├── Vision/                     # [Git Submodule] 图像处理
├── PythonHost/                 # [Git Submodule] Python 集成
├── PyBindCore/                 # [Git Submodule] Python 绑定
└── ThirdParty/                 # [Git Submodule] 第三方依赖
```

---

## 3. 当前架构分析

### 3.1 启动流程

```
main()
  └─ runCADApplication()
       └─ CADApplicationRuntime::run()
            ├─ AppInitializer::initialize()
            ├─ LicenseManager::CheckLicense()
            └─ AppBootstrapper(paths, name, version)
                 └─ bootstrapper.initialize()
                      └─ ApplicationCompositionRoot()
                           ├─ 创建所有 Service
                           │    ├─ UiStateCenter
                           │    ├─ DefaultUiThemeService
                           │    ├─ DefaultUiLayoutService
                           │    ├─ DefaultInteractionDispatcher
                           │    ├─ DefaultUndoStack
                           │    └─ UiShellHost
                           ├─ 注入依赖关系
                           └─ registerCommands()  // ~10 个命令处理器
                 └─ bootstrapper.bootstrap()
                      ├─ 组装 UiServices 结构体
                      ├─ 创建 Workbench2D 或 Workbench3D
                      ├─ workbench.initialize(services)
                      ├─ shell.setFrameworkServices()
                      ├─ shell.setUiServices()
                      ├─ shell.setWorkbench()
                      └─ shell.initializeAndShow()
                           ├─ workbench.attachToWindow(mainWindow)
                           │    ├─ 创建 Dock、Toolbar、Viewport
                           │    ├─ 通过 SceneBuilder 创建文档
                           │    └─ 在主窗口注册面板/工具栏
                           ├─ workbench.activate()
                           ├─ 设置主题变更回调
                           ├─ 设置 Workbench Factory（延迟创建 3D）
                           └─ mainWindow.show()
```

### 3.2 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│  UI Layer (Main/Src/UI + UI/)                              │
│  - Qt Widgets, QMainWindow, QGraphicsView                   │
│  - WorkbenchWindow, Viewport2D, Viewport3D, Panels          │
│  - IInteractionDispatcher, OperationBus (UI commands)       │
│  - UiStateCenter (reactive state)                           │
│  - UiConfigLoader (编译期嵌入 JSON 配置加载)                  │
├─────────────────────────────────────────────────────────────┤
│  Engine Layer (Engine/)                                     │
│  - Eg::SceneManager (2D scene graph)                        │
│  - Eg::SyEntity (geometric primitives)                      │
│  - SceneDocument3D (3D scene tree)                          │
│  - Algorithm (boolean ops, offsets, etc.)                   │
├─────────────────────────────────────────────────────────────┤
│  Render Layer (Render/)                                     │
│  - RenderCommon, Render2D, Render3D                         │
│  - IRenderer3D, rendering pipeline                          │
├─────────────────────────────────────────────────────────────┤
│  Infrastructure (Utility, Log, CrashHandler)                │
│  - Containers, math, logging, crash reporting               │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 当前使用的设计模式

| 模式 | 用途 | 位置 |
|------|------|------|
| **Composition Root** | 手动 DI 容器，创建并注入所有服务 | `ApplicationCompositionRoot` |
| **Command Pattern** | 命令生命周期管理，支持撤销/重做 | `IInteractionDispatcher` + `OperationBus` |
| **Service Locator** | 服务聚合结构体 | `UiServices` struct |
| **Observer Pattern** | 状态变更通知（Qt Signals） | `UiStateCenter` |
| **Strategy Pattern** | 主题、布局策略可替换 | `UiThemeService`、`UiLayoutService` |
| **Workbench Pattern** | 工作台模式（类似 Eclipse RCP） | `UiWorkbench` → `Workbench2D` / `Workbench3D` |
| **Adapter Pattern** | 桥接引擎与 UI | `SceneEditServiceAdapter`、`ViewWidgetAdapter` 等 |

### 3.4 当前 UI 组件注册方式

| 组件 | 注册方式 | 位置 |
|------|---------|------|
| 主窗口 | 在 UiShellHost 构造函数中创建 | `UiShellHost.cpp` |
| 菜单栏 | 硬编码在 WorkbenchWindow | `WorkbenchWindow.cpp:buildMenus()` |
| 工具栏 | Workbench 创建，注册到主窗口 | `Workbench2D::attachToWindow()` |
| Dock 面板 | Workbench 创建，注册到主窗口 | `Workbench2D::attachToWindow()` |
| 视口 | 由 Workbench 设置为 centralWidget | `Workbench2D::createCentralViewport()` |
| 命令处理器 | 注册到 OperationBus | `ApplicationCompositionRoot::registerCommands()` |
| 主题样式 | 从 Qt 资源加载 QSS | `resources.qrc` |

### 3.5 当前架构评估

#### 优势

1. **清晰的分层架构** — Git Submodule 物理隔离，层间依赖明确
2. **Workbench 抽象** — 支持 2D/3D 多种工作模式
3. **Command 模式** — 解耦用户操作与 UI 组件
4. **组合根** — 集中管理依赖创建与注入
5. **状态中心** — 集中式响应式状态管理
6. **撤销/重做** — 通过 Command 模式统一支持
7. **主题系统** — 基于 QSS 可扩展

#### 对 UI 定制的不足

1. **无外部化 UI 定义** — 菜单、工具栏、面板全部硬编码在 C++ 中，改 UI 必须修改 C++ 代码
2. **无面板工厂** — 自定义 Dock 面板必须修改 Workbench 代码，无法直接由配置驱动
3. **客户配置与代码耦合** — 每个客户定制必须修改多处 C++ 源文件，改 UI 布局也触发 C++ 重新编译
4. **无客户专属资源隔离** — 主题、图标、样式等资源混在一起
5. **CMake 无客户选择机制** — 无法通过编译参数选择客户版本

---

## 4. UI 定制需求分析

### 4.1 需求场景

| 需求 | 描述 |
|------|------|
| 菜单定制 | 增删菜单项、修改菜单结构、更换菜单位置 |
| 工具栏定制 | 增删工具栏、调整工具栏位置、修改工具栏功能按钮 |
| 面板定制 | 增删 Dock 面板、调整面板位置 |
| 布局定制 | 完全更换界面布局（如从单文档切换为多文档） |
| 主题定制 | 更换整体视觉风格、颜色、图标 |
| 功能定制 | 为不同客户隐藏/显示特定功能 |
| 完整 UI 替换 | 针对特定客户提供完全不同的 UI 界面 |

### 4.2 核心约束

| 约束 | 说明 |
|------|------|
| 编译期确定 | 每个客户一个版本，编译时确定配置，无需运行时配置加载 |
| 无需热加载 | 不支持运行时动态切换配置 |
| 多客户支持 | 支持 10 个以上客户定制 |
| 算法不变 | Engine/Render/Utility 层不随客户变化 |

### 4.3 不变层

- Engine（几何算法、场景管理）
- Render（渲染管线）
- Utility、Log、CrashHandler 等基础设施
- 命令处理器的业务逻辑（Command Handler 内部算法不变）
- IInteractionDispatcher、UiStateCenter 等核心服务

### 4.4 变层

- `Main/Src/UI/ClientConfig/configs/` 下的 JSON 配置文件和客户专属资源
- 菜单、工具栏、Dock 面板的定义与布局（JSON 数据）
- 主题样式与图标资源（可按客户隔离）
- 客户专属的自定义面板（C++ 实现，通过面板工厂注册）

---

## 5. 方案：编译期嵌入 JSON 配置方案

### 5.1 思路

将菜单、工具栏、Dock 面板等 UI 组件的定义从硬编码 C++ 中抽离到 **JSON 数据文件**中，通过 CMake 编译选项选择目标客户，将对应客户的 JSON 配置通过 Qt 资源（`.qrc`）编译期嵌入最终二进制。运行时由 `UiConfigLoader` 解析 JSON 为 C++ 数据结构，再由 `UiLayoutBuilder` 根据数据构建实际 UI 组件。

**与纯 C++ 配置方案的关键区别**：

| 维度 | C++ 编译期方案（旧） | JSON 嵌入方案（本方案） |
|------|-------------------|----------------------|
| 客户配置形式 | 每个客户一个 C++ 类（.h + .cpp） | 每个客户一个 JSON 文件 |
| 新增客户工作量 | 创建 2 个 C++ 文件 + 修改 CMake 分支 | 创建 1 个 JSON 文件 + CMake 加一行 |
| CMake 分支数 | 与客户数成正比（每个客户一个 elseif） | 固定 2 个分支（仅根据客户 ID 选 qrc） |
| 改 UI 布局 | 改 C++ → 重新编译整个工程 | 改 JSON → 重新编译仅资源变更 |
| 类型安全性 | 编译期类型检查 | 解析时验证（可加单元测试） |
| 非 C++ 维护 | 必须 C++ 开发者 | 懂 JSON 即可 |
| 二进制内容 | 配置数据在代码段（RO data） | 配置数据在资源段（也属 RO data） |

### 5.2 架构设计

```
┌───────────────────────────────────────────────────────────────────┐
│                        CMake 编译选项                              │
│                  -DSANYI_CLIENT_ID=san_yi                         │
│                  -DSANYI_CLIENT_ID=client_a                       │
├───────────────────────────────────────────────────────────────────┤
│  CMake 根据客户 ID 选择对应的 .qrc 文件                            │
│  Qt 资源编译器（rcc）在编译期将 JSON 嵌入到二进制                          │
├───────────────────────────────────────────────────────────────────┤
│                    Main/Src/UI/ClientConfig/                      │
│  ┌─────────────────┬─────────────────┬──────────────────────────┐│
│  │ UiClientConfigBase│  UiConfigLoader │       UiPanelRegistry    ││
│  │ (数据模型)        │  (JSON 解析器)   │       (面板工厂)          ││
│  └─────────────────┴─────────────────┴──────────────────────────┘│
│  ┌─────────────────┬─────────────────┬──────────────────────────┐│
│  │  configs/        │  configs/       │     resources/           ││
│  │ san_yi.json      │ client_a.json   │  configs_client_a.qrc   ││
│  └─────────────────┴─────────────────┴──────────────────────────┘│
├───────────────────────────────────────────────────────────────────┤
│                    UiLayoutBuilder (构建 Qt UI)                     │
│                     WorkbenchWindow / UiWorkbench                  │
├───────────────────────────────────────────────────────────────────┤
│                    Engine / Render / Utility                       │
│                    (不变，不依赖任何客户配置)                        │
└───────────────────────────────────────────────────────────────────┘
```

### 5.3 运行流程

```
应用启动
  └─ WorkbenchWindow::initializeWorkbenchShell()
       ├─ UiConfigLoader::load(":configs/ui_config.json")  ← 从 Qt 资源加载 JSON
       │    └─ 解析 JSON → UiConfigData 结构体（含 menus, toolBars, docks 等）
       ├─ UiLayoutBuilder::buildMenus(configData.menus)
       ├─ UiLayoutBuilder::buildToolBars(configData.toolBars)
       ├─ UiLayoutBuilder::buildDocks(configData.docks)
       ├─ UiLayoutBuilder::buildShortcuts(configData.shortcuts)
       └─ configData.registerPanels(panelRegistry)
```

### 5.4 关键设计决策说明

**为什么要 JSON 而不是 C++ 类？**

1. **10+ 客户下 CMake 不膨胀** — C++ 方案每加一个客户就要加一个 `elseif` 分支，10+ 个后 CMakeLists.txt 变得冗长。JSON 方案只需维护一个客户列表即可
2. **改 UI 布局不触发 C++ 编译** — 改 JSON 只需要重编译资源（秒级），不改 JSON 只需要重链（更快）
3. **数据与代码分离** — JSON 是纯数据，可直接用 Qt 的 `QJsonDocument` 解析，无需引入第三方 JSON 库
4. **配置可验证** — 可以为 JSON 写单元测试（加载 → 解析 → 校验字段完整性），而不需要编译整个 C++ 工程

**什么时候仍需要 C++ 配置类？**

当客户定制超出了 UI 布局层面，例如需要：
- 自定义 Qt 样式表（QSS）
- 自定义信号-槽连接逻辑
- 条件性显示（根据运行时状态决定菜单是否可用）

这些场景仍然需要少量的 C++ 代码，但可以通过 `registerPanels()` 或启动时回调来处理，而不是把整个 UI 布局定义在 C++ 中。

---

## 6. 核心接口设计

### 6.1 配置数据结构（UiClientConfigBase.h）

数据结构保持不变，所有定义与 C++ 方案一致：

```cpp
#pragma once

#include <QString>
#include <vector>
#include <variant>

/// 菜单项类型
enum class MenuItemType
{
    Action,      // 动作项（触发命令）
    Separator,   // 分隔符
    SubMenu      // 子菜单
};

/// 菜单动作定义
struct MenuActionDef
{
    QString id;               // 动作唯一标识
    QString label;            // 显示文本
    QString commandId;        // 关联的命令 ID
    QString shortcut;         // 快捷键
    bool checkable = false;
};

/// 子菜单定义
struct SubMenuDef
{
    QString id;
    QString label;
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> items;
};

/// 顶层菜单定义
struct MenuDef
{
    QString id;
    QString label;
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> items;
};

/// 工具栏位置
enum class ToolBarPosition { Top, Left, Right, Bottom };

/// 工具栏动作定义
struct ToolBarActionDef
{
    QString id;
    QString label;
    QString iconName;
    QString commandId;
    QString shortcut;
    bool checkable = false;
};

/// 工具栏定义
struct ToolBarDef
{
    QString id;
    QString title;
    ToolBarPosition position;
    QString workbenchId;      // "2D", "3D", "global"
    std::vector<std::variant<ToolBarActionDef, MenuItemType>> items;
};

/// Dock 面板位置
enum class DockPosition { Left, Right, Top, Bottom };

/// Dock 面板定义
struct DockDef
{
    QString id;
    QString title;
    DockPosition position;
    QString widgetType;       // 面板类型（由 UiPanelRegistry 解析）
    bool visible = true;
};

/// 快捷键定义
struct ShortcutDef
{
    QString commandId;
    QString keySequence;
};
```

### 6.2 配置加载器（UiConfigLoader.h）

`UiConfigLoader` 取代了 C++ 方案中的 `UiClientConfig` 抽象类。它的职责是：从 Qt 资源加载 JSON → 解析为 C++ 数据结构。

```cpp
#pragma once

#include "UiClientConfigBase.h"
#include <QString>

class UiPanelRegistry;

/// 配置加载结果（所有客户共享的数据结构）
struct UiConfigData
{
    QString clientId;
    QString clientName;
    std::vector<MenuDef> menus;
    std::vector<ToolBarDef> toolBars;
    std::vector<DockDef> docks;
    std::vector<ShortcutDef> shortcuts;
    QString themeStyle;       // 主题标识或 QSS 路径
};

/// UI 配置加载器
/// 从 Qt 资源中加载当前客户的 JSON 配置，解析为 C++ 数据结构
class UiConfigLoader
{
public:
    /// 构造函数
    /// @param resourcePath Qt 资源路径（如 ":configs/ui_config.json"）
    explicit UiConfigLoader(const QString& resourcePath);

    /// 加载并解析配置
    /// @return 解析结果，失败时返回 std::nullopt
    std::optional<UiConfigData> load();

    /// 获取加载错误信息
    QString lastError() const;

private:
    /// 解析 JSON 文档为 UiConfigData
    std::optional<UiConfigData> parseConfig(const QJsonDocument& doc);

    /// 各个子解析函数
    std::optional<MenuDef> parseMenu(const QJsonObject& obj);
    std::optional<MenuActionDef> parseMenuAction(const QJsonObject& obj);
    std::optional<SubMenuDef> parseSubMenu(const QJsonObject& obj);
    std::optional<ToolBarDef> parseToolBar(const QJsonObject& obj);
    std::optional<ToolBarActionDef> parseToolBarAction(const QJsonObject& obj);
    std::optional<DockDef> parseDock(const QJsonObject& obj);
    std::optional<ShortcutDef> parseShortcut(const QJsonObject& obj);

    QString m_resourcePath;
    QString m_lastError;
};
```

### 6.3 配置管理器（UiConfigurationManager.h）

`UiConfigurationManager` 是"多客户切换的收口点"。在编译期嵌入方案中，它的职责简化：
- 创建 `UiConfigLoader` 并加载 JSON
- 持有 `UiPanelRegistry`，协调面板注册
- 提供给上层一个统一的配置入口

```cpp
#pragma once

#include "UiConfigLoader.h"
#include "UiPanelRegistry.h"
#include <memory>

class QMainWindow;
class OperationBus;

/// UI 配置管理器
/// 多客户 UI 配置的总控点，所有 UI 定制通过此入口
class UiConfigurationManager
{
public:
    explicit UiConfigurationManager(QMainWindow* mainWindow,
                                    OperationBus* operationBus);

    /// 加载并应用当前客户的 UI 配置
    /// @param resourcePath Qt 资源中的 JSON 配置路径
    /// @return 是否成功
    bool applyConfiguration(const QString& resourcePath);

    /// 获取面板注册表
    UiPanelRegistry* panelRegistry() const;

    /// 获取加载后的配置数据
    const UiConfigData* configData() const;

private:
    /// 注册所有内置面板（图层、属性、命令等）
    void registerBuiltinPanels();

    /// 注册客户自定义面板（从 JSON 中的面板定义注册）
    void registerCustomPanels(const UiConfigData& config);

    QMainWindow* m_mainWindow;
    OperationBus* m_operationBus;
    std::unique_ptr<UiConfigData> m_configData;
    std::unique_ptr<UiPanelRegistry> m_panelRegistry;
};
```

### 6.4 布局构建器（UiLayoutBuilder.h）

基本保持不变，与 C++ 方案中的 `UiLayoutBuilder` 一致：

```cpp
#pragma once

#include "UiClientConfigBase.h"

class QMenu;
class QToolBar;
class QMainWindow;
class OperationBus;
class UiPanelRegistry;

class UiLayoutBuilder
{
public:
    UiLayoutBuilder(QMainWindow* window,
                    OperationBus* operationBus,
                    UiPanelRegistry* panelRegistry);

    void buildMenus(const std::vector<MenuDef>& menus);
    void buildToolBars(const std::vector<ToolBarDef>& toolBars);
    void buildDocks(const std::vector<DockDef>& docks);
    void buildShortcuts(const std::vector<ShortcutDef>& shortcuts);

private:
    void buildMenuItem(QMenu* parent,
                       const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item);
    QMainWindow* m_window;
    OperationBus* m_operationBus;
    UiPanelRegistry* m_panelRegistry;
};
```

### 6.5 面板工厂注册表（UiPanelRegistry.h）

保持不变，与 C++ 方案一致：

```cpp
#pragma once

#include <QString>
#include <functional>
#include <QMap>

class QWidget;

using PanelFactory = std::function<QWidget*(QWidget* parent)>;

class UiPanelRegistry
{
public:
    void registerPanel(const QString& id, PanelFactory factory);
    QWidget* createPanel(const QString& id, QWidget* parent);
    bool isPanelRegistered(const QString& id) const;
    QStringList registeredPanelIds() const;

private:
    QMap<QString, PanelFactory> m_factories;
};
```

---

## 7. JSON 配置格式

### 7.1 完整示例

```json
{
  "meta": {
    "clientId": "san_yi",
    "clientName": "三益标准版本",
    "version": "1.0"
  },
  "menus": [
    {
      "id": "file",
      "label": "文件",
      "items": [
        { "type": "action", "id": "file.new", "label": "新建", "command": "2d.new", "shortcut": "Ctrl+N" },
        { "type": "action", "id": "file.open", "label": "打开", "command": "2d.open", "shortcut": "Ctrl+O" },
        { "type": "separator" },
        { "type": "action", "id": "file.save", "label": "保存", "command": "2d.save", "shortcut": "Ctrl+S" },
        { "type": "separator" },
        { "type": "action", "id": "file.exit", "label": "退出", "command": "app.exit" }
      ]
    },
    {
      "id": "edit",
      "label": "编辑",
      "items": [
        { "type": "action", "id": "edit.undo", "label": "撤销", "command": "edit.undo", "shortcut": "Ctrl+Z" },
        { "type": "action", "id": "edit.redo", "label": "重做", "command": "edit.redo", "shortcut": "Ctrl+Y" }
      ]
    },
    {
      "id": "view",
      "label": "视图",
      "items": [
        {
          "type": "submenu",
          "id": "view.workbench",
          "label": "工作台",
          "items": [
            { "type": "action", "id": "wb.2d", "label": "2D 设计", "command": "workbench.switch.2d", "checkable": true },
            { "type": "action", "id": "wb.3d", "label": "3D 设计", "command": "workbench.switch.3d", "checkable": true }
          ]
        },
        { "type": "separator" },
        { "type": "action", "id": "theme.system", "label": "系统主题", "command": "theme.system", "checkable": true },
        { "type": "action", "id": "theme.light", "label": "浅色主题", "command": "theme.light", "checkable": true },
        { "type": "action", "id": "theme.dark", "label": "深色主题", "command": "theme.dark", "checkable": true }
      ]
    },
    {
      "id": "tools",
      "label": "工具",
      "items": [
        { "type": "action", "id": "tool.measure", "label": "测量", "command": "2d.measure" }
      ]
    }
  ],
  "toolbars": [
    {
      "id": "toolbar.main",
      "title": "主工具栏",
      "position": "top",
      "workbench": "global",
      "items": [
        { "type": "action", "id": "tool.move", "label": "移动", "icon": "move.svg", "command": "2d.move" },
        { "type": "action", "id": "tool.copy", "label": "复制", "icon": "copy.svg", "command": "2d.copy" },
        { "type": "action", "id": "tool.rotate", "label": "旋转", "icon": "rotate.svg", "command": "2d.rotate" },
        { "type": "separator" },
        { "type": "action", "id": "tool.delete", "label": "删除", "icon": "delete.svg", "command": "2d.delete" }
      ]
    },
    {
      "id": "toolbar.view",
      "title": "视图工具栏",
      "position": "top",
      "workbench": "2D",
      "items": [
        { "type": "action", "id": "view.zoom.extents", "label": "缩放至全屏", "icon": "zoom_extents.svg", "command": "view.zoom.extents" },
        { "type": "action", "id": "view.pan", "label": "平移", "icon": "pan.svg", "command": "view.pan" }
      ]
    }
  ],
  "docks": [
    {
      "id": "dock.layers",
      "title": "图层",
      "position": "left",
      "widgetType": "LayersPanel",
      "visible": true
    },
    {
      "id": "dock.properties",
      "title": "属性",
      "position": "right",
      "widgetType": "PropertiesPanel",
      "visible": true
    },
    {
      "id": "dock.command",
      "title": "命令",
      "position": "bottom",
      "widgetType": "CommandPanel",
      "visible": true
    }
  ],
  "shortcuts": [
    { "command": "2d.new", "keys": "Ctrl+N" },
    { "command": "2d.open", "keys": "Ctrl+O" },
    { "command": "2d.save", "keys": "Ctrl+S" },
    { "command": "edit.undo", "keys": "Ctrl+Z" },
    { "command": "edit.redo", "keys": "Ctrl+Y" },
    { "command": "2d.delete", "keys": "Delete" }
  ],
  "panels": {
    "register": [
      { "widgetType": "LayersPanel", "builtin": true },
      { "widgetType": "PropertiesPanel", "builtin": true },
      { "widgetType": "CommandPanel", "builtin": true }
    ]
  }
}
```

### 7.2 JSON Schema 结构

```
ui_config.json
├── meta                  # 元数据
│   ├── clientId         # 客户 ID
│   ├── clientName       # 客户名称
│   └── version          # 配置版本
├── menus[]              # 菜单列表
│   ├── id               # 菜单 ID
│   ├── label            # 显示文本
│   └── items[]          # 菜单项
│       ├── {type: "action"}     # 动作项
│       │   ├── id, label, command, shortcut, checkable
│       ├── {type: "separator"}  # 分隔符
│       └── {type: "submenu"}    # 子菜单（递归）
│           ├── id, label
│           └── items[]          # 递归
├── toolbars[]           # 工具栏列表
│   ├── id               # 工具栏 ID
│   ├── title            # 显示标题
│   ├── position         # 位置: "top" | "left" | "right" | "bottom"
│   ├── workbench        # 所属工作台: "global" | "2D" | "3D"
│   └── items[]          # 工具栏项
│       ├── {type: "action"}     # 工具按钮
│       └── {type: "separator"}  # 分隔符
├── docks[]              # Dock 面板列表
│   ├── id               # 面板 ID
│   ├── title            # 显示标题
│   ├── position         # 位置: "left" | "right" | "top" | "bottom"
│   ├── widgetType       # 面板类型（对应 UiPanelRegistry 注册的 ID）
│   └── visible          # 是否默认显示
├── shortcuts[]          # 快捷键列表
│   ├── command          # 命令 ID
│   └── keys             # 快捷键
└── panels               # 面板注册声明
    └── register[]       # 面板注册列表
        ├── widgetType   # 面板类型
        └── builtin      # 是否为内置面板
```

---

## 8. CMake 配置方案

### 8.1 客户配置目录

```
Main/Src/UI/ClientConfig/
├── UiClientConfigBase.h      # 数据结构定义（不变）
├── UiConfigLoader.h/.cpp     # JSON 加载器
├── UiConfigurationManager.h/.cpp  # 配置管理器
├── UiLayoutBuilder.h/.cpp    # 布局构建器
├── UiPanelRegistry.h/.cpp    # 面板工厂
├── configs/                  # JSON 配置（每个客户一个文件）
│   ├── san_yi.json
│   ├── client_a.json
│   ├── client_b.json
│   └── ...
└── resources/                # Qt 资源文件
    ├── configs.qrc.in        # 模板文件（CMake 配置时生成最终 .qrc）
    └── ... 
```

### 8.2 核心设计

CMake 在配置阶段根据 `SANYI_CLIENT_ID` **生成**对应的 `.qrc` 文件，只将选中客户的 JSON 嵌入到二进制中。这样：

- 对于客户 A 的版本，二进制中只包含 `client_a.json`，不含其他客户的配置
- 无需在 C++ 代码中出现任何 `#if` 分支
- 新增客户只需：新建 JSON 文件 + 在 CMake 列表中注册

### 8.3 根 CMakeLists.txt

```cmake
# 客户配置选项
set(SANYI_CLIENT_ID "san_yi" CACHE STRING "Target client ID")
set_property(CACHE SANYI_CLIENT_ID PROPERTY STRINGS
    "san_yi"      # 三益标准版本
    "client_a"    # 客户 A
    "client_b"    # 客户 B
    # ...
)

# 定义编译宏（用于运行时获取当前客户 ID）
add_compile_definitions(SANYI_CLIENT_ID="${SANYI_CLIENT_ID}")
```

### 8.4 Main/CMakeLists.txt

```cmake
# 客户配置相关源文件（所有客户共享）
set(CLIENT_CONFIG_SOURCES
    "${UI_SRC_DIR}/ClientConfig/UiClientConfigBase.h"
    "${UI_SRC_DIR}/ClientConfig/UiConfigLoader.h"
    "${UI_SRC_DIR}/ClientConfig/UiConfigLoader.cpp"
    "${UI_SRC_DIR}/ClientConfig/UiConfigurationManager.h"
    "${UI_SRC_DIR}/ClientConfig/UiConfigurationManager.cpp"
    "${UI_SRC_DIR}/ClientConfig/UiLayoutBuilder.h"
    "${UI_SRC_DIR}/ClientConfig/UiLayoutBuilder.cpp"
    "${UI_SRC_DIR}/ClientConfig/UiPanelRegistry.h"
    "${UI_SRC_DIR}/ClientConfig/UiPanelRegistry.cpp"
)

# 客户 JSON 配置文件列表
set(CLIENT_CONFIG_JSON
    "${UI_SRC_DIR}/ClientConfig/configs/${SANYI_CLIENT_ID}.json"
)

# 生成 .qrc 文件（仅包含当前客户的配置）
configure_file(
    "${UI_SRC_DIR}/ClientConfig/resources/configs.qrc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/configs_${SANYI_CLIENT_ID}.qrc"
    @ONLY
)

# 添加到 Qt 资源
set(CMAKE_AUTORCC ON)
qt_add_resources(${app_name} "CONFIGS"
    PREFIX "/configs"
    FILES "${UI_SRC_DIR}/ClientConfig/configs/${SANYI_CLIENT_ID}.json"
    BASE "${UI_SRC_DIR}/ClientConfig/configs"
)
```

### 8.5 configs.qrc.in 模板

```qrc
<!DOCTYPE RCC>
<RCC version="1.0">
    <qresource prefix="/configs">
        <file>@SANYI_CLIENT_ID@.json</file>
    </qresource>
</RCC>
```

实际上，使用 `qt_add_resources` 或 `qt_add_qml_module`（Qt 6.5+）更简洁，可以直接在 CMake 中嵌入资源：

```cmake
# Qt 6.5+ 推荐写法
qt_add_resources(${app_name} "APP_CONFIG"
    PREFIX "/configs"
    FILES
        "${UI_SRC_DIR}/ClientConfig/configs/${SANYI_CLIENT_ID}.json"
)
```

### 8.6 编译命令

```bash
# 默认编译（三益标准版本）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 编译客户 A 版本
cmake -S . -B build_client_a -DCMAKE_BUILD_TYPE=Release -DSANYI_CLIENT_ID=client_a
cmake --build build_client_a

# 编译客户 B 版本
cmake -S . -B build_client_b -DCMAKE_BUILD_TYPE=Release -DSANYI_CLIENT_ID=client_b
cmake --build build_client_b
```

---

## 9. 新增客户配置流程

### 9.1 添加新客户步骤

```
1. 创建 JSON 配置文件
   └─ 复制 san_yi.json → client_x.json，修改内容

2. 在 CMake 中注册客户
   └─ 根 CMakeLists.txt 的 STRINGS 列表添加 "client_x"

3. （可选）如果客户需要自定义面板（C++）
   └─ 实现面板类，在 UiConfigurationManager::registerCustomPanels() 中注册

4. 编译验证
   └─ cmake -DSANYI_CLIENT_ID=client_x ... && cmake --build .
```

### 9.2 无需修改的代码

- ❌ 不需要创建 C++ 类
- ❌ 不需要修改 `WorkbenchWindow.cpp`
- ❌ 不需要修改 `UiWorkbench.cpp`
- ❌ 不需要修改任何 CMake if/else 分支

### 9.3 新增客户 vs 修改菜单的对比

| 操作 | C++ 方案 | JSON 方案 |
|------|---------|----------|
| 新增一个客户(标准布局+改菜单) | 创建 2 个 C++ 文件 + CMake 分支 | 创建 1 个 JSON 文件 + CMake 加一行 |
| 修改某个客户的菜单项 | 修改 C++ -> 编译整个工程 | 修改 JSON -> 重编资源(秒级) |
| 新增一个面板类型 | 实现 C++ 面板类 + 注册到工厂 | 实现 C++ 面板类 + 注册到工厂(与JSON方案相同) |

---

## 10. 现有代码改造方案

### 10.1 WorkbenchWindow 改造

**改造前**：`buildMenus()`、`initializeToolBarSkeleton()`、`buildDockAreas()` 硬编码

**改造后**：

```cpp
// WorkbenchWindow.cpp 简化后的初始化
void WorkbenchWindow::initializeWorkbenchShell()
{
    // 1. 创建配置管理器（传入当前窗口和命令调度器）
    m_configManager = std::make_unique<UiConfigurationManager>(this, m_commandDispatcher);

    // 2. 加载并应用客户配置（从 Qt 资源加载，编译期嵌入）
    if (!m_configManager->applyConfiguration(":/configs/ui_config.json"))
    {
        // 配置加载失败，回退到默认布局
        buildDefaultFallbackUI();
        return;
    }

    // 3. 使用布局构建器创建 UI
    auto* data = m_configManager->configData();
    UiLayoutBuilder builder(this, m_commandDispatcher, m_configManager->panelRegistry());

    builder.buildMenus(data->menus);
    builder.buildToolBars(data->toolBars);
    builder.buildDocks(data->docks);
    builder.buildShortcuts(data->shortcuts);

    // 4. 创建视口
    setCentralWidget(createInitialCentralWidget());

    // 5. 绑定状态信号
    bindStateSignals();
    updateWindowTitle();
    refreshStatusText();
}
```

### 10.2 UiWorkbench 改造

Workbench 仍然负责工作台专属逻辑（如 2D/3D 切换），但工具栏定义来自 JSON 配置：

```cpp
void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    // 1. 从配置管理器中获取数据
    auto* configManager = window.configManager();
    auto* configData = configManager->configData();
    auto* panelRegistry = configManager->panelRegistry();

    // 2. 创建工作台专属的面板
    auto* sceneDock = createLayersDock(window);
    auto* drawToolBar = new DrawToolBarWidget(&window);
    auto* properties = new PropertiesPanelWidget(&window);
    auto* commandPanel = createPanelWidget(QObject::tr("Command panel"), &window);

    // 3. 在窗口上注册工作台专属控件
    window.registerDockWidget(QObject::tr("2D Draw Tools"), drawToolBar, Qt::LeftDockWidgetArea);
    window.registerDockWidget(QObject::tr("2D Properties"), properties, Qt::RightDockWidgetArea);
    window.registerDockWidget(QObject::tr("2D Command"), commandPanel, Qt::BottomDockWidgetArea);

    // 4. 从配置中过滤出 2D 工作台专属的工具栏
    UiLayoutBuilder builder(&window, m_services.commandDispatcher, panelRegistry);
    auto workbenchToolBars = filterToolBarsForWorkbench(configData->toolBars, QStringLiteral("2D"));
    builder.buildToolBars(workbenchToolBars);

    // 5. 创建视口...
}
```

### 10.3 自定义面板注册

如果一个客户需要额外的自定义面板（C++ 实现），在 `UiConfigurationManager` 中添加：

```cpp
// UiConfigurationManager::registerCustomPanels()
void UiConfigurationManager::registerCustomPanels(const UiConfigData& config)
{
    for (const auto& panelDef : config.docks)
    {
        // 如果面板是自定义类型，且不在内置注册表中，由客户配置注册
        if (panelDef.widgetType == "NestingParamPanel")
        {
            // 嵌套参数面板可能来自 Nesting 模块
            m_panelRegistry->registerPanel("NestingParamPanel",
                [](QWidget* parent) -> QWidget* {
                    return new NestingParamPanel(parent);
                });
        }
        else if (panelDef.widgetType == "MachineStatusPanel")
        {
            m_panelRegistry->registerPanel("MachineStatusPanel",
                [](QWidget* parent) -> QWidget* {
                    return new MachineStatusPanel(parent);
                });
        }
    }
}
```

---

## 11. 多客户定制场景示例

### 11.1 默认客户 SanYi — `configs/san_yi.json`

```json
{
  "meta": { "clientId": "san_yi", "clientName": "三益标准版" },
  "menus": [ /* 文件、编辑、绘图、图元操作、视图、工具 */ ],
  "toolbars": [ /* 主工具栏(顶部)、视图工具栏(顶部) */ ],
  "docks": [ /* 图层(左)、属性(右)、命令(底) */ ]
}
```

### 11.2 客户 A 精简版 — `configs/client_a.json`

```json
{
  "meta": { "clientId": "client_a", "clientName": "客户 A 精简版" },
  "menus": [
    { "id": "file", "label": "文件", "items": [ /* 仅保留打开、保存、退出 */ ] },
    { "id": "tools", "label": "工具", "items": [ /* 仅保留常用工具 */ ] }
  ],
  "toolbars": [
    { "id": "toolbar.main", "title": "常用工具", "position": "left",
      "workbench": "global", "items": [ /* 竖排，仅常用 5 个工具 */ ] }
  ],
  "docks": [
    { "id": "dock.properties", "title": "属性", "position": "right",
      "widgetType": "PropertiesPanel", "visible": true }
  ]
}
```

### 11.3 客户 B 专业版 — `configs/client_b.json`

```json
{
  "meta": { "clientId": "client_b", "clientName": "客户 B 专业版" },
  "menus": [
    { "id": "file", "label": "文件", "items": [ /* ... */ ] },
    { "id": "edit", "label": "编辑", "items": [ /* ... */ ] },
    { "id": "advanced", "label": "高级", "items": [
        { "type": "action", "id": "adv.nesting", "label": "排样", "command": "nesting.run" },
        { "type": "action", "id": "adv.measure", "label": "标注", "command": "measure.run" }
      ]
    }
  ],
  "toolbars": [
    { "id": "toolbar.main", "title": "主工具栏", "position": "top",
      "workbench": "global", "items": [ /* ... */ ] },
    { "id": "toolbar.measure", "title": "标注", "position": "top",
      "workbench": "global", "items": [ /* 标注相关工具 */ ] },
    { "id": "toolbar.nesting", "title": "排样", "position": "left",
      "workbench": "2D", "items": [ /* 排样相关工具 */ ] }
  ],
  "docks": [
    { "id": "dock.layers", "title": "图层", "position": "left",
      "widgetType": "LayersPanel", "visible": true },
    { "id": "dock.properties", "title": "属性", "position": "right",
      "widgetType": "PropertiesPanel", "visible": true },
    { "id": "dock.nesting", "title": "排样参数", "position": "right",
      "widgetType": "NestingParamPanel", "visible": false }
  ]
}
```

---

## 12. 架构变更影响

### 12.1 变更前

```
WorkbenchWindow::initializeWorkbenchShell()
    ├── buildMenus()              → 硬编码创建菜单
    ├── initializeToolBarSkeleton() → 硬编码创建工具栏
    ├── buildDockAreas()          → 硬编码创建 Dock
    └── bindShortcuts()           → 硬编码快捷键

Workbench2D::attachToWindow()
    ├── 硬编码创建工具栏          → QToolBar::addAction(...)
    ├── 硬编码创建 Dock           → QDockWidget::setWidget(...)
    └── 硬编码创建视口            → new Viewport2D(...)
```

### 12.2 变更后

```
WorkbenchWindow::initializeWorkbenchShell()
    ├── UiConfigurationManager::applyConfiguration()  → 加载编译期嵌入的 JSON
    │    └── UiConfigLoader::load(":/configs/ui_config.json")
    │         └── 解析 JSON → UiConfigData
    ├── UiLayoutBuilder::buildMenus(configData.menus)    → 从 JSON 数据构建菜单
    ├── UiLayoutBuilder::buildToolBars(configData.toolBars) → 从 JSON 数据构建工具栏
    ├── UiLayoutBuilder::buildDocks(configData.docks)    → 从 JSON 数据构建 Dock
    ├── UiLayoutBuilder::buildShortcuts(configData.shortcuts) → 从 JSON 数据构建快捷键
    └── panelRegistry.registerPanel(...)                 → 注册面板工厂

Workbench2D::attachToWindow()
    ├── 过滤工作台专属工具栏       → filterToolBarsForWorkbench(...)
    ├── UiLayoutBuilder::buildToolBars(...)              → 从 JSON 数据构建
    └── UiPanelRegistry::createPanel(...)                → 从 JSON 数据创建面板
```

---

## 13. UI 独立 DLL 分析

### 13.1 方案描述

将 UI 层（Main/Src/UI/ + UI/ 子模块）从主程序中拆分为独立的 DLL，主程序通过 DLL 接口与 UI 层交互。

```
┌─────────────────────────┐
│  SanYiCAD.exe            │
│  - main()                │
│  - Engine/Render/Utility │  ← 不依赖 UI DLL 的具体实现
│  - UI DLL 加载器          │
│  - IUiShellHost 接口      │
└────────┬────────────────┘
         │ LoadLibrary / QPluginLoader
┌────────▼────────────────┐
│  UI.dll                  │
│  - WorkbenchWindow       │  ← 只依赖 Engine/Render 的接口
│  - Workbench2D/3D        │
│  - UiLayoutBuilder       │
│  - PanelFactory           │
│  - 所有 Qt Widgets 代码   │
│  - JSON 配置加载          │
└─────────────────────────┘
```

### 13.2 接口定义

```cpp
// IUiShellHost.h — UI DLL 对外接口
class IUiShellHost
{
public:
    virtual ~IUiShellHost() = default;
    virtual bool initialize(UiServices* services) = 0;
    virtual int exec() = 0;
    virtual void shutdown() = 0;
};

// DLL 导出函数
extern "C" __declspec(dllexport) IUiShellHost* createUiShellHost();
```

### 13.3 优缺点评估

| 维度 | 说明 |
|------|------|
| **物理隔离** | UI 和 Core 完全独立编译，界面层无法绕过接口访问底层 |
| **独立开发** | UI 团队和 Engine 团队可独立开发、独立测试 |
| **版本独立** | UI DLL 可独立于主程序版本发布 |
| **DLL 爆炸** | 10+ 客户 = 10+ 个 UI DLL，管理成本高 |
| **调试成本** | 跨 DLL 调试增加复杂度，崩溃定位需要 PDB |
| **ABI 兼容** | 编译器版本、Qt 版本必须严格一致 |
| **链接复杂度** | 信号/槽跨 DLL 需要特殊处理（Q_OBJECT 宏导出） |

### 13.4 建议

**当前阶段不建议拆分 UI DLL**，原因：

1. **每个客户独立编译** — 本来就是每个客户一个完整二进制，拆 DLL 不能节省编译时间
2. **10+ UI DLL 管理复杂** — 每个客户需要维护匹配的 exe + DLL 版本对
3. **Qt 跨 DLL Q_OBJECT 陷阱多** — 信号/槽、元类型注册跨 DLL 容易出运行时错误
4. **调试成本增加** — 对于中小团队，跨 DLL 调试得不偿失
5. **UI 层本身已有 ClientConfig 物理隔离** — 配置文件和工厂模式已经解耦了 UI 变的部分

**如果未来需要拆分，前提条件**：

1. 存在独立的 UI 测试/设计团队，需要独立发布 UI 版本
2. 主程序需要支持多个不同的 UI 方案同时存在（如标准版 UI.dll 和触屏版 UI.dll）
3. 有充分的自动化测试覆盖跨 DLL 接口

**更务实的做法**：保持单一二进制，通过 CMake 的 `OBJECT` 库或 `STATIC` 库进行逻辑模块划分，同链接时只链接需要的 UI 模块。

---

## 14. 翻译策略

由于 UI 标签从 JSON 配置加载，而非 C++ 源码中的 `tr("text")`宏，Qt 的 `lupdate` 工具无法自动提取这些字符串，需要专门的方案处理。

### 14.1 方案选型

| 方案 | 说明 | 适用场景 |
|------|------|---------|
| A. Qt 原生 `tr()` | 在布局构建器中调用 tr() 翻译 | JSON 标签是英文/基础语言，运行时需要 Qt 翻译系统翻译为目标语言 |
| B. JSON 内置多语言 | JSON 中直接包含多语言字段 | 客户配置本身就是特定语言版本，不需要额外的翻译流程 |

### 14.2 方案 A：Qt `tr()` 翻译（推荐）

在 `UiConfigLoader` 解析 JSON 后，构建 UI 时通过 `QObject::tr()` 翻译：

```cpp
// UiLayoutBuilder.cpp
void UiLayoutBuilder::buildMenus(const std::vector<MenuDef>& menus)
{
    for (const auto& menu : menus)
    {
        // JSON 中的 label 作为翻译源文本
        auto* qMenu = m_window->menuBar()->addMenu(
            QObject::tr(menu.label.toUtf8().constData()));
        buildMenuItem(qMenu, menu.items);
    }
}
```

**提取策略**：`lupdate` 无法扫描 JSON，但可以扫描一个专门的 C++ 文件来提取所有可翻译字符串。推荐为每个客户创建一个翻译源文件，列出该客户 JSON 中所有的 label：

```cpp
// configs/san_yi_translatables.cpp
// 此文件不被编译，仅用于 lupdate 提取字符串

// clang-format off
static const auto* _ = {
    // menus
    QObject::tr("文件"), QObject::tr("编辑"), QObject::tr("视图"), QObject::tr("工具"),
    // menu items
    QObject::tr("新建"), QObject::tr("打开"), QObject::tr("保存"), QObject::tr("退出"),
    QObject::tr("撤销"), QObject::tr("重做"),
    QObject::tr("2D 设计"), QObject::tr("3D 设计"),
    QObject::tr("系统主题"), QObject::tr("浅色主题"), QObject::tr("深色主题"),
    // toolbar labels
    QObject::tr("移动"), QObject::tr("复制"), QObject::tr("旋转"), QObject::tr("删除"),
    QObject::tr("缩放至全屏"), QObject::tr("平移"),
    // dock titles
    QObject::tr("图层"), QObject::tr("属性"), QObject::tr("命令"),
};
```

这种 `.cpp` 文件只在 `lupdate` 执行时需要，不需要编译到目标中。可以在 CMake 中添加：

```cmake
# 仅用于 lupdate 提取翻译，不参与编译
set(TRANSLATABLE_SOURCES
    "${UI_SRC_DIR}/ClientConfig/configs/san_yi_translatables.cpp"
    "${UI_SRC_DIR}/ClientConfig/configs/client_a_translatables.cpp"
)
```

### 14.3 方案 B：JSON 内置多语言

如果客户配置本身就是特定语言（不需要运行时翻译），可以在 JSON 中包含多语言字段：

```json
{
  "id": "file",
  "label": {
    "zh_CN": "文件",
    "en_US": "File",
    "ja_JP": "ファイル"
  },
  "items": [...]
}
```

这种方式避免了 Qt 翻译系统的依赖，但 JSON 文件体积增大，且需要修改解析逻辑。

### 14.4 方案推荐

**推荐方案 A**（Qt `tr()` 翻译），理由：
- 与 Qt 的翻译生态兼容，可使用 Qt Linguist 进行翻译
- 翻译文件（.ts/.qm）独立于 JSON 配置，客户只需要维护一个 JSON
- 不改 JSON 也能支持多语言
- 不需要修改 `UiConfigLoader` 解析逻辑

---

## 15. 实施步骤

### 15.1 第一阶段：核心基础设施（1-2 周）

1. 创建 `Main/Src/UI/ClientConfig/` 目录结构
2. 实现 `UiClientConfigBase.h`（数据结构定义）
3. 实现 `UiConfigLoader.h/.cpp`（JSON 解析器）
4. 实现 `UiPanelRegistry.h/.cpp`（面板工厂）
5. 实现 `UiLayoutBuilder.h/.cpp`（布局构建器）
6. 实现 `UiConfigurationManager.h/.cpp`（配置管理器）
7. 更新 CMakeLists.txt（嵌入 JSON 资源）
8. 创建默认 `san_yi.json` 配置文件

### 15.2 第二阶段：迁移现有 UI（1-2 周）

1. 分析现有 `WorkbenchWindow::buildMenus()`，将菜单结构翻译为 JSON
2. 分析现有工具栏创建代码，翻译为 JSON
3. 分析现有 Dock 面板创建代码，翻译为 JSON
4. 更新 `WorkbenchWindow` 使用 `UiConfigurationManager`
5. 更新 `Workbench2D`/`Workbench3D` 使用 `UiLayoutBuilder`
6. 验证现有功能不受影响

### 15.3 第三阶段：客户定制（按需）

1. 为第一个实际客户创建 `client_a.json`
2. 添加该客户需要的自定义面板（C++ 实现 + 工厂注册）
3. 配置客户专属资源（图标、主题）
4. 编译验证并交付

### 15.4 第四阶段：完善（持续）

1. 为每个新客户创建 JSON 配置
2. 添加配置验证单元测试（加载 JSON → 解析 → 校验字段）
3. 添加配置迁移工具（JSON 版本升级）
4. 编写内部文档《客户配置开发指南》

---

## 16. 方案评估

### 16.1 优点

| 维度 | 说明 |
|------|------|
| **不重新编译换 UI** | 改 UI 布局只需改 JSON，不需要改 C++。二进制仍然需要重新生成，但编译时间大幅减少（只重编资源） |
| **新建客户快** | 10+ 客户 = 10+ JSON 文件，不需要任何 C++ 编程经验即可创建客户配置 |
| **CMake 简洁** | 不需要为每个客户加 elseif 分支，只需维护一个列表 |
| **单点入口** | `UiConfigurationManager` 是所有客户配置的总控点，新增/修改/排障都经过这里 |
| **面板工厂解耦** | 自定义面板 C++ 实现不受 JSON 影响，通过工厂注册即可。面板复用性好 |
| **数据与代码分离** | JSON 是纯数据，可通过 schema 校验，冲突合并友好 |
| **编译安全** | JSON 解析错误不会导致编译失败（但可通过单元测试捕获） |

### 16.2 缺点

| 维度 | 说明 |
|------|------|
| 运行时解析开销 | JSON 解析一次，微小开销（可忽略） |
| 类型安全性 | 字段名错误、类型错误在运行时发现（需要单元测试覆盖） |
| JSON 文件管理 | 需要维护多套 JSON 文件，版本管理需要留意 |
| 复杂逻辑仍靠 C++ | 条件性 UI、动态面板等仍需要 C++ 代码 |

### 16.3 与 C++ 编译期方案对比

| 维度 | JSON 嵌入方案（本方案） | C++ 编译期方案 |
|------|----------------------|---------------|
| 客户配置形式 | 每个客户 = 1 个 JSON 文件 | 每个客户 = 2 个 C++ 文件 |
| 新增客户 CMake 改动 | 列表加 1 行 | 加 elseif 分支 + 文件列表 |
| 改 UI 布局 | 改 JSON，重编资源(秒级) | 改 C++，完整编译 |
| 10+ 客户 CMake 维护 | 简单（列表不长） | 繁琐（分支多） |
| 非 C++ 维护能力 | 懂 JSON 即可 | 必须 C++ 开发者 |
| 类型安全 | 解析时验证 | 编译期检查 |
| 配置可测试性 | 独立 JSON 加载测试 | 需要链接整个 UI 库 |
| 引用外部资源(图标等) | JSON 直接引用路径 | C++ 硬编码 QRC |
| 配置版本化（diff/merge） | 纯 JSON diff，友好 | C++ diff，噪音多 |

### 16.4 适用场景

推荐本方案：
- **10+ 客户**需要独立维护 UI 布局
- 开发团队想减少"改菜单就重编"的等待时间
- 需要非 C++ 人员（配置工程师）参与客户配置
- UI 布局频繁调整（原型阶段、客户验收阶段）

仍可选 C++ 方案：
- 只有 2-3 个客户，且都是开发者维护
- 极度在意那一次 JSON 解析的微小开销（嵌入式等场景）
- 开发者想保持"一切皆代码"的一致性

---

## 17. 总结

### 17.1 核心设计思想

**编译期嵌入 JSON 配置 + 数据驱动 UI 构建**：
- JSON 配置文件定义 UI 布局（菜单/工具栏/Dock/快捷键）
- CMake 选择当前客户对应的 JSON，通过 Qt 资源在编译期嵌入二进制
- 运行时 `UiConfigLoader` 解析 JSON → `UiClientConfigBase` 数据结构
- `UiLayoutBuilder` 根据数据构建实际的 Qt Widgets
- `UiPanelRegistry` 提供面板工厂注册能力，支持自定义面板扩展
- `UiConfigurationManager` 作为多客户配置的总控点

### 17.2 关键组件

| 组件 | 职责 | 文中的位置 |
|------|------|-----------|
| `UiClientConfigBase` | 配置数据结构（MenuDef, ToolBarDef, DockDef） | §6.1 |
| `UiConfigLoader` | JSON 解析器，将资源中的配置加载为 C++ 数据结构 | §6.2 |
| `UiConfigurationManager` | 多客户配置总控点，持有面板工厂和配置数据 | §6.3 |
| `UiLayoutBuilder` | 根据数据构建 Qt UI 组件（菜单/工具栏/Dock/快捷键） | §6.4 |
| `UiPanelRegistry` | 面板工厂注册表，支持运行时创建自定义面板 | §6.5 |
| JSON 配置文件 | 客户 UI 布局的纯数据定义 | §7 |

### 17.3 新增客户步骤

```
1. 创建 JSON 配置文件  → configs/client_x.json
2. 在 CMake 列表注册    → STRINGS 列表添加 "client_x"
3. 编译                → cmake -DSANYI_CLIENT_ID=client_x
4. 交付                → 每个客户一个独立版本的二进制
```

### 17.4 编译方式

```bash
# 默认编译（三益标准版本）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 编译特定客户版本
cmake -S . -B build_client_x -DCMAKE_BUILD_TYPE=Release -DSANYI_CLIENT_ID=client_x
cmake --build build_client_x
```

### 17.5 关于 UI DLL 的建议

**当前阶段不建议拆分 UI DLL**，保持单一二进制。通过 JSON 配置和面板工厂模式已经实现了 UI 定制的灵活性。如果未来确实需要独立发布 UI 版本（如触屏版 UI、VR 版 UI），再考虑拆分。

此方案在保持 Engine/Render/Utility 层不变的前提下，通过**编译期嵌入的 JSON 配置** + **面板工厂**实现了灵活的 UI 定制，特别适合 10 个以上客户独立版本管理的场景。

---

## 18. 补充设计考量

### 18.1 JSON 配置继承机制

**问题**：10+ 客户中，80% 的菜单/工具栏是共通的，只有 20% 的差异。如果每个客户都写一个完整的 JSON，维护负担很大。

**方案**：引入 `"extends"` 字段，让一个 JSON 继承另一个，并覆盖或追加特定部分。

```
configs/
├── base.json              # 基础配置（所有客户的公共部分）
├── san_yi.json            # extends: base  → 覆盖或追加客户定制
├── client_a.json          # extends: base  → 只写差异部分
├── client_b.json          # extends: base  → 只写差异部分
├── client_c.json          # extends: client_b  → 链式继承
```

**示例**：`client_a.json`

```json
{
  "meta": {
    "clientId": "client_a",
    "clientName": "客户 A"
  },
  "extends": "base",
  "menus": [
    // 只定义需要追加或覆盖的菜单
    // 如果同 id 则覆盖，否则追加
  ],
  "toolbars": [
    // 只定义差异部分
  ],
  "docks": [
    // 只定义差异部分
  ],
  "panels": {
    "register": [
      { "widgetType": "CustomPanelA", "builtin": false }
    ]
  }
}
```

**继承规则**：

| 规则 | 说明 |
|------|------|
| 同级 id 覆盖 | 子配置中如果出现与父配置相同 id 的菜单/工具栏/Dock，则用子配置的完整定义替换 |
| 追加 | 子配置中新增 id 的元素，追加到父配置的对应列表中 |
| 删除 | 子配置中 `"action": "remove"` 标记的元素，从父配置中移除 |
| 链式继承 | 支持多层继承（A extends B, B extends C） |

**在 UiConfigLoader 中的实现**：

```
UiConfigLoader::load(resourcePath)
  ├─ 解析当前 JSON
  ├─ 如果存在 "extends" 字段
  │    ├─ 加载父配置 JSON
  │    ├─ 递归合并（父配置的菜单 + 子配置的覆盖/追加）
  │    └─ 返回合并后的完整配置
  └─ 返回最终配置
```

**注意**：由于 JSON 在编译期嵌入，继承关系在编译时就已确定，不存在运行时动态查找父配置的问题。基类配置文件也在同一个 `.qrc` 中。

### 18.2 配置验证策略

**问题**：JSON 配置没有 C++ 编译器的类型检查，字段拼写错误、类型错误只能在运行时发现。

**方案**：分三层验证。

| 验证层 | 时机 | 手段 | 捕获的问题 |
|--------|------|------|-----------|
| **语法验证** | CI / 提交前 | `QJsonDocument::fromJson()` 基本解析 | JSON 格式错误、非法字符 |
| **Schema 验证** | CI / 单元测试 | Qt 的 `QJsonDocument` + 自定义校验，或第三方 JSON Schema 库 | 字段缺失、类型不对、枚举值非法 |
| **运行时验证** | 应用启动时 | `UiConfigLoader::load()` 返回 `std::optional` 或抛异常 | 资源缺失、值不符合业务逻辑 |

**Schema 验证示例**（在单元测试中）：

```cpp
// Test_UiConfigLoader.cpp
TEST(UiConfigLoaderTest, ValidateSanYiConfig)
{
    auto loader = UiConfigLoader(":/configs/ui_config.json");
    auto result = loader.load();
    ASSERT_TRUE(result.has_value());

    // 验证必填字段
    EXPECT_FALSE(result->clientId.isEmpty());
    EXPECT_GT(result->menus.size(), 0);
    EXPECT_GT(result->toolBars.size(), 0);

    // 验证所有 commandId 都在注册表中
    for (const auto& menu : result->menus)
        validateCommandIds(menu);

    // 验证 widgetType 都在面板工厂中注册
    for (const auto& dock : result->docks)
        EXPECT_TRUE(m_panelRegistry->isPanelRegistered(dock.widgetType));
}
```

**CI 集成建议**：

```
每次提交 JSON 配置变更时：
  1. 语法验证       → 每个 JSON 文件都能被 QJsonDocument 正确解析
  2. Schema 验证    → 字段结构符合预期
  3. 模拟加载验证    → 在测试环境中加载 JSON，验证合并后的完整配置
  4. Command ID 验证 → 确保所有 commandId 都在命令调度器中注册
```

### 18.3 错误处理与优雅降级

**问题**：配置加载或面板创建失败时，程序不能崩溃，需要有合理的降级策略。

**降级策略**（优先级从高到低）：

```
UiConfigurationManager::applyConfiguration()
  ├─ 尝试加载客户专属 JSON
  │    ├─ 成功 → 正常启动
  │    └─ 失败 → 尝试加载 "san_yi.json" 作为回退
  │         ├─ 成功 → 使用 san_yi 配置，告警日志
  │         └─ 失败 → 调用 buildDefaultFallbackUI()
  │              └─ 硬编码创建最基本 UI（菜单只有文件/退出）
  │
  ├─ 面板创建时
  │    ├─ 面板工厂中有注册 → 正常创建
  │    └─ 面板工厂中未注册 → QLabel("未找到面板: xxx") 占位，告警日志
  │
  └─ 命令绑定失败时
       ├─ commandId 在调度器中有注册 → 正常绑定
       └─ commandId 未注册 → 按钮 disabled + 工具提示 "功能未启用"
```

**实现要点**：

- `UiConfigLoader::load()` 返回 `std::optional<UiConfigData>`，失败时不抛异常
- `UiPanelRegistry::createPanel()` 返回 `QWidget*`，失败返回 `nullptr`
- `UiConfigurationManager` 在构造时接收一个 `FallbackPolicy` 枚举：

```cpp
enum class ConfigFallbackPolicy
{
    Strict,    // 加载失败直接终止（开发阶段）
    Fallback,  // 加载失败回退到 san_yi 配置（生产阶段，推荐）
    Silent     // 加载失败使用硬编码基本 UI（极端情况）
};
```

### 18.4 客户专属资源隔离

**问题**：不同客户的图标、主题样式、翻译文件需要分开管理。

**目录结构**：

```
Main/resources/
├── clients/
│   ├── san_yi/
│   │   ├── icons/
│   │   │   ├── move.svg
│   │   │   ├── copy.svg
│   │   │   └── ...
│   │   ├── styles/
│   │   │   ├── light.qss
│   │   │   └── dark.qss
│   │   └── translations/
│   │       └── SanYiCAD_zh.ts
│   ├── client_a/
│   │   ├── icons/
│   │   ├── styles/
│   │   └── translations/
│   └── client_b/
│       └── ...
├── common/                    # 所有客户共用的资源
│   ├── icons/
│   └── styles/
```

**CMake 选择策略**：

```cmake
# 所有客户共用的资源
qt_add_resources(${app_name} "COMMON_RES"
    PREFIX "/resources/common"
    FILES
        ${COMMON_ICONS}
        ${COMMON_STYLES}
)

# 客户专属资源（根据 SANYI_CLIENT_ID 选择）
qt_add_resources(${app_name} "CLIENT_RES"
    PREFIX "/resources/client"
    FILES
        ${CLIENT_ICONS_${SANYI_CLIENT_ID}}
        ${CLIENT_STYLES_${SANYI_CLIENT_ID}}
)
```

**JSON 配置中的资源引用**：

```json
{
  "id": "tool.move",
  "label": "移动",
  "icon": "move.svg",
  "command": "2d.move"
}
```

`UiLayoutBuilder` 加载图标时，先查找 `/resources/client/icons/move.svg`，找不到则回退到 `/resources/common/icons/move.svg`。

> **✅ 已落地（2026-08-10）**：上述「客户/皮肤资源隔离」在图标维度已由 **`IconHelper` 多主题图标机制** 实现，思路一致但落地于 UICommon：
>
> - **存储**：`UI/Common/Resources/Icons/` 为默认集，`Icons/<flavor>/`（`light` / `dark` / `highcontrast`）为皮肤覆盖集，qrc 统一注册。
> - **解析顺序**（`IconHelper::resolveIconResource`）：用户自定义目录（文件系统）→ `Icons/<flavor>/<相对路径>` → 默认集。
> - **语义色令牌**：SVG 内用 `currentColor` / `%bg%` / `%accent%` 等令牌，渲染时按当前主题替换（`ThemeManager::colorTokens()`），同一张图适配所有皮肤。
> - **用户定制**：`IconHelper::setUserIconDirectory()` 运行期覆盖，无需重编译。
> - **皮肤新增**：`ThemeManager::iconFlavorFor(AppTheme)` 决定覆盖集归属；新增皮肤 = 枚举 + 令牌色 + QSS + flavor 映射。
>
> 详细用法见 `Docs/01-当前架构/UI定制.md` 3.4 图标。

### 18.5 Workbench 级别的配置分离

**问题**：当前一个 JSON 文件包含所有 workbench（2D 和 3D）的配置。如果某个客户只需要 2D 工作台，或者 2D 和 3D 配置差异很大，一个文件会变得庞大。

**可选方案**：将 JSON 按 workbench 拆分。

```
configs/
├── san_yi/
│   ├── common.json        # 跨 workbench 共享（全局菜单、通用工具栏）
│   ├── workbench_2d.json  # 2D 专属
│   └── workbench_3d.json  # 3D 专属
├── client_a/
│   ├── common.json
│   ├── workbench_2d.json
│   └── workbench_3d.json  # 可选（如果客户只需 2D）
```

**在 Qt 资源中的组织**：

```cmake
qt_add_resources(${app_name} "CONFIGS"
    PREFIX "/configs"
    FILES
        "${UI_SRC_DIR}/ClientConfig/configs/${SANYI_CLIENT_ID}/common.json"
        "${UI_SRC_DIR}/ClientConfig/configs/${SANYI_CLIENT_ID}/workbench_2d.json"
        # 可选：${UI_SRC_DIR}/.../workbench_3d.json"
)
```

**启动时加载**：

```cpp
void WorkbenchWindow::initializeWorkbenchShell()
{
    // 加载通用配置（全局菜单、通用工具栏等）
    m_configManager->applyConfiguration(":/configs/common.json");

    // Workbench 切换时加载 workbench 专属配置
    // 这部分由 UiWorkbench 的子类在 attachToWindow 中完成
}
```

### 18.6 CI/CD 构建策略（10+ 客户）

**问题**：10+ 客户意味着 10+ 个构建任务，如何管理？

**推荐策略**：

```yaml
# 伪代码：CI 构建矩阵
matrix:
  include:
    - client: san_yi
      cmake_args: -DSANYI_CLIENT_ID=san_yi
    - client: client_a
      cmake_args: -DSANYI_CLIENT_ID=client_a
    - client: client_b
      cmake_args: -DSANYI_CLIENT_ID=client_b
    # ... 10+ 个客户

steps:
  - cmake ${cmake_args} -S . -B build_${client}
  - cmake --build build_${client}
  - ctest --test-dir build_${client}
  # 打包：输出 SanYiCAD_${client}_v${version}.exe
```

**减少构建时间的策略**：

| 策略 | 说明 |
|------|------|
| **ccache** | 缓存 C++ 编译产物，不同客户间共享公共部分的编译结果 |
| **分层构建** | Engine/Render/Utility 作为静态库，只有 UI 层和主程序按客户构建 |
| **增量构建** | 不改 C++ 只改 JSON 时，只触发资源编译和链接（秒级） |

---

## 19. 迁移策略

### 19.1 原则

不能"一刀切"式迁移，旧硬编码和新 JSON 配置需要能够并存，逐步替换。

### 19.2 分步迁移路线

```
阶段 1：基础设施并行（第 1-2 周）
  ├─ 实现 UiConfigLoader、UiLayoutBuilder、UiPanelRegistry
  ├─ 实现 UiConfigurationManager
  ├─ 创建第一个 JSON 配置文件（完全复制现有硬编码布局）
  ├─ 新增一个编译开关 SANYI_ENABLE_CONFIG_DRIVEN_UI=ON/OFF
  ├─ WorkbenchWindow 中：
  │     if (config-driven enabled)
  │         UiConfigurationManager::applyConfiguration(...)
  │     else
  │         buildMenus()  ← 旧的硬编码逻辑
  │         buildToolBars()
  │         buildDocks()
  └─ 此时：开关 OFF 行为不变；开关 ON 应该产生完全相同的 UI

阶段 2：按组件替换（第 3-4 周）
  ├─ 菜单层：先从 JSON 加载菜单（保留硬编码工具栏/Dock）
  ├─ 确认菜单正确后 → 工具栏层：从 JSON 加载
  ├─ 确认工具栏正确后 → Dock 层：从 JSON 加载
  └─ 每个阶段都允许新旧并行，方便对比验证

阶段 3：清理（第 5 周）
  ├─ 移除 buildMenus()、buildToolBars()、buildDockAreas() 等硬编码方法
  ├─ 移除 SANYI_ENABLE_CONFIG_DRIVEN_UI 编译开关（永久启用 JSON 驱动）
  └─ 旧代码备份到 git 历史中，随时可回退
```

### 19.3 风险控制

| 风险 | 缓解措施 |
|------|---------|
| JSON 解析结果与硬编码不一致 | 阶段 1 写自动化对比测试：解析 JSON 生成的数据结构 vs 硬编码数据逐个字段对比（菜单项数量、标签、命令 ID） |
| 某个客户 JSON 迁移中断 | 每个客户独立迁移，互不影响。回退只需改 CMake 的 `SANYI_CLIENT_ID` |
| 自定义面板（DrawToolBarWidget 等）无法通过 JSON 描述 | 这些面板保留 C++ 实现，通过 `UiPanelRegistry` 注册。JSON 中通过 `widgetType` 引用。无需迁移到 JSON |
| 旧功能回归 | 每个迁移阶段跑全量自动化测试 + 人工走查 |

### 19.4 对比测试（确保新旧一致性）

```cpp
// Test_ConfigMigration.cpp
// 验证 JSON 配置解析结果与当前硬编码行为一致

TEST(ConfigMigrationTest, MenuConsistency)
{
    // 1. 配置驱动方式
    UiConfigLoader loader(":/configs/ui_config.json");
    auto configData = loader.load();
    ASSERT_TRUE(configData.has_value());

    // 2. 硬编码方式 — 直接用原有函数生成参考数据
    auto expectedMenus = buildHardcodedMenusForTest();

    // 3. 逐项对比
    ASSERT_EQ(configData->menus.size(), expectedMenus.size());
    for (size_t i = 0; i < configData->menus.size(); i++)
    {
        EXPECT_EQ(configData->menus[i].id, expectedMenus[i].id);
        EXPECT_EQ(configData->menus[i].label, expectedMenus[i].label);
        EXPECT_EQ(configData->menus[i].items.size(), expectedMenus[i].items.size());
    }
}
```

---

## 20. 命令 ID 治理

### 20.1 问题

JSON 配置中通过 `command` 字段引用命令 ID（如 `"2d.move"`），但这些 ID 在 C++ 代码中通过 `OperationBus` 注册。二者缺少一个"注册表"来保证一致性。

### 20.2 命令注册表

**方案**：让 `OperationBus` 提供运行时查询能力，同时生成一个头文件供编译期校验。

```cpp
// OperationBus.h 扩展
class OperationBus
{
public:
    // ... 原有方法 ...

    /// 返回所有已注册的命令 ID 列表
    QStringList registeredCommandIds() const;

    /// 检查命令是否已注册
    bool isCommandRegistered(const QString& commandId) const;

    /// 添加命令别名（某些客户的 JSON 使用了不同命名）
    void addCommandAlias(const QString& alias, const QString& actualCommandId);
};
```

### 20.3 CI 验证

```
验证流程（每次提交 JSON 变更时）：
  1. 编译带所有命令注册的测试目标
  2. 加载 JSON 配置
  3. 递归遍历所有菜单、工具栏，提取所有 command 字段
  4. 检查每个 command 是否在 dispatcher 的 registeredCommandIds() 中
  5. 如果发现未注册的命令 ID → 报错并列出
```

### 20.4 命令 ID 命名规范

建议统一命令 ID 的命名规范，减少混淆：

| 规范 | 示例 |
|------|------|
| 2D 命令前缀 | `2d.line`、`2d.circle`、`2d.move` |
| 3D 命令前缀 | `3d.orbit`、`3d.select`、`3d.measure` |
| 编辑命令前缀 | `edit.undo`、`edit.redo`、`edit.delete` |
| 视图命令前缀 | `view.zoom.in`、`view.zoom.out`、`view.pan` |
| 工作台切换 | `workbench.switch.2d`、`workbench.switch.3d` |
| 主题命令前缀 | `theme.light`、`theme.dark`、`theme.system` |
| 应用级命令前缀 | `app.exit`、`app.about`、`app.settings` |
| 模块特有前缀 | `nesting.run`、`fileio.import`、`fileio.export` |

### 20.5 `unknownCommand` 兜底策略

对于 JSON 中引用了但 dispatcher 中未注册的命令：

```cpp
// UiLayoutBuilder::buildMenuItem() 中
void bindAction(QAction* action, const QString& commandId)
{
    if (m_dispatcher->isCommandRegistered(commandId))
    {
        connect(action, &QAction::triggered, this, [this, commandId]() {
            m_dispatcher->dispatch(commandId);
        });
    }
    else
    {
        // 命令不存在 → 按钮 disabled + 可选的工具提示
        action->setEnabled(false);
        action->setToolTip(QObject::tr("功能未启用 (command: %1)").arg(commandId));
    }
}
```

---

## 21. JSON 配置与 C++ 代码的分界线

### 21.1 该放 JSON 的

| 项目 | 原因 |
|------|------|
| 菜单结构（菜单项、子菜单、分隔符） | 纯静态结构，无运行时逻辑 |
| 工具栏内容（工具按钮、图标、命令 ID） | 纯静态配置 |
| Dock 面板列表（位置、类型、默认可见性） | 面板类型由工厂创建，JSON 只声明"放什么面板" |
| 快捷键绑定 | 命令 ID 到快捷键的映射 |
| 图标引用 | 图标路径、回退策略 |
| Workbench 与 UI 组件的归属关系 | 哪个工具栏属于哪个 workbench |
| 许可/功能开关标记 | 哪些功能需要特定许可才显示 |

### 21.2 该留 C++ 代码的

| 项目 | 原因 |
|------|------|
| 面板的具体实现（LayersPanel、PropertiesPanel 等） | 含 Qt 信号/槽、业务逻辑、数据绑定 |
| Context Menu（右键菜单） | 依赖运行时选择状态，无法静态描述 |
| 命令处理器的注册（`registerCommands()`） | 每个命令的 activate/deactivate、undo/redo 逻辑 |
| 动态 UI 行为（根据文档状态启用/禁用菜单） | 依赖运行时状态，通过 `UiStateCenter` 信号处理 |
| Workbench 切换逻辑 | 2D ↔ 3D 切换涉及状态保存/恢复、view 重建 |
| 自定义 Widget（DrawToolBarWidget 等） | 含绘图交互逻辑、鼠标事件处理 |
| 主题切换逻辑 | System/Light/Dark/Blue 切换的 C++ 逻辑 |
| 配置加载时机与降级策略 | 应用启动时的初始化顺序、异常处理 |

### 21.3 灰色地带（视情况而定）

| 项目 | 建议 |
|------|------|
| 菜单项的启用/禁用状态 | 初始状态可配，运行时状态由代码驱动 |
| 面板的默认大小/比例 | 可在 JSON 中配 `initialWidth`/`initialHeight`，但最终由布局管理器决定 |
| 工具栏的图标大小 | 可在 JSON 中配 `iconSize`，也可交由 QStyle 决定 |
| 快捷键冲突检测 | 代码层面做（启动时遍历所有快捷键，检测冲突） |

---

## 22. 许可与功能开关

### 22.1 问题

不同客户购买的功能模块不同。JSON 配置可以定义 UI 布局，但某些菜单/工具栏只能在客户购买了对应许可后才显示或启用。

### 22.2 方案

在 JSON 配置中为需要许可控制的项目添加 `feature` 标记：

```json
{
  "menus": [
    {
      "id": "advanced",
      "label": "高级",
      "items": [
        {
          "type": "action",
          "id": "adv.nesting",
          "label": "排样",
          "command": "nesting.run",
          "feature": "NESTING"    ← 需要 NESTING 许可
        },
        {
          "type": "action",
          "id": "adv.engrave",
          "label": "激光雕刻",
          "command": "engrave.run",
          "feature": "ENGRAVE"    ← 需要 ENGRAVE 许可
        }
      ]
    }
  ],
  "toolbars": [
    {
      "id": "toolbar.nesting",
      "title": "排样",
      "position": "top",
      "feature": "NESTING",      ← 整个工具栏需要 NESTING 许可
      "items": [ ... ]
    }
  ]
}
```

### 22.3 运行时处理

```cpp
// UiLayoutBuilder.cpp
void UiLayoutBuilder::buildMenuItem(QMenu* parent, const MenuItemDef& item)
{
    // 如果菜单项指定了 feature，检查许可
    if (!item.feature.isEmpty() && !LicenseManager::hasFeature(item.feature))
    {
        return;  // 不添加这个菜单项
    }

    // 正常创建菜单项 ...
}

void UiLayoutBuilder::buildToolBars(const std::vector<ToolBarDef>& toolBars)
{
    for (const auto& tb : toolBars)
    {
        // 如果工具栏指定了 feature，检查许可
        if (!tb.feature.isEmpty() && !LicenseManager::hasFeature(tb.feature))
        {
            continue;  // 不创建这个工具栏
        }

        // 正常创建工具栏 ...
    }
}
```

### 22.4 数据结构扩展

```cpp
// 在 MenuActionDef / ToolBarDef / DockDef 中添加可选字段
struct MenuActionDef
{
    QString id;
    QString label;
    QString commandId;
    QString shortcut;
    QString feature;       // 可选：需要的许可功能 ID
    bool checkable = false;
};
```

---

## 23. 调试与诊断工具

### 23.1 开发期诊断

**配置转储模式**：启动时加命令行参数 `--dump-config`，将加载并合并后的完整配置输出到控制台或文件。

```cpp
// main.cpp
int main(int argc, char* argv[])
{
    // ...
    if (QCoreApplication::arguments().contains("--dump-config"))
    {
        auto loader = UiConfigLoader(":/configs/ui_config.json");
        auto config = loader.load();
        if (config)
        {
            QFile file("ui_config_dump.json");
            file.open(QIODevice::WriteOnly);
            file.write(serializeConfigToJson(*config));
            file.close();
            qDebug() << "Config dumped to ui_config_dump.json";
        }
        return 0;
    }
    // ...
}
```

**可视化配置树**：开发工具窗口显示当前生效的配置树，与实际的 Qt Widget 树对照。

### 23.2 运行时诊断

**配置热刷新日志**：每次加载配置时输出：

```
[Config] Loading config for client: "san_yi"
[Config]   menus: 4 top-level, 23 actions, 3 submenus
[Config]   toolbars: 3 (2 top, 1 left)
[Config]   docks: 3 (1 left, 1 right, 1 bottom)
[Config]   shortcuts: 12
[Config]   panels registered: 3 (LayersPanel, PropertiesPanel, CommandPanel)
[Config] Loading completed in 2ms
```

### 23.3 配置验证独立工具

一个脱离主程序的独立命令行工具，专门用于验证 JSON 配置：

```bash
# 验证所有客户配置
> validate_configs.exe --config-dir=configs/
san_yi.json    → PASS
client_a.json  → FAIL: unknown command "2d.nesting" at menus[3].items[0].command
client_b.json  → PASS
client_c.json  → PASS

# 验证单个客户配置的完整合并
> validate_configs.exe --config=san_yi.json --base=base.json
PASS: merged config has 4 menus, 3 toolbars, 3 docks
```

---

## 24. 开发工作流

### 24.1 本地迭代

```bash
# 1. 修改 san_yi.json 中的菜单项

# 2. 快速验证解析正确性（不需要完整编译）
> validate_configs.exe --config=configs/san_yi.json

# 3. 编译（只触发资源重打包，秒级）
> cmake --build build --target SanYiCAD

# 4. 运行验证
> ./build/SanYiCAD.exe --dump-config  # 检查合并后的完整配置
> ./build/SanYiCAD.exe                 # 实际启动
```

### 24.2 多客户迭代

```bash
# 批量验证所有客户的配置（无需编译）
> validate_configs.exe --config-dir=configs/

# 单独构建并测试某个客户
> cmake -S . -B build_a -DSANYI_CLIENT_ID=client_a
> cmake --build build_a
> ./build_a/SanYiCAD.exe --client-id=client_a  # 启动时显示当前客户 ID
```

### 24.3 版本管理建议

```
configs/ 目录纳入 git，JSON 配置变更走正常 Code Review 流程。

特别注意 diff 的可读性：
  - 每个配置项独立一行，不折叠
  - 为 arrays 使用标准缩进（2 空格）
  - CI 中加 JSON 格式化检查（clang-format 对 JSON 的支持，或使用 prettier）
```