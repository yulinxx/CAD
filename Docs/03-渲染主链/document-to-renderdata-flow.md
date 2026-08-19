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
→ Renderx / SanYiRender
→ Screen
```

---

## 4. 当前建议

1. 文档只通过统一编辑服务改变。
2. 渲染数据只通过统一适配层进入渲染器。
3. 视口只负责显示和事件转发。
4. 渲染层只负责绘制和资源管理。
