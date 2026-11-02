# 文档到渲染数据流程

本文只描述当前文档如何进入渲染链，不保留历史阶段记录。

---

## 1. 当前总流程

```text
Document / Scene
→ selection / 状态
→ render data
→ GPU renderer
→ viewport
→ screen
```

---

## 2. 各层职责

### 2.1 文档层

文档层保存：

- 图元
- selection
- 变换结果
- 提交结果
- 回滚状态

文档层不负责：

- 直接绘制
- GPU 资源管理
- 视口重绘策略

### 2.2 render data 层

render data 层负责：

- 提取图元信息
- 组织可绘制批次
- 组织 selection 高亮数据
- 组织预览数据

### 2.3 GPU renderer 层

GPU renderer 负责：

- 接收 render data
- 编译绘制帧
- 执行 GPU 绘制
- 输出最终屏幕结果

### 2.4 viewport 层

viewport 负责：

- 接收刷新请求
- 接收输入事件
- 承载渲染上下文
- 触发重绘

---

## 3. 当前 2D 链路

```text
User Input
→ Tool / OperationBus
→ SceneEditService
→ SceneDocument2D
→ SceneChanged
→ RenderViewport2D
→ RenderWidget
→ Renderx / RenderX
→ Screen
```

---

## 4. 完整刷新调用链（7 层跨 3 模块）

以下是从 Engine 变更到最终 GPU 渲染的完整调用链，横跨 UI → RenderBridge → RenderX 三个模块：

| 层级 | 模块 | 组件 | 职责 |
|------|------|------|------|
| 1 | Engine | `SceneEditService` / `SceneEditService3D` | 修改场景、图元增删改变换 |
| 2 | UI | `SceneRefreshCoordinator` / `SceneRefreshCoordinator3D` | 脏图元记账、16ms 节流合并、3D 增量与全量刷新协调 |
| 3 | UI | `RenderSceneBuilder` / `Mesh3DBuilder` | 从 SceneManager 提取几何、分解为顶点/索引、组织 DrawList |
| 4 | RenderBridge | `PersistentGeometryStore` | 常驻几何存储（2D/3D 共用）、段位哈希去重、脏条目更新 |
| 5 | RenderBridge | `GeometryManager` + `DrawList` | 几何仓块分配（`allocBlock`）、绘制命令 upsert（`upsertDrawItem`） |
| 6 | RenderBridge | `RenderSessionHost` | GL 上下文 makeCurrent/doneCurrent、`submitSceneFromDataSource` |
| 7 | RenderX | `renderSubmitGeometry` / `renderFrame` | GPU 执行：顶点上传、管线编译、Draw Call 提交、帧缓冲呈现 |

**关键节点详解**：

### 4.1 SceneRefreshCoordinator（UI 层 · 节流与调度）

当前为**上帝类**，承担多重职责：
- 脏图元收集（`recordDirty` / `recordDeleted`）
- 刷新级别判定（LightUpdate / FullRefresh / Repaint）
- 16ms 定时器节流合并
- 3D 侧脏 ID 集合管理（`takePendingDirtyIds` / `takePendingDeletedIds`）
- FlushCallback 同步刷新（`update() + processEvents()`）

**已知问题**：职责过多，后续需拆分为独立调度器。

### 4.2 RenderSceneBuilder（UI 层 · 几何提取与组装）

- 从 `SceneManager::readChanges()` 读取变更日志
- 对每个脏图元调用 `emitEntityGeometry` 分解为顶点
- `upsertEntities()` 逐个写入 `PersistentGeometryStore`
- 段位游标必须在每个实体处理前**无条件归零**（增量路径约束）
- 几何归属锁定为调用方传入的**真实实体 id**（不得用临时 clone 的 id）

### 4.3 PersistentGeometryStore（RenderBridge 层 · 常驻仓）

- 2D/3D 共用的几何存储
- 按段位哈希（几何 hash + 顶点坐标）判断是否需要重传
- `GeometryManager::allocBlock()` 分配几何块、`upsertDrawItem()` upsert 绘制命令到 DrawList

### 4.4 Frame Context（GL 上下文保证）

`SceneRefreshCoordinator` 定时器回调在**帧外**调用 GL 入口时，必须自己 `makeCurrent`：
- `submitSceneFromDataSource()` / `addRenderEntity()` 路径
- 已在帧内（`paintGL`）时不重复切换

---

## 5. 当前建议

1. 文档只通过统一编辑服务改变。
2. 渲染数据只通过统一适配层进入渲染器。
3. 视口只负责显示和事件转发。
4. 渲染层只负责绘制和资源管理。

---

## 6. 数据层深度审查结论

**当前无 P0 问题**。以下为渲染主链数据层的完整审查结论，覆盖脏标记、增量/全量刷新、几何生命周期、同步点、代码重复等维度。

### 6.1 脏标记与刷新升级策略

`SceneRefreshCoordinator` 的脏标记路径经过完整审查，关键设计点：

- **脏升级链路**：图元变更时调用 `recordDirty(id)`，删除时调用 `recordDeleted(id)`。定时器节流后根据集合规模决定刷新级别：
  - `pendingDirtyIds` 非空 → **LightUpdate**（增量刷新）
  - `pendingDeletedIds` 非空 或 图元数 > `kBulkChangeThreshold` → **FullRefresh**（全量刷新）
  - 两者为空 → **Repaint**（仅重绘，不重建几何）
- **抑制窗口安全**：16ms 节流窗口内的多次变更会合并为一次刷新，窗口结束后**必须**触发，不存在"事件被吞"情况。已验证定时器超时回调中 flush 逻辑无 early return 丢事件分支。
- **活对象复核防撤销误判**：每次刷新前对 `pendingDirtyIds` 中的 ID 做一次 `SceneManager` 活对象查询，若对象已不存在（例如撤销操作删除后又被记录脏），从集合中剔除，防止对已删除对象做无效几何重建。

### 6.2 增量与全量刷新的关键差异

| 维度 | LightUpdate（增量） | FullRefresh（全量） |
|------|---------------------|----------------------|
| 几何来源 | `SceneManager::readChanges()` 仅读变更日志 | `SceneManager::readAll()` 全量扫描 |
| 视口裁剪 | **无视口裁剪**：所有脏图元均重建 | **有视口裁剪**：`RenderViewport2D::visibleEntities()` 过滤 |
| DrawList 策略 | upsert 脏条目，其余保留 | 清空后全量重建 |
| 触发条件 | 脏图元数 ≤ `kBulkChangeThreshold` 且无删除 | 图元数 > 阈值 或 有删除操作 |

**`kBulkChangeThreshold = 16384` 说明**：这是一个经验值，基于"单个图元增量重建开销 vs 全量重绘带视口裁剪的开销"的折中。在 2D 场景下，16384 个图元的增量重建约 50-80ms，全量重绘带视口裁剪（视口内约 1000 个）约 10-20ms；但如果 16384 个图元全部在视口内，增量更快。此阈值**需要实机校准**，不同场景复杂度、GPU 型号、驱动版本都会影响最优值。

### 6.3 几何生命周期

`PersistentGeometryStore` + `GeometryManager` 的 GPU 几何块管理有明确的释放路径：

- **分配**：`GeometryManager::allocBlock(vertexCount, indexCount)` 从 GPU 池分配块，返回 blockHandle
- **引用**：blockHandle 存入 `DrawList` 条目，与实体 ID 绑定
- **释放**：
  - **增量路径**：脏图元 upsert 时，如果新几何的段位哈希与旧的相同，**跳过重传**；如果不同，旧 block 标记为可回收，新 block 分配
  - **全量路径**：清空 DrawList 后，遍历 `GeometryManager` 中所有 block，**未出现在新 DrawList 中的 block 立即回收**
  - **槽号管理**：每个几何块有唯一 `slotId`，全量重建时从 0 开始**单调增长**，上限约 1600 万（`uint32_t` 高位保留给标记位）
  - **GPU 回收时机**：回收标记写入队列后，在下一帧 `renderFrame` 提交前调用 `glDeleteBuffers` / `glDeleteVertexArrays`，同步等待 GPU 完成当前帧

### 6.4 CPU/GPU 同步点

- **主帧路径无 readPixels**：`readPixels` 仅出现在**导出路径**（`ExportDispatcher::exportToImage`），不在正常帧刷新或视口渲染中出现。这是 P0 级别的安全性确认 — `readPixels` 会强制 GPU 管线同步，在主帧路径出现会导致帧率暴跌。
- **帧外 makeCurrent**：定时器回调触发刷新时（不在 `paintGL` 帧内），必须调用 `RenderSessionHost::makeCurrent()` → `submitSceneFromDataSource()` → `doneCurrent()`。GL 上下文切换本身是轻量操作，但后续的顶点上传（`glBufferSubData`）是**异步**的，不阻塞 CPU。

### 6.5 代码重复：2D / 3D Builder 同构不共享

`RenderSceneBuilder`（2D，UI2D 模块）和 `Mesh3DBuilder`（3D，UI3D 模块）是同构的：
- 都从各自的 `SceneManager` 读取变更
- 都分解为顶点/索引缓冲区
- 都调用 `PersistentGeometryStore::upsertEntities()` 写入

两者**不共享代码**，原因：
- 2D 图元是 `Entity2D` 基类，3D 是 `Entity3D` 基类，类型层次完全不同
- 顶点布局不同（2D 有 z=0 强制平面，3D 有法线/UV/tangent）
- 未来如果 2D 需要简化管线（例如跳过法线上传），共享会增加耦合

结论：**当前不需要合并**，但如果未来出现 2.5D 或更多维度，应考虑模板化 Builder 基类。

### 6.6 关键边界约束

- **脏 ID 集合必须在每次刷新后清空**：`SceneRefreshCoordinator` 的 Repaint 分支检测到 `pendingDirtyIds` / `pendingDeletedIds` 非空时**升级为 LightUpdate**，正常 flush 后 clear 集合，防止脏 ID 永久滞留导致每次都走 Repaint 路径。

### 6.7 实机验证清单（需人工确认）

以下项在代码审查层面无问题，但需要实机测试确认：

| 验证项 | 原因 | 建议场景 |
|--------|------|----------|
| **200% DPI 下 1px 细线可见性** | DPI 缩放后，1px 线可能变成 <1 物理像素被 GPU 抗锯齿吞并 | Windows 显示设置 200% + 画一条 1px 水平线 |
| **1e5 坐标 float 抖动** | 顶点数据用 `float`（32 位），在 1e5 mm（100 米）量级时精度约 0.01mm，可能导致图元抖动 | 超大场景（100m × 100m）中画密排小线段，观察是否闪烁 |
| **LOD 切换闪烁** | 3D 侧 LOD 切换时，不同 LOD 级别的顶点数差异可能导致 DrawList 重建出现一帧黑屏 | 3D 场景中快速缩放相机，观察 LOD 切换瞬间 |
| **kBulkChangeThreshold 最优值** | 16384 是经验值，不同硬件/场景的拐点不同 | 准备 5000/10000/16384/30000 图元四组 benchmark，测 LightUpdate vs FullRefresh 耗时交叉点 |


