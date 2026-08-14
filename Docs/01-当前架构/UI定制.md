# UI 定制开发指南

> **面向读者**：后续开发人员 / 客户化工程师
> **目标**：教你在 **不改动核心算法/渲染代码**的前提下，完成 *不同客户不同的 UI 布局 / 菜单 / 工具栏 / 面板* 定制。

本文档是 `Docs/耦合性分析.md` 任务 E（布局配置化）的执行版。阅读前先理解整个架构的约定：

- UI 边界 ≈ **配置 + 注册表 + 构建器**，不负责业务语义。
- 业务语义入口唯一：`OperationBus::run(OperationId)`。
- 菜单/工具栏的 **动态刷新**（工作台切换）仍由 C++ `WorkbenchMenuManager` 维护；
  **静态骨架**（停靠面板布局）通过 JSON 配置化 —— 这是当前阶段的定制入口。

---

## 1. 定制能力边界（先念清）

### 1.1 可以定制的
| 维度 | 机制 | 示例 |
|------|------|------|
| 客户版本选择 | CMake 变量 `SANYI_CLIENT_ID` | `san_yi` / `client_a` / `client_b` |
| 布局开关 | CMake 选项 `SANYI_ENABLE_CONFIG_DRIVEN_UI` | ON=JSON 驱动 Dock；OFF=硬编码骨架 |
| 停靠面板骨架 | `configs/<client>.json` → `docks[]` | 增删/换位 Scene/Properties |
| 面板实现 | `UiPanelRegistry::registerPanel()` 工厂 | 注册自定义属性面板 |
| 菜单/工具栏 | 仍在 C++，动态刷新 | `WorkbenchMenuManager` |
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
├── UiClientConfigBase.h      数据结构（MenuDef / ToolBarDef / DockDef / ShortcutDef / UiConfigData）
├── UiConfigLoader.h/.cpp     JSON 解析 + extends 继承合并
├── UiConfigurationManager.h/.cpp  加载器 + 面板注册表持有者 + 回退
├── UiLayoutBuilder.h/.cpp    数据→Qt 控件构建器（Menu/ToolBar/Dock/Shortcut）
├── UiPanelRegistry.h/.cpp    面板工厂注册表
└── configs/
    ├── configs.qrc           Qt 资源：:/configs/*.json
    ├── san_yi.json           标准版布局
    ├── base.json             公共基线（其它客户继承）
    └── client_a.json         示例：extends base，新增左侧 dock
```

> 菜单/工具栏的动态构建仍在 `Main/Src/UI/Workbench/WorkbenchMenuManager.cpp` 与 `UI/2D`、`UI/3D` 的 `CommandCatalog` 下。

---

## 3. 定制流程

### 3.1 选择/新建一个客户版本

在 `Main/CMakeLists.txt` 之上，编译期决定资源路径：

```cmake
set(SANYI_CLIENT_ID "san_yi" CACHE STRING "Target UI client ID")   # ← 改这里
option(SANYI_ENABLE_CONFIG_DRIVEN_UI "Enable config-driven UI layout" OFF)  # ON 才走 JSON Dock
```

- `SANYI_CLIENT_ID` 用于拼资源路径 `:/configs/<client>.json`。
- `SANYI_ENABLE_CONFIG_DRIVEN_UI=ON` 才会调用 `WorkbenchLayoutManager::buildDockAreasFromConfig()`。
- 默认 OFF，保证基线构建行为不变；生产客户化时打开。

> **换客户不改代码，只改 `SANYI_CLIENT_ID` 然后重新CMake+编译。**

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
- `menus[].{id,label,items:[action|separator|submenu]}`
- `toolBars[].{id,title,position∈{top|left|right|bottom},workbenchId,items}`
- `shortcuts[].{commandId,keySequence}`
- `themeStyle` — 主题标识或 QSS 路径

**继承规则**（`UiConfigLoader::loadWithInheritance` + `mergeConfig`）：
- 同 `id` 的菜单/工具栏 → **替换**；
- 新 `id` → **追加**；
- 未出现在子配置中的字段 → **继承自父**。

> 调试技巧：`UiConfigLoader::lastError()` 返回失败原因。加载失败且策略为 `Fallback` 时自动回退到 `:/configs/san_yi.json`。

### 3.3 注册/替换面板

在 `WorkbenchLayoutManager::buildDockAreasFromConfig()` 注册内置面板工厂：

```cpp
registry->registerPanel("SceneTreePanel", [](QWidget* p){ return new SceneTreeDockWidget(p); });
registry->registerPanel("PropertiesPanel", [](QWidget* p){ return new PropertiesPanelWidget(p); });
```

定制面板：
1. 编写 `MyCustomPanel : public QWidget`；
2. `registerPanel("MyPanel", [](QWidget* p){ return new MyCustomPanel(p); });`；
3. 在 JSON 的 `docks[].widgetType` 写 `"MyPanel"`。

> 注意：`UiLayoutBuilder::buildDocks` 遇到 **未注册** `widgetType` 会降级为占位 `QWidget` 并 `SY_WARNF` 告警 —— 不会崩溃。

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
`Dark/Slate → dark`，`Light/Blue → light`，`HighContrast → highcontrast`。

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

#### 3.4.6 刷新与缓存

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

---

## 4. 运行期与回退

```
SANYI_ENABLE_CONFIG_DRIVEN_UI=ON
  → buildDockAreas() → buildDockAreasFromConfig()
        ├─ UiConfigurationManager::applyConfiguration(:/configs/<client>.json, Strict)
        │     ├─ 成功 → UiLayoutBuilder::buildDocks(docks)
        │     └─ 失败 → 回退到 hard-coded SceneDock/PropertiesDock
        └─ registerPanel 注册 SceneTreePanel / PropertiesPanel 工厂
```

- 回退策略 `ConfigFallbackPolicy::Strict`：失败就返回 false → 执行硬编码骨架（当前代码默认）。
- `Fallback`：失败时自动用 `:/configs/san_yi.json`。
- `Silent`：失败则空配置。

> 开发调试时建议用 `Strict` 快速发现 JSON 错误；生产发布用 `Fallback` 提升健壮性。

---

## 5. 动态菜单与命令绑定（边界约定）

菜单的 **动态部分** 由 `WorkbenchMenuManager` 在工作台切换时重建（`refreshXxxMenuForWorkbench()` 系列，2026-08-11 起**仅服务 2D**，对 3D 工作台早退，3D 菜单由 `MenuManager3D` 自理）：

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
```bash
1. 写 configs/client_a.json：docks[].widgetType = "MyPanel"
2. Main/CMakeLists.txt: SANYI_CLIENT_ID=client_a, SANYI_ENABLE_CONFIG_DRIVEN_UI=ON
3. WorkbenchLayoutManager::buildDockAreasFromConfig() 注册 registerPanel("MyPanel", ...)
4. cmake --build ... --config Debug
```

### 6.2 目标：“新增一个菜单命令”
```
1. CommandCatalog.cpp 加 CommandEntry2D (kMenuOnly) + 命令字符串
2. OperationBus 注册 OperationId 实现
3. （可选）WorkbenchMenuManager 动态刷菜单里加 action —— 多数命令已自动从 catalog 生成
```

---

## 7. 常见问题

- **QAction 没反应 / 不显示图标**：检查 `SANYI_ENABLE_CONFIG_DRIVEN_UI` 是否开、JSON `widgetType` 是否 `registerPanel` 注册过、图标路径是否以 `:/ui/common/`。
- **JSON 修改后不生效**：资源通过 AUTORCC 编译进二进制，改 JSON 后需 **重新 CMake+编译**（不能单纯刷新运行）。
- **布局快照还原覆盖标题**：`UiLayoutBuilder` 构建 Dock 会设置 `objectName`（如 `SceneDock`/`PropertiesDock`）；`restoreLayoutSnapshot()` 依赖 `_workbench_dock_title` 属性恢复标题，config-driven 路径已在 `buildDockAreasFromConfig()` 中通过 `setProperty` 兜底。
- **3D 菜单**：3D 命令来自 `CommandCatalog3D`，菜单构建受 `BUILD_UI3D` 宏保护，与上一节约定一致。

---

## 8. 回归检查清单

- [ ] `SANYI_ENABLE_CONFIG_DRIVEN_UI=OFF` 时基线功能不变；
- [ ] `SANYI_ENABLE_CONFIG_DRIVEN_UI=ON` 时 `:/configs/san_yi.json` 能正常加载；
- [ ] Dock 能从 JSON 构建，`m_panelState.sceneTreeDock/propertiesDock/leftDock/rightDock` 仍被正确填充（供 `WorkbenchWindow::sceneTreeDock()`/`setSkeletonDocksVisible()` 使用）；
- [ ] `ClientConfigTests` 全部通过；
- [ ] 全量 `MainTests` 回归无新增失败。
