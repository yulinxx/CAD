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

### 2.2 当前职责分工

- `SceneEditService` 负责文档编辑事务
- `SceneRefreshCoordinator` 负责刷新节流、增量与全量刷新协调
- `RenderViewport2D` 负责 2D 宿主、输入和刷新触发
- `RenderWidget` 负责 OpenGL 宿主和渲染设备连接
- `Renderx` / `RenderX` 负责最终渲染执行

---

## 3. 当前建议

1. 刷新触发与文档编辑分离。
2. 增量刷新与全量刷新分离。
3. 视口只做显示和事件转发，不直接保存文档事实。
4. 渲染层只负责绘制，不承担业务编排。

---

## 4. 当前需继续收口的点

- 继续减少视口内部的协调逻辑
- 继续统一 2D 和 3D 的刷新语义
- 继续保证文档变化只通过统一入口进入刷新链
