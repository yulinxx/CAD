# 渲染核心当前分析（简版）

本文只保留当前 RenderX 的结论和下一步方向，不保留历史讨论过程。

---

## 1. 当前结论

RenderX 当前已经具备统一渲染运行时的形态，当前生产目标名为 `SanYiRender`，源码位于 `Renderx/`。

---

## 2. 当前结构

- 对外提供 C ABI
- 对内管理渲染世界、网格、叠加层、文本、场景环境
- 对 GPU 提供 RHI 封装
- 对上层提供 2D / 3D 统一渲染入口

---

## 3. 当前链路

### 3.1 2D

```text
SceneDocument2D
→ SceneGeometrySinkAdapter
→ RenderDevice
→ RenderWorld
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

## 4. 当前约束

1. 生产路径统一为 `Renderx` / `SanYiRender`。
2. UI 不直接依赖渲染内部实现。
3. C ABI 不暴露不稳定容器布局。
4. 继续清理旧命名和旧历史路径。

---

## 5. 下一步

- 统一 2D / 3D 提交入口
- 统一设备状态与统计接口
- 统一文档中的术语
- 继续减少过渡层职责
