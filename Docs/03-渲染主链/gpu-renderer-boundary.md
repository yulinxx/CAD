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

### 2.1 IRenderBackend 接口

```cpp
class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;
    
    /// 初始化后端
    virtual void initialize(const RenderContext& ctx) = 0;
    
    /// 关闭后端
    virtual void shutdown() = 0;
    
    /// 渲染一帧
    virtual void render(const RenderFrame& frame) = 0;
    
    /// 调整视口尺寸
    virtual void resize(const Size2D& size) = 0;
    
    /// 获取后端能力
    virtual const BackendCapabilities& getCapabilities() const = 0;
};
```

### 2.2 RenderContext

```cpp
struct RenderContext
{
    Size2D viewportSize;
    float devicePixelRatio;
    bool vsyncEnabled;
    std::string backendName;
};
```

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
- 使用 QOpenGLContext 管理上下文
- 使用 QOpenGLFunctions 调用 OpenGL API
- 支持 OpenGL 3.3+

**关键文件**：
- `Main/Src/RenderCore/MinimalOpenGLBackend.h/cpp`

### 5.2 软件后端

**特点**：
- 使用 QPainter 进行 CPU 渲染
- 支持离屏渲染
- 作为硬件渲染的回退方案

**关键文件**：
- `Main/Src/RenderCore/SoftwareRenderer.h/cpp`

### 5.3 默认后端

**特点**：
- 自动选择最佳后端
- 优先使用 OpenGL 后端
- 失败时回退到软件后端

**关键文件**：
- `Main/Src/RenderCore/DefaultRenderBackend.h/cpp`

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
│ (Engine2D)      │                          │ (RenderCompat)   │
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

### 8.5 渲染流程（2026-07-16 更新）

```
RenderViewport2D::updateSceneRender()
    │
    ▼
RenderWidget::submitSceneFromDataSource(dataSource)
    │
    ├─→ renderBeginScene(m_device)     
    │       // 清除所有旧图元（RenderWorld::clearAllEntities），
    │       // 重置图元ID计数器，避免数据源场景与旧图元累积冲突
    │
    ├─→ SceneGeometrySinkAdapter sink(m_device)
    │
    ├─→ dataSource->gatherGeometry(sink)
    │       // 遍历场景图元，推送几何原语到渲染层
    │
    ├─→ renderEndScene(m_device)       
    │
    └─→ QOpenGLWidget::update()        // 触发 paintGL → renderFrame

paintGL() → renderFrame()
    │
    ├─→ rhi->beginFrame() / clear()
    ├─→ world2D.update()              // 脏图元顶点 → GPU 顶点池
    ├─→ world2D.queryVisible()        // 四叉树剔除
    ├─→ sceneEnv.render()             // 网格背景
    ├─→ batchQueue.submit() / render() // 批次绘制
    ├─→ overlayQueue.render()         // 叠加层（仅脏时上传 GPU）
    ├─→ meshManager.render()          // 仅在有 3D 实例时
    ├─→ rhi->endFrame() / present()
```

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
