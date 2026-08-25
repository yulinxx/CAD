# 用户界面定制开发指南

> **面向读者**：后续开发人员 / 客户化工程师
> **目标**：教你在 **不改动核心算法/渲染代码**的前提下，完成 *不同客户不同的 UI 布局 / 菜单 / 工具栏 / 面板* 定制。

本文档是 `Docs/耦合性分析.md` 任务 E（布局配置化）的执行版。阅读前先理解整个架构的约定：

- UI 边界 ≈ **配置 + 注册表 + 构建器**，不负责业务语义。
- 业务语义入口唯一：`OperationBus::run(OperationId)`。
- **配置驱动是唯一的 UI 构建路径**。菜单 / 工具栏 / 停靠面板 / 状态栏 / 右键菜单
  全部由客户 JSON 生成，硬编码回退分支已删除。

> **2026-08-25 起的重要变更（P0 收口）**
>
> 1. **客户 ID 改为运行时解析**：编译期宏 `SANYI_CLIENT_ID` 已废除。
>    客户 ID 由 `UiClientContext` 在运行时解析，**一份二进制可服务多个客户**。
> 2. **`SANYI_ENABLE_CONFIG_DRIVEN_UI` 已废弃**：配置驱动是唯一路径，
>    `WorkbenchLayoutManager` 中的 `#ifdef` 与硬编码 Dock 骨架已删除。
>    不要再新增该宏的分支。
> 3. **状态栏、右键菜单、3D 菜单纳入同一套 JSON**：`MenuManager3D` 硬编码路径已移除。
> 4. **License feature gating 已接线**：JSON 中的 `feature` 字段现在真正生效。

---

## 1. 定制能力边界（先念清）

### 1.1 可以定制的
| 维度 | 机制 | 示例 |
|------|------|------|
| 客户版本选择 | **运行时** `UiClientContext`（环境变量 / QSettings） | `san_yi` / `client_a` / `client_b` |
| 顶层菜单 / 子菜单 | `configs/<client>.json` → `menus[]` | 增删/排序/改名/换图标 |
| 工具栏 | `configs/<client>.json` → `toolbars[]` | 顶/左/右/底工具栏及其按钮 |
| 停靠面板骨架 | `configs/<client>.json` → `docks[]` | 增删/换位 Scene/Properties |
| 状态栏槽位 | `configs/<client>.json` → `statusBar.items[]` | 客户标识 / 授权状态 / 自定义指示器 |
| 右键菜单 | `configs/<client>.json` → `contextMenus[]` | 2D/3D 画布右键项及动态段插入位置 |
| 快捷键 | `configs/<client>.json` → `shortcuts[]` | 覆盖默认键位 |
| 面板 / 状态栏槽位实现 | `UiPanelRegistry::registerPanel()` 工厂 | 注册自定义属性面板、自定义指示器 |
| **功能授权分级** | JSON `feature` 字段 + 注册码 `features` | 未授权项不出现 |
| 图标资源 | `IconHelper` + 多主题覆盖集 `:/ui/common/Icons/*` | qrc 追加 / 皮肤覆盖集 / `setUserIconDirectory()` |
| 回退策略 | `ConfigFallbackPolicy` | Strict / Fallback / Silent |

### 1.2 原则上不可定制的（触碰需经过架构评审）
- `SyEntity` / `SceneManager` / `SceneEditService`（算法内核）
- `OperationBus` 注册的 `OperationId` 枚举与其实现
- 渲染契约 `SceneRenderContract.h`

> 约定：客户化只能从「**外观与编排**」的维度出发，不能绕开 `CommandCatalog → OperationBus` 去直改实体或渲染。

---

## 2. 目录与关键文件

```
Main/Src/UI/ClientConfig/
├── UiClientContext.h/.cpp    客户 ID 运行时唯一事实源（解析 + 缓存 + 资源路径回退）
├── UiClientConfigBase.h      数据结构（MenuDef / ToolBarDef / DockDef / ShortcutDef /
│                             StatusBarDef / StatusBarSlotDef / ContextMenuDef / UiConfigData）
├── UiConfigLoader.h/.cpp     JSON 解析 + extends 继承合并
├── UiConfigurationManager.h/.cpp  加载器 + 回退；shared() 为进程级唯一配置源
├── UiLayoutBuilder.h/.cpp    数据→Qt 控件构建器（Menu/ToolBar/Dock/Shortcut/StatusBar/ContextMenu）
├── UiPanelRegistry.h/.cpp    面板与状态栏槽位工厂注册表
├── UiBuiltinPanels.h/.cpp    内置面板/槽位工厂的唯一注册入口
├── UiContextMenuService.h/.cpp  配置驱动右键菜单 + 动态段提供者注册
├── UiFeatureGate.h/.cpp      功能授权闸门（License features → UI feature 字段）
└── configs/
    ├── configs.qrc           Qt 资源：:/configs/*.json
    ├── base.json             公共基线（其它客户继承）
    ├── san_yi.json           标准版布局
    ├── client_a.json         示例：extends base，精简菜单/工具栏
    └── client_b.json         示例：extends base，授权分级 + 状态栏/右键菜单定制
```

> 消费方：`WorkbenchMenuManager`（菜单+快捷键）、`WorkbenchLayoutManager`（工具栏+Dock+状态栏）、
> `Workbench2D/3D::buildConfiguredContextMenu`（右键菜单）。三者都从
> `UiConfigurationManager::shared()` 取同一份 `UiConfigData`。

---

## 3. 定制流程

### 3.1 选择/新建一个客户版本

**客户 ID 在运行时解析，不需要重新编译。** 解析优先级由高到低
（`UiClientContext::resolveClientId`）：

| 优先级 | 来源 | 用途 |
|--------|------|------|
| 1 | `UiClientContext::setClientIdOverride()` | 命令行 `--client=xxx`、单元测试 |
| 2 | 环境变量 `SANYI_CLIENT_ID` | CI、现场临时切换排查 |
| 3 | `QSettings` 键 `Client/Id` | 安装包部署时写入客户标识 |
| 4 | 内置默认 `san_yi` | 兜底 |

```powershell
# 现场切换客户，无需重新编译
$env:SANYI_CLIENT_ID = "client_b"; .\SanYiCAD.exe
```

CMake 侧只剩一个 `SANYI_DEFAULT_CLIENT_ID`，**仅用于安装包写入默认设置**，
不再作为编译期宏影响代码分支：

```cmake
set(SANYI_DEFAULT_CLIENT_ID "san_yi" CACHE STRING "Default UI client ID written to installer settings")
```

配置资源不存在时（客户 ID 拼错 / 配置未随包发布），
`UiClientContext::configResourcePath()` 会打 WARN 并回退到 `san_yi.json` ——
因为已无硬编码回退路径，加载失败会直接导致空窗口。

> **换客户不改代码、不重编译，只改环境变量或安装包设置。**
> 启动日志中 `[UiClientContext] Active client id='xxx' (source: yyy)` 是排查
> 「客户配置为什么没生效」的第一现场。

### 3.2 编写/修改 JSON 布局

以 `configs/san_yi.json` 为模板，新增 `configs/client_a.json`：

```json
{
  "meta": { "clientId": "client_a", "clientName": "客户 A 精简版", "version": "1.0" },
  "extends": "base",                      // ← 继承 base.json（同 id 覆盖，新 id 追加）
  "docks": [
    { "id": "SceneDock", "title": "Scene", "position": "left",
      "widgetType": "SceneTreePanel", "visible": true }
  ],
  "shortcuts": [{ "command": "edit.redo", "keys": "Ctrl+Y" }]
}
```

可用字段（详见 `UiClientConfigBase.h`）：

- `docks[].{id,title,position∈{left|right|top|bottom},widgetType,visible}`
- `menus[].{id,label,visible,workbenches[],items:[action|separator|submenu]}`
- `toolbars[].{id,title,position∈{top|left|right|bottom},workbench,feature,items[]}`
  （注意 JSON 键是小写 `toolbars`、`workbench`）
- `shortcuts[].{command,keys}`
- `statusBar.{visible,sizeGripEnabled,items[]}` — 见 §3.11
- `contextMenus[].{id,workbench,feature,items[],dynamicSections[]}` — 见 §3.12
- `feature` — 可挂在菜单项 / 工具栏 / 状态栏槽位 / 右键菜单上的授权门槛，见 §3.13
- `themeStyle` — 主题标识或 QSS 路径

**继承规则**（`UiConfigLoader::loadWithInheritance` + `mergeConfig`）：
- 同 `id` 的菜单/工具栏 → **替换**；
- 新 `id` → **追加**；
- 未出现在子配置中的字段 → **继承自父**。

> 调试技巧：`UiConfigLoader::lastError()` 返回失败原因。加载失败且策略为 `Fallback` 时自动回退到 `:/configs/san_yi.json`。

### 3.3 注册/替换面板

内置面板与状态栏槽位的工厂**只在一个地方注册** ——
`UiBuiltinPanels.cpp` 的 `registerBuiltinUiPanels(UiPanelRegistry&)`：

```cpp
// Main/Src/UI/ClientConfig/UiBuiltinPanels.cpp
void registerBuiltinUiPanels(UiPanelRegistry& registry) {
    registry.registerPanel("SceneTreePanel",   [](QWidget* p){ return new SceneTreePanel2D(p); });
    registry.registerPanel("PropertiesPanel",  [](QWidget* p){ return new PropertiesPanelWidget(p); });
    registry.registerPanel("ClientIndicator",  ...);   // 状态栏槽位
    registry.registerPanel("LicenseIndicator", ...);
    registry.registerPanel("Spacer",           ...);
    registry.registerPanel("MessageLabel",     ...);
}
```

> **历史坑**：此前 `WorkbenchMenuManager` 与 `WorkbenchLayoutManager` 各自维护一份注册表，
> 导致两边注册的 `widgetType` 集合会漂移。现已统一到本函数，**新增槽位/面板只改这一处**。

定制面板：
1. 编写 `MyCustomPanel : public QWidget`；
2. 在 `registerBuiltinUiPanels()`（或客户专属注册函数）里
   `registry.registerPanel("MyPanel", [](QWidget* p){ return new MyCustomPanel(p); });`；
3. 在 JSON 的 `docks[].widgetType`（或 `statusBar.items[].widgetType`）写 `"MyPanel"`。

> 注意：`UiLayoutBuilder::buildDocks` 遇到 **未注册** `widgetType` 会降级为占位 `QWidget` 并 `SY_WARNF` 告警 —— 不会崩溃。

**场景树面板（数据/算法/UI 分离，UI 可定制/可缺失）**：
- 2D 场景树：`SceneTreeBuilder2D`（算法层，读 Engine2D 场景）→ `SceneTreeModel2D`（数据层）→ `SceneTreePanel2D`（UI，由 `WorkbenchLayoutManager` 经面板注册表创建）。
- 3D 场景树：`SceneTreeBuilder3D`（算法层，读 `SceneManager3D`）→ `SceneTreeModel3D`（数据层）→ `SceneTreePanel3D`（UI，由 `Workbench3D::setupSceneTree3D` 直接创建并注册 dock）。
- 两套面板只消费各自纯数据模型并发出选择/可见性/重命名信号，业务逻辑不感知 UI；面板可替换/移除/定制，不影响算法层与数据层。
- 3D 树刷新时机：导入时 `SceneManager3D::markDataChanged()` 触发 `SceneMonitor3D::sceneChanged` → `Workbench3D::refreshSceneTree3D`；另在 `ImportService::importFinished` 显式兜底一次。
- 算法层单测：`Main/Src/UI/Test/SceneTreeBuilder3DTests.cpp`（空场景/节点填充/名称回退/选中计数/`selectedIds`）。

### 3.4 图标

图标来自 Qt 资源 `:/ui/common/...`（见 `UI/Common/Resources/ui_common.qrc`），统一经 `IconHelper` 加载，并**随当前皮肤自动换色**（深/浅/蓝/板岩/高对比）。

#### 3.4.1 基本用法

任何 `QAction` / `QAbstractButton` / 菜单图标，用 `IconHelper` 而不是裸 `QIcon`：

```cpp
IconHelper::setThemedIcon(action, ":/ui/common/Icons/Actions/move.svg");
// 菜单用链式：IconHelper::themed(menu->addAction(...), ":/ui/common/Icons/...");
```

SVG 规范：以 `currentColor` 作为主描边色，`IconHelper` 渲染时替换为当前主题前景色，并自动生成 Normal / Disabled / Active / Selected 四种状态色。

#### 3.4.2 语义色令牌（同一张图适配所有皮肤）

除 `currentColor` 外，SVG 内可使用以下令牌，让**同一张图在不同皮肤下自动得到不同底色 / 强调 / 状态色**：

| 令牌 | 语义 | Dark 示例 | Light 示例 |
|------|------|-----------|------------|
| `currentColor` | 前景主色 | `#e0e0e0` | `#1f2937` |
| `%fgmuted%` | 次要线 | `#a0a0b0` | `#6b7280` |
| `%bg%` | 图标底色（chip） | `#2a2a3c` | `#ffffff` |
| `%bgalt%` | 备选底色 | `#3d3d52` | `#eef2f7` |
| `%accent%` | 强调 / 高亮 / 开启态 | `#5dadec` | `#3b82f6` |
| `%success%` / `%warning%` / `%error%` | 状态色 | `#2ecc71` / `#f39c12` / `#ff6b6b` | `#16a34a` / `#d97706` / `#dc2626` |

令牌定义见 `ThemeManager::colorTokens()`；5 套皮肤色值见 `ThemeManager::applyColorsForTheme()`。

> 老图标只有 `currentColor` 的无需改动，行为与之前完全一致。

#### 3.4.3 存储布局：默认集 + 皮肤覆盖集

调用点写默认路径即可，无需感知皮肤。现有图标分类目录：

| 目录 | 用途 |
|------|------|
| `Icons/File/` | 文件操作（新建/打开/保存/导入/导出/退出…） |
| `Icons/Edit/` | 编辑（撤销/重做…） |
| `Icons/Actions/` | 修改操作（移动/旋转/镜像/布尔/裁剪/圆角/算法…） |
| `Icons/Tools/` | 绘图工具（线/圆/矩形/贝塞尔/文字…） |
| `Icons/View/` | 2D 视图（图层/单位/捕捉/正交/缩放/3D 切换…） |
| `Icons/View3D/` | 3D 视图（视角/线框/网格/缩放/重置…） |
| `Icons/Model3D/` | 3D 图元（方体/球/圆柱…） |
| `Icons/Help/` | 帮助（文档/快捷键/设置/语言/主题/关于…） |

```
UI/Common/Resources/Icons/
├── <Category>/name.svg                 默认基础集（通用，qrc 注册）
├── light/<Category>/name.svg           Light / Blue 皮肤覆盖
├── dark/<Category>/name.svg            Dark / Slate 皮肤覆盖
└── highcontrast/<Category>/name.svg    HighContrast 皮肤覆盖
```

**解析顺序**（`IconHelper::resolveIconResource`）：

1. 用户自定义目录（文件系统，见 3.4.4）；
2. `:/ui/common/Icons/<flavor>/<相对路径>` — 皮肤覆盖集；
3. `:/ui/common/Icons/<相对路径>` — 默认集。

覆盖集**只放需要差异化的图标**，其余自动继承默认集。皮肤 → 覆盖集映射由 `ThemeManager::iconFlavorFor()` 决定：
`Dark/Slate → dark`，`Light/Blue/Default → light`，`HighContrast → highcontrast`，`System → 根据当前系统外观动态选择`。

#### 3.4.4 用户定制图标

运行期指定一个自定义图标目录，同名相对路径优先于内置资源：

```cpp
IconHelper::setUserIconDirectory("C:/myIcons");   // 传空串清除定制
// 目录内放 "Actions/move.svg"、"View/zoom_in.svg" 等即可覆盖对应图标
// 调用即触发全量刷新，无需重启
```

#### 3.4.5 新增一套皮肤的步骤

1. `AppTheme` 枚举（`ThemeManager.h`）加一项；
2. `ThemeManager::applyColorsForTheme()` 填 9 个文本色 + 8 个图标令牌色；
3. `ThemeManager::iconFlavorFor()` 将新皮肤归入已有或新增覆盖集；
4. `ThemeManager::loadStylesheet()` 加对应 QSS 路径（`Styles/<theme>.qss`）；
5. 仅个别图标需要差异化时，再新增 `<flavor>/` 覆盖集并注册进 qrc。

#### 3.4.6 系统主题跟随（System 模式）

`AppTheme::System` 是一个特殊的主题模式，它不加载独立的 QSS 文件，而是根据操作系统的深色/浅色模式设置，自动切换到 `Light` 或 `Dark` 主题。

**核心组件：`SystemThemeDetector`**（`UI/Common/Include/UI/SystemThemeDetector.h`）

- 单例模式，通过 `STD` 宏访问
- 跨平台检测：macOS（NSAppearance）、Windows（注册表）、Linux（GTK 主题）
- 信号驱动：系统主题变化时发射 `systemThemeChanged(bool isDark)`
- 低耦合：仅负责检测，不直接操作 UI 样式

**工作流程：**

```
系统外观变化
  → SystemThemeDetector 检测到变化
     → 发射 systemThemeChanged(isDark) 信号
        → ThemeManager 接收信号（构造函数中连接）
           → 若当前为 System 模式：
              → 解析为 Light 或 Dark
              → 应用对应的 QSS + Palette + 图标
              → 发射 themeChanged(System) 信号
```

**各平台检测策略：**

| 平台 | 检测方法 | 变化监听 |
|------|---------|---------|
| macOS | `[NSApp effectiveAppearance]` + `bestMatchFromAppearancesWithNames:` | `NSDistributedNotificationCenter` 监听 `AppleInterfaceThemeChangedNotification` |
| Windows | 注册表 `HKCU\...\Personalize\AppsUseLightTheme` | `QAbstractNativeEventFilter` 监听 `WM_SETTINGCHANGE` |
| Linux | 读取 GTK 主题名称（gsettings / settings.ini） | `QTimer` 每 5 秒轮询 |

**日志输出：**

所有检测和变化事件均通过 `SY_INFO`/`SY_DEBUG` 记录，格式为 `[SystemThemeDetector] ...`，便于问题排查。

#### 3.4.7 刷新与缓存

- 主题切换：`ThemeManager::setTheme` → `IconHelper::refreshAllThemedIcons()` 全量重刷（遍历所有带 `kThemedIconPathProperty` 属性的 action/button）。
- 渲染缓存：按「解析路径 + 令牌色 + 尺寸 + DPR」缓存 `QPixmap`，切换主题 / DPI 后自动失效。

> 资源路径区分大小写，前缀为 `/ui/common`。

### 3.5 点击日志

约定：所有菜单/工具栏 action 的触发，最终走 `OperationBus::run(OperationId)`。在 `OperationBus` 处集中记录日志即可（例如 `SY_INFOF("[Cmd] %s", Cmd::operationIdToString(id))`），而非每个 action 打一份。已实现的 `UiLayoutBuilder::bindAction` 会在 **命令未注册**时 `SY_WARNF` 告警。

### 3.6 菜单也可以配置化

菜单不再只是一段硬编码 UI，而是可以通过 **命令目录驱动 + 菜单树配置** 来生成。这样不同客户可以在不改核心代码的前提下，定制：

- 顶层菜单是否显示
- 菜单项顺序
- 菜单分组结构
- 菜单标题文案
- 菜单图标
- 菜单是否启用/禁用
- 菜单是否仅在某个工作台显示
- 菜单是否随客户版本切换

> 约定：菜单配置只决定“怎么展示”，不直接承载业务逻辑。业务执行仍然必须回到 `CommandCatalog -> OperationBus`。

### 3.7 菜单配置 schema 草案

下面是建议的菜单配置结构。它可以放在客户配置 JSON 中，也可以作为单独的菜单配置文件加载。

```json
{
  "meta": {
    "clientId": "client_a",
    "clientName": "客户A定制版",
    "version": "1.0"
  },
  "menus": [
    {
      "id": "file",
      "label": "File",
      "visible": true,
      "workbenches": ["2D", "3D"],
      "items": [
        { "type": "action", "commandId": "file.new" },
        { "type": "action", "commandId": "file.open" },
        { "type": "separator" },
        {
          "type": "submenu",
          "id": "file.import",
          "label": "Import",
          "items": [
            { "type": "action", "commandId": "file.import_dxf" },
            { "type": "action", "commandId": "file.import_svg" }
          ]
        }
      ]
    }
  ],
  "toolbarBindings": [
    {
      "toolbarId": "top",
      "items": [
        { "type": "action", "commandId": "file.open" },
        { "type": "action", "commandId": "edit.undo" }
      ]
    }
  ]
}
```

### 3.8 Schema 字段说明

#### 顶层字段

- `meta.clientId`：客户标识
- `meta.clientName`：客户显示名
- `meta.version`：配置版本
- `menus[]`：菜单树定义
- `toolbarBindings[]`：工具栏绑定定义

#### `menus[]` 字段

- `id`：菜单节点唯一标识
- `label`：显示文本
- `visible`：是否显示
- `workbenches`：允许出现的工作台列表，例如 `2D`、`3D`
- `items[]`：菜单项列表

#### `items[]` 支持类型

- `action`：命令项，必须有 `commandId`
- `separator`：分隔线
- `submenu`：子菜单，必须有 `items`

#### `action` 常用扩展字段

- `commandId`：命令 ID，必须与 `CommandCatalog` 对齐
- `text`：可覆盖默认显示文本
- `icon`：可覆盖图标资源
- `shortcut`：可覆盖快捷键
- `visible`：是否显示
- `enabled`：是否启用
- `checked`：是否勾选
- `workbenches`：仅在特定工作台显示

### 3.9 菜单定制可以怎么做

不同客户的定制建议按下面几种方式进行：

#### 方式 A：只改配置，不改代码

适合：

- 菜单增删
- 菜单排序
- 菜单显隐
- 菜单标题调整
- 图标替换

做法：

1. 复用统一菜单 schema
2. 通过客户配置文件覆盖默认菜单树
3. 保持 `commandId` 不变

#### 方式 B：配置 + 命令目录扩展

适合：

- 新增客户专属命令
- 新增行业菜单项
- 新增 2D/3D 专属入口

做法：

1. 在 `CommandCatalog` 增加命令
2. 在菜单 schema 中引用新 `commandId`
3. 让 `OperationBus` 提供对应实现

#### 方式 C：配置 + 插件扩展

适合：

- 客户自定义面板
- 客户自定义工具栏组
- 客户自定义高级命令

做法：

1. 插件注册自己的命令
2. 插件提供图标和动作信息
3. 菜单 schema 引用插件命令 ID

### 3.10 菜单定制的边界

允许定制：

- 菜单结构
- 菜单显示文本
- 菜单图标
- 菜单顺序
- 菜单工作台可见性
- 菜单是否启用

不建议客户直接定制：

- `OperationId` 的核心语义
- `OperationBus` 执行链
- `SceneManager` / `SceneEditService` 内部逻辑
- 渲染契约

### 3.11 状态栏配置（`statusBar`）

状态栏不再硬编码，由 JSON 描述槽位序列，`WorkbenchLayoutManager::buildStatusBar()`
调用 `UiLayoutBuilder::buildStatusBar()` 生成。

```json
"statusBar": {
  "visible": true,
  "sizeGripEnabled": true,
  "items": [
    { "id": "statusSpacer",     "widgetType": "Spacer",           "align": "left", "stretch": 1 },
    { "id": "clientIndicator",  "widgetType": "ClientIndicator",  "align": "permanent" },
    { "id": "licenseIndicator", "widgetType": "LicenseIndicator", "align": "permanent" }
  ]
}
```

| 字段 | 说明 |
|------|------|
| `id` | 槽位标识，用于日志与继承覆盖 |
| `widgetType` | 必须已在 `registerBuiltinUiPanels()` 注册（见 §3.3） |
| `align` | `left`（默认）→ `QStatusBar::addWidget`；`permanent` / `right` → `addPermanentWidget` |
| `stretch` | 拉伸因子，做占位弹簧时配 `Spacer` + `stretch: 1` |
| `minimumWidth` | 最小宽度，避免文本抖动 |
| `feature` | 授权门槛，见 §3.13 |
| `workbenches` | 仅在指定工作台显示（如 `["2D"]`） |
| `visible` | false 时不创建 |

> **`align` 的实际差别**：`addWidget` 放的控件会被 `showMessage()` 的临时消息**覆盖**，
> `addPermanentWidget` 不会。客户标识 / 授权状态这类常驻指示器必须用 `permanent`。
>
> **继承语义**：`statusBar` 是**整段替换**（只要子配置写了 `statusBar` 就完全覆盖父配置，
> 不做逐槽位合并）。`StatusBarDef::declared` 用来区分「没写」和「写了空的」。

> 实现注解：`StatusBarDef` 的成员叫 `items` 而**不是** `slots` ——
> Qt 把 `slots` 定义成了空宏，用作结构体成员名会编译失败。

### 3.12 右键菜单配置（`contextMenus`）与动态段

```json
"contextMenus": [
  {
    "id": "canvas.2d",
    "workbench": "2D",
    "items": [
      { "type": "action", "id": "edit.cut",  "label": "Cut",  "command": "edit.cut" },
      { "type": "separator" },
      { "type": "action", "id": "view.layer_manager", "label": "Layer Manager", "command": "view.layer_manager" }
    ],
    "dynamicSections": ["layer.actions"]
  }
]
```

调用路径：

```
右键事件（Workbench2D::onViewportContextMenu / Workbench3D::on3DContextMenuRequested）
  → buildConfiguredContextMenu("canvas.2d" | "canvas.3d", ...)
      → UiContextMenuService::buildMenu(config, id, dispatcher, parent)
          ├─ 按 items[] 生成静态项（走 UiLayoutBuilder::buildContextMenu）
          └─ 按 dynamicSections[] 顺序追加动态段
  → menu->exec(globalPos)  →  delete menu
```

**`dynamicSections` 解决什么问题**：右键菜单里有一部分内容取决于运行期数据
（例如「设为当前图层」/「移动到图层…」子菜单要枚举 `LayerManager::getAllLayerIds()`），
无法用静态 JSON 描述。做法是在 JSON 里**只声明插入位点的 id**，
C++ 侧注册一个填充器：

```cpp
UiContextMenuService::instance().registerDynamicSection(
    QStringLiteral("layer.actions"),
    [this](QMenu* menu) {
        CommandActionHub::populateLayerContextSection(menu, layerManager, hasSelection);
    });
```

动态段按 `dynamicSections` 数组顺序、**追加在静态项之后**。
未注册的 section id 会被跳过并告警，不会崩。

> **生命周期陷阱**：`buildConfiguredContextMenu` 里的 `IUiCommandDispatcher`
> 适配器是**栈对象**，因此弹出后必须在同一作用域内 `delete menu`，
> **不能用 `deleteLater()`** —— 否则 dispatcher 已析构而菜单还在。

> 尚未迁移的右键入口：`UiSceneTreePanel2D` 的场景树右键仍是硬编码。
> 需要定制时按同样的 `contextMenus` + `dynamicSections` 模式迁移。

### 3.13 功能授权分级（`feature`）

`feature` 把「注册码买了什么」和「界面显示什么」连起来，
链路为：注册码 `features` 字段 → `UiFeatureGate` → JSON `feature` 字段。

写入侧（`CADApplicationRuntime.cpp`）：

```
License_GetInfo() 成功
  → UiFeatureGate::instance().loadFromLicenseString(info.features)   // 逗号/分号/空白分隔
授权校验被关闭，或 License_GetInfo 失败
  → UiFeatureGate::instance().setUnrestricted(true)                  // 不做限制
```

读取侧（`UiLayoutBuilder`）：菜单项 / 整条工具栏 / 单个工具栏按钮 /
状态栏槽位 / 整个右键菜单，共 5 处会先问 `UiFeatureGate::isAllowed(feature)`。

```json
{ "type": "action", "id": "algo.nesting", "command": "algo.nesting", "feature": "nesting" }
```

语义约定：

- `feature` 为空 → 永远允许（绝大多数基础功能不写这个字段）。
- 未授权项**根本不创建**，而不是创建后置灰 ——
  客户不应该看见自己没买的功能入口。
- `features` 里写 `*` 或 `all` → 不受限。
- 匹配大小写不敏感。

> 示例见 `configs/client_b.json`（`nesting` / `relief3d` / `vision` 三级授权）。

---

## 4. 运行期与回退

配置驱动是唯一路径，**没有硬编码骨架兜底**，因此回退全部发生在「取哪份 JSON」这一层：

```
UiClientContext::clientId()                       // 覆盖 > 环境变量 > QSettings > san_yi
  → UiClientContext::configResourcePath()
        ├─ :/configs/<client>.json 存在 → 用它
        └─ 不存在 → SY_WARN + 回退 :/configs/san_yi.json
  → UiConfigurationManager::shared()              // 进程级唯一，ConfigFallbackPolicy::Fallback
        ├─ 解析成功 → UiConfigData
        └─ 解析失败 → 再回退 :/configs/san_yi.json
  → 三个消费方共用同一份 UiConfigData：
        ├─ WorkbenchMenuManager      菜单 + 快捷键
        ├─ WorkbenchLayoutManager    工具栏 + Dock + 状态栏
        └─ Workbench2D/3D            右键菜单
  → Dock 构建彻底失败 → SY_ERROR，错误码 ui.dock_config_build_failed
```

`ConfigFallbackPolicy`：

- `Strict` — 失败即失败（**开发调试用**，能第一时间暴露 JSON 错误）；
- `Fallback` — 失败时用 `:/configs/san_yi.json`（**当前 `shared()` 采用**）；
- `Silent` — 失败则空配置。

> 关键日志（排查「客户配置为什么没生效」按这个顺序看）：
> 1. `[UiClientContext] Active client id='xxx' (source: yyy)`
> 2. `[UiClientContext]` 资源缺失告警
> 3. `UiConfigLoader::lastError()` 对应的解析错误
> 4. `ui.dock_config_build_failed`

> **不要再新增 `#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI` 分支。**
> 该宏留在 CMake 里仅为兼容旧脚本，代码里已无引用。
> 历史教训：这个宏曾长期默认 OFF，而 ON 分支里的 `NullDispatcher`
> 定义在 `buildDockAreasFromConfig()` 内部却被 `buildToolBars()` 引用 ——
> 也就是说**开启后根本编译不过**，这正是「配置驱动代码写了却从没跑过」的根因。

---

## 5. 动态菜单与命令绑定（边界约定）

菜单的 **动态部分** 由 `WorkbenchMenuManager` 在工作台切换时重建
（`refreshXxxMenuForWorkbench()` 系列）。2D / 3D 菜单现在走**同一套** JSON +
命令目录路径，`MenuManager3D` 及其 5 个 Menu 子类的硬编码构建路径已移除，
`Workbench3D::managesOwnMenus()` 恒返回 `false`。


```
QAction::triggered
  → lambda(commandId)
      → logMenuTrigger(text, commandId)                // 统一菜单触发日志
      → dispatchCommandSafely(commandId)               // 内部按 CommandCatalog::operationForCommandId 路由
      → OperationBus::run(OperationId)                 // 执行
```

命令字符串命名规范（统一小写下划线）：`file.*`、`edit.*`、`view.*`、`tool.*`、`2d.*`、`algo.*`、`help.*`。

**新增菜单项的正确步骤**：
1. 在 `CommandCatalog`（`UI/2D/Src/Operation/CommandCatalog.cpp`）注册一条 `kMenuOnly`/`kMenuTopCtx` 的 `CommandEntry2D`；
2. 在 `OperationBus` 注册对应的 `OperationId` 实现；
3. 确保 `operationForCommandId()` 能把命令字符串映射到该 `OperationId`。

> 错误示范：不要在菜单 action 的 lambda 里直接实现业务逻辑 —— 应该通过 `OperationBus::run()`。

---

## 6. 流程速查（给后续开发人员）

### 6.1 目标：“客户 A 的右侧 panel 改成自定义面板”
```
1. UiBuiltinPanels.cpp 里 registry.registerPanel("MyPanel", ...)
2. 写 configs/client_a.json：docks[].widgetType = "MyPanel"
3. configs.qrc 加入 client_a.json（新客户才需要）
4. cmake -S . -B build && cmake --build build --config Debug   ← 新增 .cpp 必须先 reconfigure
5. 运行：$env:SANYI_CLIENT_ID = "client_a"; .\SanYiCAD.exe      ← 不改 CMake、不改客户宏
```

### 6.2 目标：“新增一个菜单命令”
```
1. CommandCatalog.cpp 加 CommandEntry2D（surfaces 位掩码决定它出现在哪些面）
2. OperationBus 注册 OperationId 实现
3. 在客户 JSON 的 menus[]/toolbars[]/contextMenus[] 里引用该 command
```

### 6.3 目标：“某个功能只卖给买了授权的客户”
```
1. 注册码 features 字段里加 "nesting"
2. JSON 对应项加 "feature": "nesting"
3. 未授权时该项不会被创建（不是置灰）
```

---

## 7. 常见问题

- **QAction 没反应 / 不显示图标**：检查 JSON 的 `command` 是否在 `CommandCatalog` 注册过、
  `widgetType` 是否在 `registerBuiltinUiPanels()` 注册过、图标路径是否以 `:/ui/common/` 开头。
- **某个菜单项/按钮整个消失了**：先看它有没有 `feature` 字段 —— 未授权项是**不创建**的。
  确认 `UiFeatureGate::licensedFeatures()` 里有没有对应条目。
- **换了 `SANYI_CLIENT_ID` 环境变量但界面没变**：看启动日志
  `[UiClientContext] Active client id=...`。若 source 不是 `env`，说明有更高优先级的
  override 生效；若 id 对但界面没变，检查该 JSON 是否已加入 `configs.qrc`。
- **JSON 修改后不生效**：资源通过 AUTORCC 编译进二进制，改 JSON 后需 **重新 CMake+编译**（不能单纯刷新运行）。
- **新增了 .cpp 但没被编译**：本仓库用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`，
  实测新增源文件后必须显式 `cmake -S . -B build` 重新配置，否则 `--build` 会“成功”但不编译新文件。
- **布局快照还原覆盖标题**：`UiLayoutBuilder` 构建 Dock 会设置 `objectName`（如 `SceneDock`/`PropertiesDock`）；`restoreLayoutSnapshot()` 依赖 `_workbench_dock_title` 属性恢复标题，构建时已通过 `setProperty` 兜底。
- **状态栏指示器被临时消息盖掉**：`align` 写成了 `left`，改成 `permanent`。
- **右键菜单里的动态段没出现**：`dynamicSections` 声明的 id 与
  `registerDynamicSection()` 注册的 id 不一致（区分大小写）。
- **3D 菜单**：3D 命令来自 `CommandCatalog3D`，菜单构建受 `BUILD_UI3D` 宏保护，与上一节约定一致。
- **System 主题不跟随系统切换**：检查日志中是否有 `[SystemThemeDetector]` 输出；macOS 需确保应用有辅助功能权限；Windows 需确保注册表路径正确；Linux 需确保 `gsettings` 命令可用。
- **深色模式下文字看不清**：检查该控件是否使用了硬编码颜色（如 `#f0f0f0`、`color: red`），应替换为 `TM->colors()` 中的语义色。


---

## 8. 数十个客户的多客户定制管理

> 目标：在 **数十个客户、每个客户菜单/工具栏/面板各不相同** 的场景下，让定制开发可控、可维护、可回归。
> 核心思想：**一份公共基线 + 每客户一份差异配置 + 统一命令目录**，尽量“只改 JSON，不改 C++”。

### 8.1 配置分层模型（基线 + 差异）

所有客户共享一份 `base.json`（公共基线），每个客户只维护自己的差异文件：

```
Main/Src/UI/ClientConfig/configs/
├── base.json              公共基线（菜单/工具栏/Dock/快捷键的默认结构）
├── san_yi.json            extends base  三义标准版（默认）
├── client_a.json          extends base  客户 A（覆盖差异项）
├── client_b.json          extends base  客户 B
├── ...                    （数十个客户，每个一个 JSON）
└── client_zz.json         extends client_a  支持客户再继承
```

继承规则（`UiConfigLoader::loadWithInheritance` + `mergeConfig`）：
- 同 `id` 的菜单/工具栏 → **替换**；
- 新 `id` → **追加**；
- 未出现在子配置中的字段 → **继承自父**。

> 效果：新增客户 = 新建 1 个 JSON + CMake 加 1 行，**不触碰任何 C++ 源文件**。

### 8.2 命令目录统一（所有客户共享同一套 commandId）

- 2D / 3D 命令目录（`CommandCatalog.cpp` / `CommandCatalog3D.cpp`）是**唯一命令事实来源**，所有客户共用。
- 客户配置只决定「怎么展示」（顺序 / 分组 / 文案 / 图标 / 显隐），不决定「有什么功能」。
- 客户要新增专属功能时，才需要扩展命令目录 + OperationBus（见 8.4）。

### 8.3 客户专属功能的三级扩展

| 级别 | 适用场景 | 改动位置 | 是否改 C++ |
|------|---------|---------|-----------|
| L1 配置级 | 菜单增删 / 排序 / 显隐 / 文案 / 图标 / 快捷键 | 客户 JSON | 否 |
| L2 命令级 | 新增客户专属命令（复用现有业务能力） | `CommandCatalog` + `OperationBus` 注册 | 是（少量） |
| L3 面板级 | 客户自定义面板 / 工具栏组 / 高级命令 | `UiPanelRegistry::registerPanel()` + 客户 JSON | 是（少量） |

> 约定：L2 / L3 的 C++ 代码必须放在**客户专属目录**（如 `Main/Src/UI/ClientConfig/custom/<clientId>/`）。
> **注意：客户 ID 已运行时化，不要再按客户条件编译。** 客户专属代码应全部编进同一份二进制，
> 在注册阶段按 `UiClientContext::instance().clientId()` 判断是否注册自己的面板/命令，
> 或直接用 `feature` 授权字段控制可见性 —— 这样才能保持「一份二进制服务全部客户」。

### 8.4 代码组织建议（数十客户规模）

```
Main/Src/UI/ClientConfig/
├── configs/                    # 所有客户 JSON（纯数据，非 C++ 人员可维护）
│   ├── base.json
│   ├── san_yi.json
│   └── client_*.json
├── custom/                     # 客户专属 C++（按客户分目录，全部参与编译）
│   ├── client_a/
│   │   ├── ClientAPanels.cpp
│   │   └── ClientACommands.cpp
│   └── client_b/
├── UiBuiltinPanels.h/.cpp      # 公共：内置面板/槽位工厂唯一注册入口
└── UiConfigLoader.h/.cpp       # 公共：JSON 解析 + 继承合并（所有客户共享）
```

客户专属面板的注册方式（**运行时判断，不用条件编译**）：

```cpp
// UiBuiltinPanels.cpp 末尾，或客户自己的注册函数
const QString clientId = UiClientContext::instance().clientId();
if (clientId == QLatin1String("client_a")) {
    registerClientAPanels(registry);   // 只影响注册表内容，不影响构建产物
}
```


### 8.5 版本管理与发布

- **配置校验**：为 JSON 写单元测试（加载 → 解析 → 字段完整性 → 命令 ID 是否在 CommandCatalog 中注册），
  未注册命令的菜单项在运行期自动禁用并 `SY_WARNF`，避免“菜单点了没反应”。
- **回归策略**：每个客户配置都能正常加载；`ClientConfigTests` 全量通过。
- **发布矩阵**：**一份构建产物服务全部客户**。客户差异由运行期 `SANYI_CLIENT_ID`
  环境变量 / `QSettings Client/Id` 决定，安装包写入默认值即可。
  CI 只需构建一次，然后对每个客户 ID 起一次进程做冒烟测试。
- **配置变更评审**：客户 JSON 的 `meta.version` 递增，diff 走代码评审，避免客户配置漂移。

### 8.6 现有框架适配性评估

**结论：配置驱动 + 命令目录 + 工作台过滤 + 面板工厂 + 运行时客户 ID + 授权闸门，**
**已能支撑「数十客户、纯配置化」的定制模式。剩余待补项如下：**

| 现状 | 评估 | 建议 |
|------|------|------|
| JSON 配置驱动 + extends 继承 | ✅ 唯一路径，硬编码分支已删 | 保持，强化配置校验测试 |
| 客户 ID 运行时解析 | ✅ 已具备（`UiClientContext`） | 保持；不要再引入客户维度的条件编译 |
| 命令目录统一（2D/3D） | ✅ 已具备 | 保持 |
| 工作台过滤 | ✅ 已具备 | 保持 |
| 面板/槽位工厂注册 | ✅ 已收口到 `registerBuiltinUiPanels()` | 保持 |
| 状态栏配置化 | ✅ 已具备（`statusBar.items[]`） | 保持 |
| 右键菜单配置化 + 动态段 | ✅ 画布右键已迁移 | 场景树右键（`UiSceneTreePanel2D`）待迁移 |
| License → UI 授权闸门 | ✅ 已接线（`UiFeatureGate`） | 补 `feature` 命名规范与授权矩阵文档 |
| 客户专属 C++ 目录 | ❌ 尚无实体 | 新增 `custom/<clientId>/` 目录 + 运行期注册约定 |
| 配置校验测试 | ⚠️ 部分 | 补“命令 ID 注册检查”“feature 拼写检查”单测 |
| 客户配置版本管理 | ⚠️ 无迁移机制 | `meta.version` + 变更评审；后续补配置迁移 |
| 3D 导出命令注册 | ❌ 缺失 | 补齐 CommandCatalog3D + FileOperations3D（见《菜单架构.md》12.2） |
| 硬件接口抽象 | ❌ 缺失（P1） | 见《Docs/05-硬件与设备/》规划 |


### 8.7 让定制更便捷的落地清单

1. 建立 `configs/` 客户 JSON 模板（含注释字段说明），新客户复制模板改差异。
2. 建立 `custom/<clientId>/` 客户 C++ 目录约定 + 运行期注册，禁止在公共代码里写客户分支。
3. 提供“配置预览”工具（可选）：加载客户 JSON 后截图对比，减少人工回归。
4. 把「新增客户」做成脚本化流程：`new_client.ps1 <id>` 生成 JSON + qrc 条目 + 单测骨架。
5. 所有客户共享同一套命令目录与同一份二进制，客户差异只体现在配置层与授权层 ——
   这是多客户可维护的关键。

---

## 9. 回归检查清单

- [ ] `:/configs/san_yi.json` / `client_a.json` / `client_b.json` 均能正常加载；
- [ ] `SANYI_CLIENT_ID` 指向**不存在**的客户时，回退到 `san_yi.json` 且日志有 WARN，界面不空白；
- [ ] 启动日志中 `[UiClientContext] Active client id=...` 与预期客户一致；
- [ ] Dock 能从 JSON 构建，`m_panelState.sceneTreeDock/propertiesDock/leftDock/rightDock` 仍被正确填充（供 `WorkbenchWindow::sceneTreeDock()`/`setSkeletonDocksVisible()` 使用）；
- [ ] 状态栏槽位按 JSON 生成，`permanent` 槽位不被 `showMessage()` 覆盖；
- [ ] 切换工作台后状态栏槽位被正确清理重建，无残留、无重复；
- [ ] 2D / 3D 画布右键菜单来自 `contextMenus`，图层动态段正常出现且能执行；
- [ ] 未授权 `feature` 对应的菜单/工具栏/状态栏/右键项**不出现**；授权后出现；
- [ ] 授权校验关闭时 `UiFeatureGate` 为 unrestricted，全部功能可见；
- [ ] 语言切换（`retranslateUi`）后菜单重建正常，无重复菜单栏；
- [ ] `ClientConfigTests` 全部通过；
- [ ] 全量 `MainTests` 回归无新增失败；
- [ ] **System 主题**：设置中选择 "System (Follow OS)" 后，切换系统深色/浅色模式时应用自动跟随切换；
- [ ] **System 主题**：日志中可看到 `[SystemThemeDetector]` 和 `[ThemeManager]` 的主题切换记录；
- [ ] **硬编码样式修复**：LicenseDialog、UiWorkbench、FillDialog 在深色模式下文字可读、背景协调；
- [ ] **跨平台**：macOS / Windows / Linux 各平台 System 主题检测正常工作。

