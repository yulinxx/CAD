# 渲染核心当前分析（联合参考稿）

本文只保留当前 RenderX 的结构判断与后续收口方向，不保留历史讨论过程。

---

## 1. 当前结论

RenderX 已经不是简单图形 API，而是统一渲染运行时。

当前生产目标名为 `SanYiRender`，源码位于 `Renderx/`。

---

## 2. 当前结构

| 组成 | 当前作用 |
|------|----------|
| C ABI | 对外统一入口 |
| `RenderDevice` | 运行时状态容器 |
| `RenderWorld` | 2D 图元与空间组织 |
| `MeshManager` | 3D 网格与实例 |
| `OverlayQueue` | 交互叠加层 |
| `TextAtlas` / `ScreenTextRenderer` | 文本渲染 |
| `RenderGraph` / `CommandEncoder` | 渲染组织 |
| `rhi::IDevice` | 平台后端抽象 |

---

## 3. 当前链路

### 3.1 2D

```text
SceneDocument2D
→ SceneGeometrySinkAdapter
→ RenderDevice
→ RenderWorld
→ BatchQueue / OverlayQueue
→ RHI
→ Screen
```

### 3.2 3D

```text
SceneDocument3D / SceneManager3D
→ 3D 提交入口
→ RenderDevice
→ MeshManager
→ RHI
→ Screen
```

---

## 4. 当前要求

1. 保持 `Renderx` / `SanYiRender` 作为唯一生产渲染目标。
2. 保持 UI 与渲染层职责分离。
3. 保持 C ABI 只输出稳定契约。
4. 继续清理旧渲染命名和旧历史术语。

---

## 5. 当前需要继续做的事

- 收紧 2D / 3D 提交接口
- 收紧 `RenderDevice` 的厚度
- 收紧 RHI 与上层的耦合
- 统一文档口径为当前现状
