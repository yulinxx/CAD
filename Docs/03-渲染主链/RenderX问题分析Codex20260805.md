# 渲染核心当前分析

本文只描述 RenderX 当前的结构、边界和后续收口方向，不保留历史阶段记录。

---

## 1. 当前定位

RenderX 当前承担统一渲染运行时的职责：

- 对外提供 C ABI
- 对内管理渲染世界、网格、叠加层、文本、场景环境
- 对 GPU 提供 RHI 封装
- 对上层提供 2D / 3D 统一渲染入口

当前生产目标名为 `SanYiRender`，源码位于 `Renderx/`。

---

## 2. 当前模块组成

| 模块 | 当前职责 |
|------|----------|
| `RenderDevice` | 统一持有渲染状态、资源与图元池 |
| `RenderWorld` | 2D 图元管理、顶点池、空间剔除、脏列表 |
| `BatchQueue` | 批次与间接绘制命令 |
| `OverlayQueue` | 交互叠加层 |
| `TextAtlas` / `ScreenTextRenderer` | 文本渲染 |
| `SceneEnv` | 场景环境层 |
| `MeshManager` | 3D 网格与实例 |
| `CommandEncoder` | 渲染命令编码 |
| `RenderGraph` | 渲染流程编排 |
| `PipelineStateManager` | 管线缓存 |
| `PersistentEntityManager` | 持久图元管理 |
| `rhi::IDevice` | 后端抽象 |

---

## 3. 当前数据流

### 3.1 2D

```text
SceneDocument2D
→ SceneGeometrySinkAdapter
→ RenderDevice
→ RenderWorld
→ BatchQueue / OverlayQueue / TextRenderer
→ RHI
→ Screen
```

### 3.2 3D

```text
SceneDocument3D / SceneManager3D
→ 3D 提交入口
→ RenderDevice
→ MeshManager / Instance 管理
→ RHI
→ Screen
```

---

## 4. 当前约束

1. 渲染层只负责绘制与资源管理，不承担文档真相。
2. UI 通过适配层连接渲染层，不直接操作渲染内部结构。
3. C ABI 对外只暴露句柄、POD 和明确函数入口。
4. 2D 与 3D 共享同一套渲染运行时，不再拆成多个旧渲染库。

---

## 5. 当前需要继续收口的方向

- 继续统一 2D / 3D 的提交语义
- 继续压缩 `RenderDevice` 的职责厚度
- 继续统一 C ABI 命名和错误约束
- 继续减少 UI 侧对渲染内部实现的直连
