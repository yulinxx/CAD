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

### 2.3 帧外上传必须自己 makeCurrent（2026-08-26 已修）

`SceneRefreshCoordinator` 是在**定时器回调**里调 `RenderWidget::submitSceneFromDataSource`
与 `addRenderEntity` 的 —— 不在 `paintGL` 内，因此没有当前 GL 上下文；而这两个入口会一路
走到 `rxGeometryAlloc / Write / Flush`，发出 `glGenBuffers / glBufferData / glBufferSubData`。

无当前上下文时这些调用在 Windows 上通常直接返回：**既不报错也不上传**。数据要等下一帧
帧内 flush 才偶然补上，症状就是"改完场景要动一下鼠标才刷新"。

现在这两个入口按需 `makeCurrent` / `doneCurrent`，且已在帧内时不重复切换。
约定：**任何会发 GL 调用的入口自己确认上下文当前性，不把这个前提外推给调用方**
（释放路径的同一条结论见《新渲染架构.md》§21.1，上传路径见 §22.2）。


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
