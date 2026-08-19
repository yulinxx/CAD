# 图形处理器渲染器边界

本文只描述当前有效的渲染边界，不保留历史路径。

---

## 1. 当前边界

当前生产渲染边界以 `Renderx` / `SanYiRender` 为准。

### 1.1 对外入口

```cpp
RenderDevice* renderCreateDevice(const DeviceDesc* desc);
void renderDestroyDevice(RenderDevice* dev);
void renderResize(RenderDevice* dev, uint32_t width, uint32_t height);
void renderSubmitGeometry(RenderDevice* dev, const GeometryPrimitive* primitive);
void renderFrame(RenderDevice* dev);
```

### 1.2 当前职责

- 接收几何与状态
- 编译并提交绘制任务
- 执行 GPU 绘制
- 输出到窗口或离屏缓冲区

### 1.3 当前禁止

- 修改文档
- 处理业务 selection 真相
- 处理用户输入
- 直接耦合 UI 对象
- 实现业务逻辑

---

## 2. 当前分层

### 2.1 UI 适配层

UI 层通过 `RenderWidget` 和 `SceneGeometrySinkAdapter` 连接渲染层。

### 2.2 渲染层

`Renderx` 内部负责：

- 几何接收
- 批次组织
- 资源管理
- OpenGL / Vulkan / Metal 后端实现

### 2.3 回退实现

无图形环境或测试环境可使用最小回退实现，但不作为业务主线。

---

## 3. 当前需要继续统一的点

- 统一 C ABI 命名
- 统一设备状态与统计接口
- 统一 2D / 3D 的提交语义
- 继续减少文档中的旧术语残留
