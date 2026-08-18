# GPU 渲染器边界

## 概述

本文档定义 GPU 渲染器的边界、接口和职责划分，确保渲染后端与上层逻辑的解耦。

---

## 1. 边界定义

### 1.1 GPU 渲染器职责

| 职责 | 说明 |
|------|------|
| **接收渲染帧** | 从上层接收 RenderFrame，包含批次数据和渲染状态 |
| **编译绘制帧** | 将 RenderFrame 转换为 GPU 可执行的绘制命令 |
| **执行 GPU 绘制** | 调用 OpenGL/其他 GPU API 执行绘制 |
| **资源管理** | 管理 GPU 资源（缓冲区、纹理、着色器） |
| **输出结果** | 将绘制结果输出到屏幕或离屏缓冲区 |

### 1.2 GPU 渲染器禁止

| 禁止操作 | 原因 |
|----------|------|
| **修改文档** | 渲染器不应该改变业务数据 |
| **处理 selection** | selection 是业务层职责 |
| **处理用户输入** | 输入处理是 UI 层职责 |
| **直接操作 UI 对象** | 渲染器不依赖 UI |
| **实现业务逻辑** | 渲染器只负责绘制 |

---

## 2. 接口定义

### 2.1 Renderx C API（当前生产边界）

当前生产路径不再以 `IRenderBackend` 作为主线描述，而是以 `Renderx` 的 C ABI 为边界：

```cpp
RenderDevice* renderCreateDevice(const DeviceDesc* desc);
void renderDestroyDevice(RenderDevice* dev);
void renderResize(RenderDevice* dev, uint32_t width, uint32_t height);
void renderSubmitGeometry(RenderDevice* dev, const GeometryPrimitive* primitive);
void renderFrame(RenderDevice* dev);
```

### 2.2 渲染设备状态（历史语境）

```cpp
struct RenderStats
{
    uint32_t entityCount;
    uint32_t visibleCount;
    uint32_t drawCallCount;
    uint32_t triangleCount;
    uint32_t lineCount;
    uint32_t pointCount;
    uint64_t gpuMemoryBytes;
};
```
> 说明：这里保留历史抽象，仅用于解释旧文档中的状态命名；当前生产边界以 `RenderDevice*` 和 `render/render.h` 为准。

### 2.3 RenderFrame

```cpp
struct RenderFrame
{
    std::vector<RenderBatch> batches;
    RenderState state;
    FrameStatistics stats;
};
```

### 2.4 RenderBatch

```cpp
struct RenderBatch
{
    BatchId id;
    BatchType type;
    PrimitiveType primitiveType;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Material material;
    bool visible;
};
```

### 2.5 RenderState

```cpp
struct RenderState
{
    ClearColor clearColor;
    bool depthTestEnabled;
    bool antialiasingEnabled;
    bool wireframeMode;
    Mat4x4 viewMatrix;
    Mat4x4 projectionMatrix;
};
```

---

## 3. 渲染流程

### 3.1 渲染帧构建

```
RenderData
    │
    ▼
BatchManager::buildRenderFrame()
    │
    ├─→ 合并批次
    ├─→ 排序批次
    ├─→ 设置渲染状态
    └─→ 填充统计信息
    │
    ▼
RenderFrame
```

### 3.2 GPU 绘制

```
RenderFrame
    │
    ▼
IRenderBackend::render()
    │
    ├─→ 清除缓冲区
    │
    ├─→ 设置渲染状态
    │       ├─→ 视口
    │       ├─→ 深度测试
    │       ├─→ 混合模式
    │       └─→ 着色器参数
    │
    ├─→ 绘制批次
    │       ├─→ 绑定缓冲区
    │       ├─→ 设置材质
    │       └─→ 执行绘制调用
    │
    └─→ 交换缓冲区
```

---

## 4. 资源管理

### 4.1 资源生命周期

```
初始化
    │
    ▼
创建 GPU 资源 (缓冲区、纹理、着色器)
    │
    ▼
渲染帧期间使用
    │
    ▼
释放 GPU 资源
    │
    ▼
关闭
```

### 4.2 资源类型

| 资源类型 | 用途 | 生命周期 |
|----------|------|----------|
| **顶点缓冲区** | 存储顶点数据 | 批次创建 → 批次销毁 |
| **索引缓冲区** | 存储索引数据 | 批次创建 → 批次销毁 |
| **纹理** | 存储图像数据 | 文档加载 → 文档关闭 |
| **着色器** | 存储渲染程序 | 后端初始化 → 后端关闭 |
| **帧缓冲区** | 存储渲染结果 | 视口创建 → 视口销毁 |

---

## 5. 后端实现

### 5.1 OpenGL 后端

**特点**：
- 由 `Renderx` 内部的 RHI 层管理上下文
- 直接面向平台 OpenGL API
- 支持 2D / 3D 统一渲染

**关键文件**：
- `Renderx/src/rhi/rhi_gl.cpp`

### 5.2 Null / 回退后端

**特点**：
- 作为无图形环境或测试环境的回退实现
- 用于保持设备创建链路可用
- 不承担业务渲染语义

**关键文件**：
- `Renderx/src/rhi/rhi_null.cpp`

### 5.3 设备创建策略

**特点**：
- 由 `renderCreateDevice()` 统一创建渲染设备
- 后端选择由 `Renderx` 内部完成
- 失败时回退到可用的最小实现

**关键文件**：
- `Renderx/src/c_api/render_c_api_device.cpp`
- `Renderx/src/c_api/render_runtime.cpp`

---

## 6. 能力查询

### 6.1 BackendCapabilities

```cpp
struct BackendCapabilities
{
    bool supportsDepthTest;
    bool supportsAntialiasing;
    bool supportsWireframe;
    bool supportsTextures;
    int maxTextureSize;
    int maxVertexCount;
    std::string vendor;
    std::string renderer;
    std::string version;
};
```

### 6.2 能力检测

```cpp
// 在后端初始化时检测能力
BackendCapabilities MinimalOpenGLBackend::getCapabilities() const
{
    BackendCapabilities caps;
    caps.supportsDepthTest = true;
    caps.supportsAntialiasing = true;
    // ... 查询 OpenGL 扩展
    return caps;
}
```

---

## 7. 边界检查清单

### 修改渲染器时检查
- [ ] 是否引入了业务逻辑？
- [ ] 是否依赖了 UI 类型？
- [ ] 是否修改了公共接口？
- [ ] 是否正确管理了 GPU 资源？

### 使用渲染器时检查
- [ ] 是否通过 IRenderBackend 接口访问？
- [ ] 是否正确构建了 RenderFrame？
- [ ] 是否正确管理了资源生命周期？

---

## 8. 数据层与渲染层解耦（2026-07-15 更新）

### 8.1 新架构概述

为实现渲染后端的可替换性，项目采用了 **ISceneGeometrySink / ISceneDataSource** 接口模式，通过 C API 桥接数据层和渲染层。

### 8.2 接口定义

#### ISceneGeometrySink（渲染层实现）

```cpp
class ISceneGeometrySink
{
public:
    virtual ~ISceneGeometrySink() = default;
    
    virtual void emitPolyline(const std::vector<Ut::Vec2d>& points, bool bClosed) = 0;
    virtual void emitCircle(const Ut::Vec2d& center, double radius) = 0;
    virtual void emitArc(const Ut::Vec2d& center, double radius,
                         double startAngle, double endAngle) = 0;
    virtual void emitEllipse(const Ut::Vec2d& center,
                             double radiusX, double radiusY,
                             double rotation,
                             double startAngle, double endAngle,
                             bool bFullEllipse) = 0;
    virtual void emitText(const Ut::Vec2d& position, const std::string& text) = 0;
    virtual void emitImagePlaceholder(const Ut::Vec2d& topLeft,
                                      const Ut::Vec2d& topRight,
                                      const Ut::Vec2d& bottomLeft,
                                      const Ut::Vec2d& bottomRight) = 0;
    virtual void emitTriangleSoup(const std::vector<Ut::Vec3f>& vertices,
                                  const std::vector<Ut::Vec3f>& normals) = 0;
};
```

#### ISceneDataSource（数据层实现）

```cpp
class ISceneDataSource
{
public:
    virtual ~ISceneDataSource() = default;
    virtual void gatherGeometry(ISceneGeometrySink& sink) const = 0;
};
```

### 8.3 C API 桥接

渲染层通过 C API 提供几何原语推送接口：

| C API 函数 | 对应几何原语 |
|------------|-------------|
| `renderEmitPolyline()` | 多段线 |
| `renderEmitCircle()` | 圆 |
| `renderEmitArc()` | 圆弧 |
| `renderEmitEllipse()` | 椭圆 |
| `renderEmitText()` | 文本 |
| `renderEmitImage()` | 图像占位 |

### 8.4 架构图

```
┌─────────────────┐     ISceneDataSource     ┌─────────────────┐
│   数据层         │ ───────────────────────► │   适配层         │
│ (Engine2D)      │                          │ (UI2D)           │
│ SceneManager    │                          │ SceneGeometry    │
└─────────────────┘                          │ SinkAdapter      │
                                             └────────┬────────┘
                                                      │ C API
                                                      ▼
                                             ┌─────────────────┐
                                             │   渲染层         │
                                             │  (Renderx)      │
                                             │  SanYiRender.dll│
                                             └─────────────────┘
```

### 8.5 渲染流程（2026-08-15 更新）

```
场景变化 → SceneRefreshCoordinator（16ms 节流，分级刷新）
    ├─→ LightUpdate → applyLightRefresh()（增量提交脏图元）
    └─→ FullRefresh → RenderWidget::submitSceneFromDataSource(dataSource)
            ├─→ renderBeginScene(m_device)
            │       // 清除所有旧图元（RenderWorld::clearAllEntities），
            │       // 重置图元ID计数器，避免数据源场景与旧图元累积冲突
            ├─→ SceneGeometrySinkAdapter sink(m_device)
            ├─→ dataSource->gatherGeometry(sink)
            │       // 遍历场景图元，推送几何原语到渲染层
            ├─→ renderEndScene(m_device)
            └─→ QOpenGLWidget::update()        // 触发 paintGL → renderFrame

paintGL() → renderFrame()
    ├─→ 数据准备（Pass 外）：
    │       ├─→ syncWorldToPersistentManager() + executeCulling()（GPU 剔除）
    │       ├─→ readBackGpuVisibility()（可见性回读，失效时回退 CPU 四叉树）
    │       └─→ batchQueue.submit()（按 PrimitiveType 排序组装批次）
    │
    ├─→ RenderGraph Pass 编排（renderGraph.execute）：
    │       ├─→ Pass 0 FrameSetup（清屏、混合、重置命令编码器）
    │       ├─→ Pass 1 SceneEnv（网格背景）
    │       ├─→ Pass 2 Bitmap（位图）
    │       ├─→ Pass 3 World2DCollect（world2D 命令 → CommandEncoder）
    │       ├─→ Pass 4 OverlayCollect（overlay 命令 → CommandEncoder）
    │       ├─→ Pass 5 CommandExecute（统一排序执行，经 PSM 绑定管线）
    │       └─→ Pass 6 Text（文本，条件启用）
    │
    └─→ rhi->endFrame() / present()
```

> 说明：早期版本（2026-07-16）记录的"world2D.update → queryVisible → sceneEnv.render →
> batchQueue.submit/render → overlayQueue.render → meshManager.render"线性流程已被
> RenderGraph 显式 Pass 编排 + GPU 剔除取代，详情见 `渲染管线.md` Phase 4 与
> `viewport-refresh-flow.md` §10。

### 8.6 更换渲染后端步骤

1. 实现新的 `ISceneGeometrySink` 适配器
2. 在适配器中调用新渲染后端的 API
3. 链接新的渲染库（替换 SanYiRender.dll）
4. 无需修改 `SceneManager` 或其他数据层代码

---

## 9. 变更记录

| 日期 | 版本 | 变更内容 | 作者 |
|------|------|----------|------|
| 2026-07-16 | 1.2.0 | 更新渲染流程为实际 Renderx 流程（renderFrame 详细步骤） | 开发组 |
| 2026-07-15 | 1.1.0 | 添加数据层与渲染层解耦方案：ISceneGeometrySink/C API 架构 | 架构组 |
| 2026-07-10 | 1.0.0 | 初版：基于当前实现编写 | 架构组 |
