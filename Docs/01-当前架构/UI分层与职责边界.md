# UI 分层与职责边界约定

> **状态（2026-10-07）**：本文档**定义约定**（应然），并记录**现状偏差**（实然）。
> 目标是解决「Main/Src/UI 与 UI/2D/UI3D/UI/Common 职责重叠、视口渲染逻辑
> 横跨 4 处」这一最大结构性混乱源。代码迁移属后续 P2 任务，本文档先冻结边界。

---

## 1. 依赖方向（已成立，务必保持）

```
Main/Src/UI  ──依赖──▶  UI/2D, UI/3D, UI/Common
UI/2D        ──依赖──▶  UI/Common
UI/3D        ──依赖──▶  UI/Common
UI/Common    ──依赖──▶  Engine/Utility/Log
```

**单向依赖，UI/2D、UI/3D、UI/Common 绝不反向依赖 Main**（已验证：
全仓仅 3 处注释提及 Main，无代码 include）。这是当前架构最健康的一条，
任何重构不得破坏。

---

## 2. 四层职责定义（应然）

| 层 | 目录 | 职责 | 不应包含 |
|---|---|---|---|
| **应用装配层** | `Main/Src/UI/` | 工作台宿主（窗口生命周期、工作台切换）、视口编排（输入路由、刷新协调）、UI 服务聚合（UiStateCenter/UiServices）、文档（SceneDocument2D）、UI 配置化（JSON 驱动） | 渲染内核、具体绘图工具、属性面板实现 |
| **通用组件层** | `UI/Common/` | 跨 2D/3D 的通用件：对话框框架、命令内核、交互契约、持久化、插件、快捷键、设置 | 任何 2D/3D 专属逻辑、渲染内核 |
| **2D 组件层** | `UI/2D/` | 2D 可复用组件：绘图工具（DrawTools）、操作总线（OperationBus）、属性面板（Option）、**渲染内核（RenderWidget）** | 工作台宿主、窗口生命周期 |
| **3D 组件层** | `UI/3D/` | 3D 可复用组件：渲染内核（RenderWidget3D + IRenderer3D 接口）、3D 服务（SceneDocument3D/CameraController3D）、操作、浮雕、工具 | 工作台宿主、窗口生命周期 |

**判断一个类该放哪里的规则**：
- 它管理窗口/工作台生命周期吗？→ **应用装配层**
- 它是 2D 和 3D 都要用的通用件吗？→ **通用组件层**
- 它只在 2D 工作台里用，且可独立复用吗？→ **2D 组件层**
- 它是渲染内核（直接调 RenderX/QOpenGLWidget）吗？→ **对应维度的组件层**

---

## 3. 现状偏差（实然）—— 视口渲染逻辑横跨 4 处

这是当前最痛的混乱。同一职责（视口渲染）被拆到两个模块、四个位置：

### 2D 链路（两轨）

| 角色 | 类 | 位置 | 行数 |
|---|---|---|---|
| 视口编排 | `RenderViewport2D`（QWidget + IViewportHost，持有 RenderWidget） | `Main/Src/UI/Render/` | — |
| 渲染内核 | `RenderWidget`（QOpenGLWidget + RenderX） | `UI/2D/Src/UI/ViewWidget/` | 1342 |
| 视图协调 | `ViewRenderCoordinator` | `UI/2D/Src/UI/ViewWidget/` | — |
| 场景构建 | `RenderSceneBuilder` | `UI/2D/Src/UI/ViewWidget/` | — |

> `UI/2D/Include/UI2D/UI2D.h` 注释已自认：「现役视口在
> `Main/Src/UI/Render/`（RenderViewport2D + Camera2D + ViewportInputRouter）」。

### 3D 链路（三轨，更乱）

| 角色 | 类 | 位置 | 行数 |
|---|---|---|---|
| 视口编排 | `UiViewport3D` | `Main/Src/UI/Render/` | — |
| 适配层 | `RenderWidget3DAdapter`（RenderWidget3D → IRenderer3D） | `Main/Src/UI/Render/` | — |
| 统一接口 | `IRenderer3D` | `UI/3D/Include/UI3D/Render3D/` | — |
| 渲染内核 | `RenderWidget3D`（旧 Qt OpenGL widget） | `UI/3D/Src/Render/` | **3469** |
| 刷新协调 | `SceneRefreshCoordinator3D` | `UI/3D/Src/Render/` | — |

### 由此产生的具体问题

1. **同一职责横跨两模块**：「视口」拆成「编排（Main）+ 内核（UI/2D·3D）」，
   改一个视口行为要同时动两个模块，影响面难以判断。
2. **2D/3D 不对称**：3D 有 `IRenderer3D` 接口 + `RenderWidget3DAdapter`
   适配器（UI 层不直接依赖旧 RenderWidget3D），但 2D **没有**对应的
   `IRenderer2D` 接口，`RenderViewport2D` 直接持有具体 `RenderWidget`。
3. **Main/Src/UI/Render 混杂两个维度**：同一目录下既有 2D 视口
   （RenderViewport2D/ViewportInputRouter）又有 3D 视口
   （UiViewport3D/RenderWidget3DAdapter），维度未隔离。

---

## 4. 收敛方向（P2 待办，非本文档执行）

两条可选路线，二选一：

### 路线 A：编排下沉（推荐）
把「视口编排」从 Main 下沉到对应维度组件层，Main 只保留工作台宿主：
- `RenderViewport2D` + `ViewportInputRouter` + `ViewportNavigation2D` → `UI/2D`
- `UiViewport3D` + `RenderWidget3DAdapter` → `UI/3D`
- Main/Src/UI/Render 只留 `SceneRefreshCoordinator`（跨维度刷新编排）与
  `EntityToVertices`（离散化契约实现）

**收益**：视口内聚到组件层，Main 瘦身为纯装配；2D 可顺势补 `IRenderer2D`
对齐 3D 的 `IRenderer3D`。
**风险**：RenderViewport2D 持有 UiServices/UiStateCenter，下沉时需把这些
依赖改为接口注入（与 UiServices 收口任务联动）。

### 路线 B：内核上提
把「渲染内核」从 UI/2D·3D 上提到 Main/Src/UI/Render，与视口编排放一起：
- `RenderWidget`（UI/2D）→ `Main/Src/UI/Render`
- `RenderWidget3D`（UI/3D）→ `Main/Src/UI/Render`

**收益**：视口编排与内核同模块，改动集中。
**风险**：UI/2D、UI/3D 失去渲染内核，变为纯工具/面板库；但 RenderWidget
依赖 RenderX 与 Qt OpenGL，上提会让 Main 的渲染耦合加重，且与「Main 是
装配层不应含渲染内核」的约定冲突。**不推荐**。

> 无论哪条路线，3D 的 `RenderWidget3D`（3469 行）都应先按 P2 拆解，
> 否则迁移会把一个 God Class 整体搬运。

---

## 5. 立即可执行的小约定（无需大重构）

以下约定可立即落地，遏制混乱继续扩大：

1. **新增视口相关代码**：渲染内核（直接调 RenderX / QOpenGLWidget）放
   `UI/2D` 或 `UI/3D`；视口编排（输入路由、刷新协调、宿主接口）放
   `Main/Src/UI/Render`。不得在 `Main/Src/UI/Workbench` 里直接写渲染调用。
2. **2D/3D 隔离**：`Main/Src/UI/Render` 目录下，2D 与 3D 的文件不得
   互相 include；跨维度共享逻辑提升到 `UI/Common`。
3. **新增 3D 渲染能力**必须实现 `IRenderer3D` 接口，不得让 Main 直接
   持有具体 `RenderWidget3D`（沿用 RenderWidget3DAdapter 模式）。
4. **2D 对齐**：新增 2D 渲染接口时，优先抽 `IRenderer2D`（对齐 3D 的
   `IRenderer3D`），不再让 `RenderViewport2D` 直接持有具体 `RenderWidget`。
5. **不得新增 Main ↔ UI/2D·3D 的反向依赖**（UI 组件层 include Main）。

---

## 6. 与其他重构任务的联动

| 任务 | 与本边界的关系 |
|---|---|
| UiServices 收口（P0 进行中） | 路线 A 下沉视口时，RenderViewport2D 对 UiServices 的依赖需改为接口注入 |
| 拆解 RenderWidget3D（P2） | 3D 内核拆解是路线 A/B 的前置条件 |
| 拆解 Workbench2D（P2） | Workbench2D 瘦身后，视口编排职责更清晰地下沉 |
| SceneDocument3D 对齐 ISceneContext（P1） | 3D 文档接口收口后，RenderWidget3DAdapter 对 SceneDocument3D 的直依赖可收敛 |

---

## 7. 一句话总结

> **Main/Src/UI 是应用装配层（宿主+编排+服务+文档+配置化），UI/2D·3D 是
> 可复用组件层（工具+面板+渲染内核），UI/Common 是跨维度通用件。**
> 当前最大偏差是「视口渲染」横跨 Main 与 UI/2D·3D 两模块四处，
> 收敛方向推荐「编排下沉」（路线 A），但需先拆解 RenderWidget3D/Workbench2D。
