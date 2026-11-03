# UI-Engine 解耦迁移方案

> **背景**：曾提出一套"UI/Core 抽象接口 + Engine/Adapter 接口 + Config 渲染后端选择"的重构清单（A/B/C）。
> 本文先给结论：**原方案按原样执行会让框架变更差**，随后给出经过修订、可落地的迁移方案。
>
> **关联文档**：`Docs/01-当前架构/耦合性分析.md`、`渲染与视口边界.md`、`模块边界定义.md`。
> 本文不重复上述文档的总体边界描述，只聚焦"UI ↔ Engine 解耦"这一条线的**取舍与落地**。

---

## 0. 结论摘要

| 问题 | 结论 |
|---|---|
| 要不要把 UI 改成继承 `Core::IViewWidget` / 走 `IGeometry2D`？ | **不要**。抽象边界选错，且抽象是空壳 |
| "低耦合 / 跨平台 / 2D-3D 可切换"的目标要不要追？ | **要**，但换路径 |
| 真正该做的是什么？ | ① 清理死抽象；② 渲染后端抽象真正落地；③ UI→Engine 依赖收口；④ 目录统一 |
| 第一步？ | **只做 Phase 0（审计 + 止损）**，不动业务代码 |

---

## 1. 为什么不能按原方案 A/B/C 做

### 1.1 `EngineAdapter` 是死代码，且是空实现

实测：

| 指标 | 值 |
|---|---|
| 全仓（除 `Engine/Adapter` 自身）引用 `IGeometry2D` / `IGeometry3D` / `IEngineAdapter` 的次数 | **0** |
| `Engine/Adapter/Src` 总行数 | 342（`Geometry2DAdapter.cpp` 175 + `Geometry3DAdapter.cpp` 127 + `EngineAdapter.cpp` 40） |
| 实现性质 | 函数体全为 `return false;` / 返回空 `vector` / 注释写明"简化实现""存根实现" |

把 UI 迁到这样的接口上，等于**用不能工作的代码替换能工作的代码**。

### 1.2 边界选错了：UI 依赖的是"领域模型"，不是"算法"

`IGeometry2D` 只建模 boolean / offset / tessellate / hitTest，约占 UI 真实依赖的 5%。
UI 实际直接使用：

- 场景：`Engine2D/Core/SceneManager.h`、`Engine3D/SceneManager3D.h`
- 具体实体：`SyLine` / `SyCircle` / `SyArc` / `SyPolygon` / `SyBezier` / `SyText` / `SyGroup` / `SyMeshEntity`
- 选择 / 图层 / 吸附：`SelectionManager3D`、`LayerManager`、`GridSnapManager`
- 编辑 / 撤销：`SceneEditService`、`SceneUndoCommands3D`、`Engine/Edit/IUndoRedoCommand.h`
- 渲染桥：`Tessellator`、`SceneRenderContract`、`RenderBridge/*`

要在"算法"处加接口，只有两条路，都更差：

1. **复制一套实体类型 + 每次调用转换** → 巨大成本、性能损失、转换 bug 温床；
2. **`void*` / 不透明句柄** → 类型不安全，比现状更糟。

CAD 的 UI 与模型**天然共享文档模型**（OCCT 等内核亦然）。强行在 UI↔模型之间插接口，产出的是"贫血接口 + 泄漏抽象"。

### 1.3 `UI/Core` 是一套"平行且没人用"的抽象，与 `UI/Common` 重复

先澄清一处易引起质疑的表述：`Core::IViewWidget` **不是全仓 0 引用**，它在 `UI/Core` 内部有 **15 处自引用**（`IRenderer.h`、`IDrawTool.h`、`IApplication.h`、`RendererFactory.cpp`、`ApplicationFactory.cpp`）。
准确说法是：**业务代码层（`UI/2D`、`UI/3D`、`UI/Common`）0 引用** —— 实测业务层 `#include "Core/*.h"` 的次数为 **0**。

进一步核查发现：`UI/Core` 与 `UI/Common`/`UI3D` 存在**成对的重复抽象**，且真正被采用的是后者：

| 能力 | `UI/Core`（业务层 0 引用） | 实际采用者 |
|---|---|---|
| 视口 | `Core::IViewWidget`（Core 内 15 处自引用） | `UI::IViewportHost` ← `RenderWidget3D` 实现 |
| 相机 | `Core::ICameraController` | `UI::ICameraController` ← `CameraController3D` 实现 |
| 渲染器 | `Core::IRenderer` + `createRendererFactory` | `UI3D::IRenderer3D` |
| 命令 | `Core::ICommand` / `ICommandCatalog` / `ICommandRegistry` | `UI::Plugin::ICommandPlugin` + `OperationBus` |
| 应用 | `Core::IApplication` | `Main/Src/Composition/ApplicationCompositionRoot` |
| 绘图工具 | `Core::IDrawTool` | 2D/3D 各自的 Tool 类 |

> 注：按名字粗搜会得到假阳性 —— 业务层的 `ICommand`（46 处）是 `UI::Plugin::ICommandPlugin`，`IRenderer`（2 处）是 `UI3D::IRenderer3D`，`ICameraController`（6 处）是 `UI::ICameraController`，都不是 `Core::` 那一套。

结论：**不是"再合并两套视口抽象"，而是 `UI/Core` 整层是无人实现、无人消费的平行设计。**
正确动作见 Phase 0：**要么整层删除，要么整层接线并删掉 `UI/Common` 的重复项（二选一，禁止并存）**。

### 1.4 真正可移植性的瓶颈在渲染，不在算法

- `RenderWidget : QOpenGLWidget`、`RenderWidget3D : QOpenGLWidget` —— 硬编码 OpenGL。
- `SANYI_DEFAULT_RENDER_BACKEND` 只在 `Config.cmake` 里 `find_package`，UI 模块并不消费它做后端切换。
- `SY_ENABLE_METAL_VIEWPORT` 有开关无实现。

原方案 B（把后端选择写进 Config）**单独做没有意义**：widget 仍然写死 GL。

---

## 2. 依赖现状清单（实测）

### 2.1 数量

UI 源码中直接 `#include` 低层模块的次数（去重前）：

| 顶层依赖 | 次数 |
|---|---:|
| `Engine2D/` | 235 |
| `Engine3D/` | 72 |
| `Engine/` | 42 |
| `RenderBridge/` | 16 |

按 UI 模块分布：

| 位置 | 次数 | 备注 |
|---|---:|---|
| `UI/2D/Src` | 197 | |
| `UI/3D/Src` | 59 | |
| `UI/2D/Include` | 36 | **公共头泄漏 Engine 类型（影响 UI 的 ABI）** |
| `UI/2D/Test` | 28 | 测试 |
| `UI/3D/Include` | 21 | **公共头泄漏** |
| `UI/Common/Include` | 9 | **公共头泄漏** |
| `UI/Common/Src` | 7 | |
| `UI/3D/Test` | 5 | |
| `UI/Common/Test` | 3 | |

> **关键**：`*/Include` 共 **66 处**。这说明 Engine 类型是 UI **公共接口的一部分**，不是实现细节。
> 因此"把 Engine 完全藏在 UI 内部"是不可能的，除非改公共 API —— 这正是不能用接口硬隔离的硬证据。

### 2.2 Top 20 具体头文件

| 次数 | 头文件 |
|---:|---|
| 37 | `Engine2D/Core/SceneManager.h` |
| 24 | `Engine3D/SyEntity/SyMeshEntity.h` |
| 20 | `Engine3D/SceneManager3D.h` |
| 20 | `Engine2D/Edit/SceneEditService.h` |
| 14 | `Engine/SyEntity/SyEntity.h` |
| 14 | `Engine2D/Interaction/LayerManager.h` |
| 14 | `Engine2D/SyEntity/SyLine.h` |
| 12 | `Engine3D/Selection/SelectionManager3D.h` |
| 8 | `Engine2D/SyEntity/SyPolygon.h` |
| 8 | `Engine2D/SyEntity/SyGroup.h` |
| 8 | `Engine2D/Render/EntityTessellateUtil.h` |
| 7 | `Engine2D/Interaction/GridSnapManager.h` |
| 6 | `Engine3D/Geometry/Geo3DTypes.h` |
| 6 | `Engine2D/SyEntity/SyBezier.h` |
| 6 | `Engine2D/SyEntity/SyCircle.h` |
| 6 | `Engine2D/SyEntity/SySmartLine.h` |
| 6 | `Engine/EntityIdGenerator.h` |
| 6 | `Engine2D/SyEntity/SyArc.h` |
| 5 | `Engine2D/Algorithm/ArrayParams.h` |
| 5 | `Engine3D/Edit/SceneUndoCommands3D.h` |

### 2.3 当前链接关系（Phase 0 基线）

| 目标 | PUBLIC | PRIVATE |
|---|---|---|
| `UICommon` | Utility, SQLiteCpp, UICore, EngineAdapter, EngineCommon, Engine2D | Engine3D, EnginePersistence |
| `UI2D` | Utility, EngineCommon, Engine2D, UICore, EngineAdapter, RenderX, RenderBridge, UICommon, Log | FileIO, opengl32 |
| `UI3D` | UICommon, Utility, EngineCommon, Engine3D, RenderX, RenderBridge, UICore, EngineAdapter | opengl32, FileIO |

### 2.4 死抽象 / 未落地清单

| 项 | 证据 | 处理 |
|---|---|---|
| `EngineAdapter`（`IEngineAdapter`/`IGeometry2D`/`IGeometry3D`） | 外部引用 0；实现为空壳 | 见 Phase 0 |
| `Core::IViewWidget` | 业务层引用 0（Core 内自引用 15） | 与 `UI::IViewportHost` 二选一 |
| `Core::ICameraController` | 业务层引用 0（Core 内自引用 25） | 与 `UI::ICameraController` 二选一 |
| `Core::IRenderer` + `createRendererFactory` | 业务层引用 0（Core 内自引用 24/3） | 与 `UI3D::IRenderer3D` 二选一；或升级为真后端抽象 |
| `Core::ICommand` / `ICommandCatalog` / `ICommandRegistry` | 业务层引用 0（Core 内自引用 40） | 与 `UI::Plugin::ICommandPlugin` + `OperationBus` 二选一 |
| `Core::IApplication` | 业务层引用 0 | 与 `ApplicationCompositionRoot` 二选一 |
| `Core::IDrawTool` | 业务层引用 0 | 删除或接线 |
| `sanyi_declare_module` | 定义 0 使用；24 个模块全走 `sanyi_add_shared_library` | 全量迁移或删除 |
| `SANYI_DEFAULT_RENDER_BACKEND` | 仅 `find_package`，UI 不消费 | Phase 1 接线 |
| `BUILD_UI_DIMENSION` | 仅 `UI/CMakeLists.txt`（独立工程）互斥；根工程不 gate | Phase 3 补根工程门控 |

### 2.5 已经正确、不要动的部分

- `Engine*` **不依赖** `UI*` / `Render*` / `Main`（已实测）。
- `RenderX` / `RenderBridge` **不依赖** `UI*`（已实测）。

底层方向是干净的。**本方案只做上层收口，不重排底层。**

---

## 3. 真实问题（按优先级）

1. **渲染后端是假的**：widget 写死 `QOpenGLWidget`，Metal/Vulkan 不可切换。
2. **UI→Engine 依赖面失控**：365 处直接 include，无单一收口点；其中 66 处在公共头。
3. **死抽象误导人**：`EngineAdapter`、`Core::IViewWidget/IRenderer` 看起来"已解耦"，实则 0 引用。
4. **CMake 抽象未落地**：`sanyi_declare_module` 0 使用。
5. **维度门控半生效**：根工程 `BUILD_UI2D`/`BUILD_UI3D` 默认都 ON。

---

## 4. 修订方案

### 4.0 原则

**共享稳定的，抽象易变的。**

| 类别 | 对象 | 策略 |
|---|---|---|
| 稳定、共享 | 领域模型（场景 / 实体 / 选择 / 撤销） | 保持为共享库，**不加接口** |
| 易变 | 渲染后端、平台、2D/3D 视口 | **抽象** |
| 按功能变化 | 算法 | 用**注册表**（已有 `OperationRegistry`），不要硬接口 |

---

### Phase 0 — 审计与止损 ✅ 已完成（2026-09）

**目标**：让"看起来怎样 = 实际怎样"，清出正确地基。

**实际执行结果**：

| 项 | 处理 | 验证 |
|---|---|---|
| `EngineAdapter` 整层 | **目录删除** + 从根/UI CMake 移除 | 全仓引用 0 |
| `UI/Core` 整层 | **目录删除** + 从根/UI CMake 移除 | 业务层 `#include "Core/*.h"` = 0 |
| `sanyi_declare_module` | 删除（0 引用） | 24 模块统一走 `sanyi_add_shared_library` |
| `sanyi_export_module` | 删除（0 引用，顺带发现） | — |
| `sanyi_add_module_sources` | 删除（0 引用） | — |
| `UI::IRenderSurface` | 删除（0 实现、0 include） | — |
| `SANYI_UICORE_DIR` / `SANYI_ENGINEADAPTER_DIR` | 从 `CMake/SanYiPaths.cmake` 删除 | — |
| `BUILD_UICORE` / `BUILD_ENGINEADAPTER` 选项 | 从 `Config.cmake` 删除 | — |

**验收**：`git grep` 中不存在"业务层 0 引用"的接口/宏；`SanYiCAD.exe` 构建通过。

---

### Phase 1 — 渲染后端：以"统一开关 + 删死抽象"收口（原计划已修订）

> **2026-09 实测修订**：原 Phase 1 假设"渲染后端没有抽象"，与事实不符 ——
> 后端**已经是运行时可选**的。因此原计划的大改造（把 widget 基类改为运行时、
> 引入 `IRenderBackend` / `RenderBackendFactory`）**成本高、风险大、收益低**，已取消。
> 保留的是真正存在的问题：两个后端开关互不相连 + 死抽象。

**已成立（无需改造）**：

| 能力 | 证据 |
|---|---|
| 运行时后端选择 | `RenderBridge/Src/RenderSessionHost.cpp:182-221` 按 `cfg.backend` 创建 Runtime |
| 多后端实现 | `Renderx` 支持 GL/Metal/Null/Vulkan；`renderx.h` 是唯一公共 ABI 头 |
| widget 传后端 | `RenderWidget.cpp:350`、`RenderWidget3D.cpp:1204` 设置 `cfg.backend` |
| 唯一公共视口接口 | `UI::IViewportHost`（`UI/Common/Include/UI/IViewportHost.h:35`） |

**实际执行**：

1. **统一两个后端开关** ✅
   `SY_ENABLE_METAL_VIEWPORT` 的默认值改为派生自 `SANYI_DEFAULT_RENDER_BACKEND`
   （`CMakeLists.txt` 视口开关段），并加一致性 WARNING；用户仍可显式覆盖。
   - 修复前问题：`-DSANYI_DEFAULT_RENDER_BACKEND=OPENGL`（Apple）仍会编译 Metal 视口。
2. **删除死抽象 `UI::IRenderSurface`** ✅（0 实现、0 include）。
3. **明确不做运行时基类切换**（决策）：
   唯一编译期耦合是 widget 基类（`QOpenGLWidget` vs `QWidget`），由
   `SY_ENABLE_METAL_VIEWPORT` 切换。该宏改变类定义，必须 PUBLIC 否则 ODR 冲突
   （`CMakeLists.txt:330-338`）。改成运行时需在裸 `QWidget` 上自管 GL 上下文，
   高风险且无实际收益——同一平台构建一次即可，**不做**。

**验收**：单一后端开关（`SANYI_DEFAULT_RENDER_BACKEND`）驱动；无 0 引用渲染抽象。

---

### Phase 2 — UI→Engine 依赖收口 + 方向强制 ✅ 已完成（2026-09）

**不做接口**，只收口依赖面。实际执行：

1. **`CMake/UiEngineAccess.cmake`** — INTERFACE 目标 `UiEngineAccess`，
   `INTERFACE` 链接 `EngineCommon/Engine2D/Engine3D/EnginePersistence`。
   在根与 `UI/CMakeLists.txt`（独立工程）里、Engine 目标之后定义。
2. **UI 模块改链门面** — `UI/Common`、`UI/2D`、`UI/3D` 的
   `target_link_libraries` 由 Engine 目标改为 `UiEngineAccess`，并删除各自重复的
   Engine 包含路径（改为从门面传递）。
   - 365 处 `#include "Engine*/..."` **未改动**；依赖从"散落各处"变为单一收口点。
   - 对外导出的 imported target 仍用 `EngineCommon::EngineCommon` 等（门面是构建内的）。
3. **`CMake/CheckDependencies.cmake`** — `sanyi_check_dependency_directions()`，
   在根 `CMakeLists.txt` 所有目标创建后执行；违规 `FATAL_ERROR`：
   - `Engine*` 不得链 `UI*` / `Render*` / `Main` / `UiEngineAccess`
   - `RenderX` / `RenderBridge` 不得链 `UI*` / `Main`
   - `UI*` 不得直接链 `EngineCommon/Engine2D/Engine3D/EnginePersistence`（只能经门面）
4. **CI**：`.github/workflows/ci.yml` 的 Configure 步骤自动触发该检查，违规即失败。

**验收（已验证）**：
- 配置输出：`[UiEngineAccess] ... 已定义` + `[SanYi] 依赖方向校验: OK`。
- 注入违规（给 `UICommon` 加 `Engine2D` 直链）→ configure 立即 `FATAL_ERROR`
  （`CheckDependencies.cmake:67`）；移除后恢复通过。
- Debug 全量构建通过，`SanYiCAD.exe` 生成。

---

### Phase 3 — 目录统一 + 维度门控 ✅ 已完成（2026-09）

**1. 2D/3D 目录骨架 — 实测已对齐，无需搬迁**

实测两者共有骨架完全一致：

```
共有: Adapter, Algorithm, Manager, Operation, Service, Settings, UI
仅2D: Action(空), Option(49 文件，域内专用)
仅3D: Edit, Plugin, Relief, Render, Shortcut(空), Storage, Tool(域内专用)
```

- `Action`、`Shortcut` 为空目录（且未被 git 跟踪）→ 已删除。
- 其余差异目录（`Option` / `Edit` / `Plugin` / `Relief` / `Storage` / `Tool`）是
  **真实域内功能**，统一骨架里没有对应类目；强行搬迁属主观且会打断
  **48 处** `#include "Option/..."` 等路径。**决定不搬迁**（避免为"整齐"付真实代价）。
- `Render` 已在骨架内（3D 有 `Src/Render`）。

**2. 根工程 `BUILD_UI_DIMENSION` 门控 — 已实现**

`Config.cmake` 新增唯一权威开关 `BUILD_UI_DIMENSION`（`2D|3D|BOTH`，默认 `BOTH`），
派生 `BUILD_UI3D` 与 `SANYI_DEFAULT_UI_DIMENSION`（`FORCE` 写回缓存，修复 2D→BOTH
粘滞）。非法取值 `FATAL_ERROR`。

| 取值 | UI2D | UI3D | 运行时默认 |
|---|---|---|---|
| `BOTH`（默认） | ON | ON | 2D |
| `2D` | ON | **OFF** | 2D |
| `3D` | ON | ON | 3D |

> 约束：UI2D 全应用必需（Main 数百处引用、无桩回退），任何取值都不关闭；
> 根 `CMakeLists.txt` 对 `BUILD_UI2D=OFF` 直接 `FATAL_ERROR`。

**验收（已验证）**：
- `cmake -DBUILD_UI_DIMENSION=2D` → `UI3D: DISABLED`，`SanYiCAD.slnx` 中 UI3D 引用 = **0**。
- `BOTH` → UI3D 重新 ON（粘滞已修复）。
- 非法值 → 配置期 `FATAL_ERROR`。

---

## 5. 与原方案 A/B/C 的对照

| 原方案 | 修订后 | 原因 |
|---|---|---|
| UI 继承 `Core::IViewWidget` | ❌ 不做 → 统一到 `UI::IViewportHost` | 与 Qt 基类打架，且已有可用抽象 |
| UI 通过 `IGeometry2D` 调算法 | ❌ 不做 → `UiEngineAccess` 门面收口 | 边界错、且是空壳 |
| OpenGL/Metal 选择进 Config | ✅ **已完成**（统一为 `SANYI_DEFAULT_RENDER_BACKEND` 单一开关） | 后端本就是运行时可选，缺的是开关统一 |
| 引入 `IRenderBackend` / `RenderBackendFactory` | ❌ 取消 | `RenderSessionHost` 已承担该职责，重复 |
| 运行时切换 widget 基类 | ❌ 不做 | 需裸 `QWidget` 自管 GL 上下文，高风险无收益 |
| 统一 2D/3D 目录 | ✅ 保留（Phase 3） | 低风险、可读性收益 |
| `sanyi_declare_module` | ✅ 已删除（0 引用） | 0 引用即技术债 |
| `BUILD_UI_DIMENSION` | ✅ 保留，补根工程门控 | 半生效 |

---

## 6. 风险

| 风险 | 缓解 |
|---|---|
| Phase 1 的 Metal/Vulkan 受 Qt 上下文生命周期绑定，成本最高 | 先在 Apple 打通 Metal 单后端；Vulkan 暂留接口 |
| 大范围移动文件打断 `git blame` / `AUTOMOC` | Phase 3 单独 PR；用 `git mv` |
| 团队并行开发冲突 | 每 Phase 独立分支 + CI 门禁 |
| 公共头已泄漏 Engine 类型（66 处），门面无法隐藏 | 门面只做"收口"，不追求"隐藏"；隐藏需另立公共 API 治理任务 |

---

## 7. 附录：Phase 0 删除/合并清单（执行用）

### 7.1 删除候选（0 引用）

| 路径 | 证据 | 动作 |
|---|---|---|
| `Engine/Adapter/**`（`IEngineAdapter.h` / `IGeometry2D.h` / `IGeometry3D.h` / `Src/*.cpp`） | 外部引用 0 | 从 `CMakeLists.txt` 移除 `add_subdirectory`；目录保留并加 `experimental/` 说明 |
| `UI/Core/**`（整层） | 业务层 `#include "Core/*.h"` = **0**；仅 Core 内部自引用 | **二选一**：① 整层删除（推荐，`UI/Common` 已有等价且被采用的抽象）；② 若保留则必须接线，并删除 `UI/Common` 的重复项（`IViewportHost` / `ICameraController` 等） |
| `CMake/Utils.cmake` 中 `sanyi_declare_module()` | 0 使用 | 全量迁移或删除 |

### 7.2 需保留但必须接线

| 路径 | 现状 | 接线目标 |
|---|---|---|
| `SANYI_DEFAULT_RENDER_BACKEND` | 仅 `find_package` | Phase 1 的 `RenderBackendFactory` |
| `BUILD_UI_DIMENSION` | 仅独立 UI 工程 | Phase 3 根工程门控 |

> `UI/Core` 若选择"保留并接线"（§7.1 方案②），则 `Core::ICommand` 需与 `UI::Plugin::ICommandPlugin` + `OperationBus` 统一，并同步删除 `UI/Common` 的重复抽象。

### 7.3 不要动

- `Engine*` → 不依赖 UI/Render（已验证，锁死即可）。
- `RenderX` / `RenderBridge` → 不依赖 UI（已验证，锁死即可）。
- 领域模型（`SceneManager` / `SyEntity` 族 / `SelectionManager`）→ 保持共享库，不加接口。

---

## 8. 执行顺序与进度

```
Phase 0  审计 + 删除死抽象            ✅ 已完成（目录删除，构建通过）
Phase 1  渲染后端收口（统一开关+删死抽象）✅ 已完成（不做运行时基类改造）
Phase 2  UiEngineAccess 门面 + CI 方向校验   ✅ 已完成（依赖收口 + 校验通过）
Phase 3  目录统一 + 维度门控                  ✅ 已完成（骨架本就对齐；BUILD_UI_DIMENSION 门控）
```

> 四个 Phase 全部完成。全程未改动业务逻辑语义：
> - 消除了"假解耦"（UI/Core、EngineAdapter、IRenderSurface、4 个死 CMake 宏）；
> - 后端开关统一为 `SANYI_DEFAULT_RENDER_BACKEND`；
> - UI→Engine 依赖收口到 `UiEngineAccess` 门面，并由 `CheckDependencies.cmake`
>   在 configure 阶段强制方向（CI 门禁）；
> - 根工程新增 `BUILD_UI_DIMENSION=2D|3D|BOTH` 门控。
