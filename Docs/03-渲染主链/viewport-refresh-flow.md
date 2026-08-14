# 视口刷新流程

## 概述

本文档描述视口刷新的完整流程，包括触发条件、数据流转和优化策略。

---

## 1. 刷新触发条件

### 1.1 场景变化

| 变化类型 | 触发源 | 处理方式 |
|----------|--------|----------|
| **图元增删改** | `SceneDocument2D/3D` | 全量或增量编译 |
| **Selection 变化** | `SelectionSet` | 仅更新高亮批次 |
| **预览变化** | `BaseTool` | 仅更新预览批次 |
| **图层可见性变化** | `LayerManager` | 增量更新批次可见性 |

### 1.2 视口变化

| 变化类型 | 触发源 | 处理方式 |
|----------|--------|----------|
| **尺寸变化** | `resizeEvent` | 重新计算视图矩阵 |
| **缩放/平移** | `ViewCamera` | 更新相机矩阵 |
| **网格设置变化** | `GridSnapManager` | 更新网格批次 |
| **显示模式变化** | `ViewWidget` | 更新渲染状态 |

### 1.3 外部触发

| 触发源 | 处理方式 |
|--------|----------|
| **操作提交** | 全量刷新 |
| **操作取消** | 回退到之前状态 |
| **Undo/Redo** | 全量刷新 |
| **视图重置** | 全量刷新 |

---

## 2. 刷新流程

### 2.1 完整刷新流程

```
触发条件
    │
    ▼
SceneDocument2D::notifyChange()
    │
    ├─→ sigSceneChanged() 信号
    │       │
    │       ▼
    │   ViewRenderCoordinator::onSceneChanged()
    │       │
    │       ▼
    │   SceneCompiler::compile()
    │       │
    │       ├─→ 全量编译 (首次加载、视图重置)
    │       └─→ 增量编译 (图元变化)
    │       │
    │       ▼
    │   RenderData 更新
    │       │
    │       ▼
    │   BatchManager::updateBatches()
    │       │
    │       ▼
    │   RenderCoreRenderer::render()
    │       │
    │       ├─→ IRenderBackend::render()
    │       │       │
    │       │       ├─→ 清除缓冲区
    │       │       ├─→ 设置渲染状态
    │       │       ├─→ 绘制批次
    │       │       └─→ 交换缓冲区
    │       │
    │       ▼
    │   Viewport::update()
    │       │
    │       ▼
    │   屏幕呈现
    │
    └─→ 防抖处理
            │
            ▼
        QTimer::singleShot(16ms, this, &refresh)
```

### 2.2 选择刷新流程

```
SelectionSet::add/remove/clear()
    │
    ├─→ sigSelectionChanged() 信号
    │       │
    │       ▼
    │   ViewWidget::syncSelectionFromScene()
    │       │
    │       ▼
    │   SceneCompiler::compileSelection()
    │       │
    │       ▼
    │   BatchManager::updateHighlightBatch()
    │       │
    │       ▼
    │   RenderCoreRenderer::render()
    │       │
    │       ▼
    │   屏幕呈现
```

### 2.3 预览刷新流程

```
BaseTool::updatePreview()
    │
    ├─→ ViewRenderCoordinator::updatePreview()
    │       │
    │       ▼
    │   SceneCompiler::compilePreview()
    │       │
    │       ▼
    │   BatchManager::updatePreviewBatch()
    │       │
    │       ▼
    │   RenderCoreRenderer::render()
    │       │
    │       ▼
    │   屏幕呈现
```

### 2.4 相机刷新流程

```
ViewCamera::update()
    │
    ├─→ sigViewMatrixChanged() 信号
    │       │
    │       ▼
    │   ViewRenderCoordinator::onViewMatrixChanged()
    │       │
    │       ▼
    │   RenderState::viewMatrix 更新
    │       │
    │       ▼
    │   RenderCoreRenderer::render()
    │       │
    │       ▼
    │   屏幕呈现 (仅重绘，不重建批次)
```

---

## 3. 刷新优化策略

### 3.1 防抖机制

```cpp
// ViewRenderCoordinator 中的防抖
void ViewRenderCoordinator::scheduleRefresh()
{
    // 使用 QTimer 合并短时间内的多次刷新请求
    m_sceneRenderTimer.start(16); // 约 60fps
}

void ViewRenderCoordinator::onSceneRenderTimer()
{
    updateRenderData();
}
```

### 3.2 增量更新

```cpp
// SceneCompiler 中的增量编译
RenderData SceneCompiler::compileIncremental(
    const Document& doc,
    const DirtySet& dirty)
{
    // 只更新脏图元对应的批次
    for (const auto& id : dirty.entityIds)
    {
        updateBatchForEntity(id);
    }
    
    // 更新 selection 和 preview
    updateSelectionBatch();
    updatePreviewBatch();
    
    return m_renderData;
}
```

### 3.3 批次缓存

```cpp
// BatchManager 中的批次缓存
class BatchManager
{
public:
    void updateBatch(BatchId id, const RenderBatch& batch)
    {
        // 如果批次内容没有变化，跳过更新
        if (m_batches[id] == batch)
            return;
        
        m_batches[id] = batch;
        m_dirtyBatches.insert(id);
    }
};
```

### 3.4 相机变化优化

```cpp
// 相机变化时只更新渲染状态，不重建批次
void ViewRenderCoordinator::onViewMatrixChanged(const Mat3f& matrix)
{
    m_renderState.viewMatrix = matrix;
    renderCoreRenderer->render(); // 直接重绘，不编译
}
```

---

## 4. 刷新优先级

### 4.1 优先级顺序

| 优先级 | 刷新类型 | 延迟要求 |
|--------|----------|----------|
| **P0** | 预览刷新 | < 16ms |
| **P1** | 选择刷新 | < 16ms |
| **P2** | 相机刷新 | < 16ms |
| **P3** | 场景刷新 | < 100ms |
| **P4** | 全量刷新 | < 500ms |

### 4.2 冲突处理

当多个刷新请求同时发生时：
- 高优先级请求打断低优先级请求
- 同优先级请求合并处理
- 全量刷新期间暂停预览刷新

---

## 5. 关键组件

### 5.1 ViewRenderCoordinator

**职责**：协调渲染刷新流程

| 方法 | 职责 |
|------|------|
| `updateRenderData()` | 更新渲染数据（场景变化） |
| `updateRenderCamera()` | 更新相机（视口变化） |
| `updatePreview()` | 更新预览（工具交互） |
| `syncSelectionFromScene()` | 同步选择状态 |

### 5.2 SceneCompiler

**职责**：编译文档到渲染数据

| 方法 | 职责 |
|------|------|
| `compile()` | 全量编译 |
| `compileIncremental()` | 增量编译 |
| `compileSelection()` | 编译选择高亮 |
| `compilePreview()` | 编译预览数据 |

### 5.3 BatchManager

**职责**：管理渲染批次

| 方法 | 职责 |
|------|------|
| `addBatch()` | 添加批次 |
| `removeBatch()` | 删除批次 |
| `updateBatch()` | 更新批次 |
| `clearPreview()` | 清除预览批次 |

### 5.4 RenderCoreRenderer

**职责**：渲染调度

| 方法 | 职责 |
|------|------|
| `render()` | 执行渲染 |
| `setScene()` | 设置场景 |
| `resize()` | 调整尺寸 |

---

## 6. 刷新流程图

```
┌─────────────────────────────────────────────────────────────┐
│                    刷新流程总览                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   场景变化 ─────┐                                           │
│                 │                                           │
│   选择变化 ─────┤                                           │
│                 ▼                                           │
│   预览变化 ─────→ ViewRenderCoordinator                      │
│                 │                                           │
│   相机变化 ─────┤                                           │
│                 │                                           │
│                 ├─→ 防抖合并                                │
│                 │                                           │
│                 ├─→ SceneCompiler::compile()                │
│                 │       │                                   │
│                 │       ├─→ 全量编译                        │
│                 │       └─→ 增量编译                        │
│                 │                                           │
│                 ├─→ BatchManager::updateBatches()           │
│                 │                                           │
│                 ├─→ RenderCoreRenderer::render()            │
│                 │       │                                   │
│                 │       └─→ IRenderBackend::render()       │
│                 │                                           │
│                 └─→ Viewport::update()                      │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 7. 边界检查清单

### 添加新的刷新触发点时检查
- [ ] 是否正确触发了刷新信号？
- [ ] 是否使用了防抖机制？
- [ ] 是否选择了正确的编译策略？
- [ ] 是否会影响性能？

### 修改刷新流程时检查
- [ ] 是否破坏了增量更新？
- [ ] 是否正确处理了选择和预览？
- [ ] 是否影响了用户体验？

---

---

## 8. 刷新机制增强（2026-07-11 更新）

### 8.1 IUndoStack 刷新回调

**文件**：`UiCommandHandler.h/cpp`

**新增接口**：
```cpp
class IUndoStack
{
public:
    using RefreshCallback = std::function<void()>;
    virtual void setRefreshCallback(RefreshCallback callback) = 0;
};
```

**实现**：`DefaultUndoStack` 在 `push()`、`undo()`、`redo()` 时调用 `notifyRefresh()` 触发视图刷新。

**刷新流程**：
```text
命令提交 / Undo / Redo
    │
    ▼
UndoStack::push() / undo() / redo()
    │
    ▼
notifyRefresh()
    │
    ├─→ 检查防重入 (m_isNotifying)
    │
    ▼
RefreshCallback()
    │
    ▼
视口重绘
```

### 8.2 防重入保护

`DefaultUndoStack` 包含 `m_isNotifying` 标志，防止刷新回调中再次触发 undo/redo 导致的无限递归。

### 8.3 增量刷新支持

通过 `setRefreshCallback` 机制，支持视图层实现增量刷新策略：
- 命令提交后按需刷新
- Undo/Redo 后恢复完整状态
- 减少不必要的全量重绘

## 9. 变更记录

| 日期 | 版本 | 变更内容 | 作者 |
|------|------|----------|------|
| 2026-07-16 | 1.2.0 | 补充 Renderx 实际渲染刷新流程，替换旧的 RenderCore 架构描述 | 开发组 |
| 2026-07-11 | 1.1.0 | 补充刷新机制增强：IUndoStack 刷新回调、防重入保护、增量刷新支持 | 开发组 |
| 2026-07-10 | 1.0.0 | 初版：基于当前实现编写 | 架构组 |

## 10. Renderx 实际刷新流程（2026-07-16 更新）

> **注意**：本文档前 7 节描述的是旧 RenderCore 架构（`ViewRenderCoordinator`、`SceneCompiler`、`BatchManager`、`RenderCoreRenderer`、`IRenderBackend`），这些组件在当前 Renderx 渲染路径中**不存在**。以下为当前实际流程。

### 10.1 场景变化刷新

```
场景变化（图元增删改）
    │
    ▼
SceneDocument2D::notifyChange()
    │
    ▼
sigSceneChanged() 信号
    │
    ▼
RenderViewport2D::updateSceneRender()
    │
    ▼
RenderWidget::submitSceneFromDataSource(dataSource)
    │
    ├─→ renderBeginScene(m_device)
    │       // 清除所有旧图元（RenderWorld::clearAllEntities）
    │
    ├─→ SceneGeometrySinkAdapter sink(m_device)
    │
    ├─→ dataSource->gatherGeometry(sink)
    │       // 遍历场景图元，推送几何原语到渲染层
    │
    ├─→ renderEndScene(m_device)
    │
    └─→ QOpenGLWidget::update()   // 触发 paintGL
    │
    ▼
paintGL() → renderFrame()
    │
    ├─→ world2D.update()           // 脏图元顶点 → GPU 顶点池
    ├─→ world2D.queryVisible()     // 四叉树视锥剔除
    ├─→ batchQueue.submit()        // 按 PrimitiveType 排序
    ├─→ batchQueue.render()        // 间接绘制
    ├─→ overlayQueue.render()      // 叠加层（仅脏时上传 GPU）
    └─→ meshManager.render()       // 仅在有 3D 实例时
    │
    ▼
屏幕呈现
```

### 10.2 相机变化刷新

```
相机变化（缩放/平移）
    │
    ▼
RenderViewport2D 更新视图矩阵
    │
    ▼
QOpenGLWidget::update()   // 触发 paintGL
    │
    ▼
renderFrame()  // 仅重绘，不重建图元数据
    │
    ├─→ world2D.queryVisible()  // 重新计算可见性
    ├─→ batchQueue.submit()     // 按新视口重新组装批次
    └─→ batchQueue.render()
    │
    ▼
屏幕呈现
```

### 10.3 实际刷新优化

| 优化策略 | 实现位置 | 说明 |
|----------|----------|------|
| **图元清除** | `renderBeginScene` → `world2D.clearAllEntities()` | 每次数据源提交流前清除旧图元，避免累积 |
| **增量更新** | `RenderWorld::update()` | 仅更新 `m_dirtyList` 中的脏图元顶点 |
| **四叉树剔除** | `RenderWorld::queryVisible()` | 仅绘制视口内可见图元 |
| **批次缓存** | `BatchQueue::submit()` | 仅在可见图元变化时重建批次 |
| **叠加层缓存** | `OverlayQueue::render()` | 仅当 `m_dirty=true` 时重建合并缓冲区和上传 GPU |
| **3D 跳过** | `renderFrame()` | 仅当 `meshManager.getInstanceCount() > 0` 时渲染 3D |
