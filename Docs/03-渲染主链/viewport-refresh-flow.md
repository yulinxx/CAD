# 视口刷新流程

本文只描述当前有效的刷新流程与当前建议的收口方向，不保留历史版本记录。

---

## 1. 刷新目标

视口刷新链的目标是把文档变化稳定地传递到渲染层，并把最终结果显示到屏幕。

---

## 2. 当前流程

### 2.1 2D 刷新链

```text
Document / Scene
→ SceneEditService
→ SceneRefreshCoordinator
→ RenderViewport2D
→ RenderWidget
→ Renderx / RenderX
→ screen
```

### 2.2 3D 刷新链（阶段 3：增量渲染）

```text
Document / Scene
→ SceneEditService3D
→ SceneRefreshCoordinator3D
→ RenderWidget3D
  → paintGL / renderScene()
    → FullRefresh:  syncMeshGeometry()        [全量重建]
    → LightUpdate: syncMeshGeometryIncremental() [按脏 ID 增量同步]
    → Repaint:     仅消费脏 ID，不做几何同步
→ Renderx / RenderX
→ screen
```

### 2.3 当前职责分工

- `SceneEditService` / `SceneEditService3D` 负责文档编辑事务
- `SceneRefreshCoordinator` / `SceneRefreshCoordinator3D` 负责刷新节流、增量与全量刷新协调
- `RenderViewport2D` / `RenderWidget3D` 负责宿主、输入和刷新触发
- `RenderWidget` / `RenderWidget3D` 负责渲染设备连接
- `Renderx` / `RenderX` 负责最终渲染执行

### 2.4 帧外上传必须自己 makeCurrent（2026-08-26 已修）

`SceneRefreshCoordinator` 是在**定时器回调**里调 `RenderWidget::submitSceneFromDataSource`
与 `addRenderEntity` 的 —— 不在 `paintGL` 内，因此没有当前 GL 上下文；而这两个入口会一路
走到 `rxGeometryAlloc / Write / Flush`，发出 `glGenBuffers / glBufferData / glBufferSubData`。

无当前上下文时这些调用在 Windows 上通常直接返回：**既不报错也不上传**。数据要等下一帧
帧内 flush 才偶然补上，症状就是"改完场景要动一下鼠标才刷新"。

现在这两个入口按需 `makeCurrent` / `doneCurrent`，且已在帧内时不重复切换。
约定：**任何会发 GL 调用的入口自己确认上下文当前性，不把这个前提外推给调用方**
（释放路径的同一条结论见《新渲染架构.md》§21.1，上传路径见 §22.2）。

---

## 3. 3D 增量渲染（阶段 3，2026-09-18 完成）

### 3.1 背景

3D 侧原本没有任何刷新调度 —— `RenderWidget3D::markSceneDirty()` 的函数体就是一句
`update()`，每帧在 `paintGL` 里从场景全量重收集顶点、逐图元重分配 VBO、逐图元
一次 draw call。只有"全量重画"一档。

### 3.2 分期路线图

| 阶段 | 状态 | 内容 |
|------|------|------|
| 阶段 1 | ✅ 完成 | 抽出 `ISceneRefreshScheduler` 公共契约 |
| 阶段 2 | ✅ 完成 | 3D 骨架实现：级别与脏 ID 记账，整帧重绘，QueuedConnection 节流 |
| 阶段 3 | ✅ 完成 | 接入常驻顶点仓与增量提交，FullRefresh 退为兜底 |
| 阶段 4 | 待做 | `markEntityDirty` 参与增量判定 |

### 3.3 阶段 3 核心改动

**增量路径（`syncMeshGeometryIncremental`）：**
- 不再调用 `gatherGeometry()` 扫描全场景
- 通过 `Mesh3DBuilder::updateDirtyEntities(dirtyIds, sm)` 只遍历脏 ID 集合
- 对每个脏图元：查找实体 → `emitTriangleSoup`（content hash 跳过内容未变的）→ 自动收录新图元
- 复杂度从 O(场景总数) 降为 O(脏图元数)

**paintGL 分流逻辑：**
```cpp
if (level >= FullRefresh)     → syncMeshGeometry()          // 全量重建
else if (level >= LightUpdate) → syncMeshGeometryIncremental() // 按脏 ID 增量
else if (level == Repaint)     → 仅消费脏 ID，不做几何同步
```

**flushPendingRefresh 同步语义修复：**
- 原实现走 `dispatch()`（异步 `update()`），导致脏 ID 在 `paintGL` 消费前被清空
- 现通过注入的 `FlushCallback` 同步执行（`update() + processEvents()`）

### 3.4 关键 API

| API | 位置 | 说明 |
|-----|------|------|
| `Mesh3DBuilder::initialize()` | UI/3D | 初始化 `GeometryManager`（几何仓 + 绘制列表） |
| `Mesh3DBuilder::updateDirtyEntities()` | UI/3D | 增量更新脏图元，通过 `GeometryManager` 写入几何仓 |
| `Mesh3DBuilder::removeEntity()` | UI/3D | 移除单个图元的几何与槽位（经 `GeometryManager` 回收） |
| `Mesh3DBuilder::setEntityVisible()` | UI/3D | 显隐对账，不重传顶点 |
| `GeometryManager::allocBlock()` | RenderBridge | 几何仓块分配（2D/3D 共用） |
| `GeometryManager::upsertDrawItem()` | RenderBridge | 绘制命令 upsert 到 DrawList |
| `SceneRefreshCoordinator3D::takePendingDirtyIds()` | UI/3D | 消费并清空脏 ID 集合 |
| `SceneRefreshCoordinator3D::takePendingDeletedIds()` | UI/3D | 消费并清空删除 ID 集合 |
| `SceneRefreshCoordinator3D::setFlushCallback()` | UI/3D | 注入同步刷新回调 |
| `SceneRefreshCoordinator3D::setRepaintHook()` | UI/3D | 重绘注入点（测试用，钉住「一次请求一次重绘」） |

---

## 4. 当前建议

1. 刷新触发与文档编辑分离。
2. 增量刷新与全量刷新分离。
3. 视口只做显示和事件转发，不直接保存文档事实。
4. 渲染层只负责绘制，不承担业务编排。
5. 一帧内的多次 `update()` / `dispatch()` 调用通过 `QMetaObject::invokeMethod`
   的 `QueuedConnection | UniqueConnection` 合并为一次，避免重复帧渲染。

---

## 5. 帧合并机制（2026-09-16）

### 5.1 问题

`ViewRenderCoordinator::requestRepaint()` 和
`SceneRefreshCoordinator3D::scheduleDispatch()` 可能被同一帧内多处代码
同时调用（如多个 overlay 变更、多次场景通知）。若每次调用都直接触发
`QOpenGLWidget::update()` 或 `dispatch()`，会导致同一帧多次渲染，
浪费 GPU 带宽并拖慢交互帧率。

### 5.2 方案

均改用 `QMetaObject::invokeMethod(obj, slot, Qt::QueuedConnection | Qt::UniqueConnection)`：

- `QueuedConnection`：将调用推入事件循环，延迟到下一帧才开始执行；
- `UniqueConnection`：同一对象同一槽函数若有待处理的 queued 调用，新调用会被丢弃；
- 保证每帧最多一次 `update()`（2D）或 `dispatch()`（3D）。

### 5.3 覆盖范围

| 位置 | 方法 | 节流方式 |
|------|------|----------|
| `ViewRenderCoordinator` | `requestRepaint()` | `invokeMethod(m_renderWidget, "update", ...)` |
| `SceneRefreshCoordinator3D` | `scheduleDispatch()` | `invokeMethod(this, lambda, Qt::QueuedConnection)` |
| `RenderWidget::setViewMatrix()` | 相机拖拽 | `invokeMethod(this, "update", ...)` |
| `RenderWidget3D::mouseMoveEvent()` | 变换/旋转/平移/框选 | `invokeMethod(this, "update", ...)` |

### 5.4 与 16ms 定时器的关系

- 16ms 定时器管「何时刷新」（批处理窗口）；
- QueuedConnection 管「一帧内刷几次」（合并同帧重复调用）；
- 两者互补：定时器到期后，QueuedConnection 保证只执行一次实际的渲染。

---

## 6. 实体显隐的刷新流（2026-09-18）

实体级显隐此前不进入刷新链：`SyEntity::setVisible()` 只 `setModified()` 标脏，
不产生场景变更记录，`readChanges()` 读不到，渲染侧只能全量重建。现在补上：

```text
setVisible(false/true) / setEntitiesVisible(ids, visible)
→ SyEntity 可见性回调（SceneManager 入场时注入）
→ recordChange(id, VisibilityChanged)
→ notifySceneChanged()
→ SceneRefreshCoordinator(2D) / SceneRefreshCoordinator3D
  → 16ms 节流合帧
  → 2D: LightUpdate 增量重提脏图元（可见性翻转触发 remove / add）
  → 3D: LightUpdate → syncMeshGeometryIncremental / Mesh3DBuilder::setEntityVisible
```

要点：

- 可见性变更只记 `VisibilityChanged`，**不推进 `structureRevision`**，场景树不重建；
- 批量显隐走 `setEntitiesVisible`，一次循环、回调逐条记变更，最终由 16ms 定时器合并为
  一帧增量重绘；
- 图层级显隐仍走 `LayerManager::setLayerVisible` → `notifySceneChanged()`，语义不同
  （图层显隐影响整层，实体显隐只影响单图元）。

### 6.1 实体锁定：不进刷新链（2026-09-20）

锁定是纯元数据，**不改变任何渲染结果**，因此刻意不进刷新链：

```text
setLocked(false/true) / setEntitiesLocked(ids, locked)
→ SyEntity 锁定回调（SceneManager 入场时注入）
→ recordChange(id, LockChanged)
→ [不调 notifySceneChanged]
→ UI 侧：refreshCommandUiState()（仅刷新 Lock/Unlock 菜单灰显）
```

- 与 `VisibilityChanged` 分列两个 kind：消费方能区分「需要重绘」与「只更新 UI 状态」；
- 场景树当前不显示锁定态，因此也不重建树（旧实现为刷新一个不存在的图标做 O(N) 重建）。

---

## 7. 当前需继续收口的点

- 继续减少视口内部的协调逻辑
- 继续统一 2D 和 3D 的刷新语义
- 继续保证文档变化只通过统一入口进入刷新链
