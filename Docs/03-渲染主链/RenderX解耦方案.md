# RenderX 解耦方案设计文档

> ## 落地结果（已实施）
>
> 本文是**规划稿**。下面是实际落地的状态与几处被取代的设计 —— 阅读后面的章节时请以本节为准。
>
> ### 达成的指标
>
> | 指标 | 规划目标 | 实测 |
> |------|----------|------|
> | 直接 `#include "render/renderx.h"` 的 UI 文件数 | 0 | **0** |
> | UI 代码中的 `Render::RT::*` 类型 / `rx*` 调用 | 0 | **0** |
> | Main 的 renderx.h 依赖 | — | **0** |
> | ABI 变更影响范围 | 仅 Adapter | 仅 RenderBridge 内部 |
> | 校验规则 | 规则 4 | 规则 4 + **5（include 路径）+ 6（传递闭包）**，默认硬失败 |
>
> 校验命令：`cmake -S . -B build`，输出应为
> `[SanYi] 依赖方向校验: OK（…; UI 通过 RenderAbstraction 访问渲染）`。
> 临时放行需显式传 `-DSANYI_STRICT_RENDER_ISOLATION=OFF`。
>
> ### 被取代的设计（本文后续章节仍按原样保留，作规划记录）
>
> | 规划 | 实际做法 | 为什么改 |
> |------|----------|----------|
> | `IRenderCommandList`（接口 3，独立文件） | **删除** | 与 `IRenderScene` 职责重叠；`IRenderScene` 自己就是命令提交入口 |
> | `CommandListHandle` + `createCommandList` / `submitToScreen` + 每帧重建整表 | **删除**，改成 `DrawListHandle` + `upsertDrawItem` / `submitDrawList` | 常驻场景的真实模型是**保留式增量**（只改动的图元重发），整表提交做不到增量 |
> | `IRenderScene::uploadGeometry()` 返回 `BufferHandle` | 换成 `IRenderDevice` 上的**常驻几何仓**接口（`createGeometryStore` / `allocGeometry` / `writeGeometry` / `freeGeometry`） | 几何仓的分片（多仓绕过单仓 4GB 上限）、块身份、脏区刷写都是业务层必须参与的事实，「给我一个 BufferHandle」表达不了 |
> | `IRenderFactory::createSurface` / `createScene` | **删除**，工厂只负责 `createDevice` | 表面/会话的创建依赖宿主侧事实（Qt 拥有 GL 上下文、Metal 交换链要挂宿主 NSView、Metal 的 Runtime 是进程级共享且销毁时机与最后一个视口对齐），`DeviceConfig`/`SurfaceConfig` 表达不了。产品实际入口是 `RenderBridge::RenderSessionHost` |
> | `NativeWindowHandle` / `SurfaceConfig` | **删除** | 只被上面两个被删的方法用到 |
> | 2D/3D 用同一个 AABB 契约 | `ViewVolume`（带类型标签：世界矩形 / 六平面视锥）+ 统一的 `Aabb3` | 2D 的矩形判据只用 x/y，斜视角下会误裁可见图元；两者是**不同的剔除判据**，不该共用一个结构 |
> | `IRenderDevice::getDefaultPipeline(格式,空间,拓扑)` | **删除**，保留 `defaultPipeline(PipelineKind)` | 三元组解析不出「同格式同空间同拓扑、片元不同」的那几条（字形 vs 贴图、网格 vs 线框、高亮 vs gizmo），是个会静默给错管线的陷阱 |
> | RenderBridge 用 `PUBLIC` 转发 RenderX | 改为 **PRIVATE** | 这是规则 4 的绕过路径：UI 只链 RenderBridge 也能拿到 RenderX 的头路径与符号 |
> | `RenderBridge/Include/RenderBridge/HostCallbacks.h`（宿主回调） | **删除**，两个函数移进 `RenderSessionHost.cpp` 的文件内静态函数 | 公开头的前提是「多个调用方共用」；视口改走 `RenderSessionHost` 后取地址点只剩一处，公开头就没意义了。且它是 UI 侧最后一个能看见宿主桥接细节的入口 |
> | `RenderBridge/Include/RenderBridge/RenderUploadQueue.h` | **删除** | 全仓库零包含者。它照搬了后端的上传队列模型（`PrimitiveTopology`/`VertexFormat`/`RxAabb3`），而与它对应的命令表模型早已换成保留式绘制列表 |
> | `IRenderDevice::maxLineWidth` / `maxTextureSize` | **删除**，只保留 `backendName` / `deviceName` | 同样是「照搬后端能力表」：线宽上限只有后端内部用（UI 要粗线自己三角化），纹理上限没人看。留下来的 `backendName` / `deviceName` 有真实调用者 —— 3D 视口的 GL 信息面板 |
>
> ### 落地时新增/发现并修掉的缺陷
>
> - `RenderXAdapter::createFactory()` 从未导出，且返回 `unique_ptr<RenderXFactory>`（调用方拿不到完整类型）—— 文档里写的公共入口一直不可用
> - 工厂路径的设备/表面/场景适配器都不销毁自己创建的句柄（资源泄漏）
> - `IRenderScene.h` 不自洽：用了 `IRenderDevice` 却没包含它，靠 include 顺序侥幸成立
> - `RenderXSurfaceAdapter::width()/height()` 恒为 0（`resize` 没记录尺寸）
> - 顶点步长契约原来只锁在 RenderX 侧；现在抽象层有 `RenderAbstraction::vertexStride()`，宿主结构体对着它做 `static_assert`，适配层另有一处运行时校验确认那张表与后端实际步长一致
> - 失败诊断原先散在 UI 调用点（UI 拼 `rxResultName`）；现已收进适配层，UI 只拿 bool
>
> ### 仍然存在的事实
>
> 产品有**两条**建设备的路径，不是一条：
>
> - `RenderBridge::RenderSessionHost` —— 产品入口。它自己建 runtime/surface/session（因为有 Metal 进程级共享、GL 宿主符号解析、销毁时机规则），然后把同一批句柄包成 `IRenderDevice` / `IRenderSurface` / `IRenderScene` 交给视口。
> - `RenderBridge::RenderXAdapter::createFactory()` —— 只建设备，用于不需要交换链的场景（契约测试等）。
>
> 两者的句柄所有权规则相反：宿主自建的句柄归宿主销毁，工厂建的 Runtime 归设备对象销毁。这一点在适配器构造函数里用显式的 `ownsRuntime` 表达。

## 1. 问题诊断

### 1.1 当前耦合现状

| 耦合层级 | 严重程度 | 表现 |
|----------|----------|------|
| **类型泄露** | 严重 | UI层直接使用 `Render::RT::DrawCommand`, `RuntimeHandle`, `SessionHandle` 等DLL内部类型 |
| **直接include** | 严重 | 20+个UI源文件直接 `#include "render/renderx.h"` |
| **命名空间污染** | 中等 | `namespace RT = Render::RT;` 在UI代码中广泛使用 |
| **构建依赖** | 中等 | UI2D/UI3D的CMakeLists直接链接RenderX |
| **ABI脆弱性** | 高 | `renderx.h` 任何字段变更都会导致UI层重新编译 |

### 1.2 耦合文件清单

**直接 `#include "render/renderx.h"` 的非Renderx/RenderBridge文件（10个）：**

| 文件 | 模块 | 使用的核心类型 |
|------|------|---------------|
| `UI/2D/Include/RenderWidget.h` | UI2D | RuntimeHandle, SurfaceHandle, SessionHandle, DrawCommand |
| `UI/2D/Include/Render/RenderSceneBuilder.h` | UI2D | RuntimeHandle, DrawListHandle, BufferHandle |
| `UI/2D/Include/Render/BitmapQuadBuilder.h` | UI2D | RuntimeHandle, TextureHandle, DrawCommand |
| `UI/2D/Include/Render/WorldTextQuadBuilder.h` | UI2D | RuntimeHandle, FontHandle, FontMetrics, GlyphInfo |
| `UI/3D/Include/UI3D/Render3D/RenderWidget3D.h` | UI3D | RuntimeHandle, SurfaceHandle, SessionHandle, DrawCommand, RxFrustum |
| `UI/3D/Include/UI3D/Render3D/Mesh3DBuilder.h` | UI3D | RuntimeHandle, BufferHandle, DrawListHandle, RxAabb3 |
| `UI/3D/Src/Render/GizmoRenderer3D.h` | UI3D | RuntimeHandle, SessionHandle, DrawCommand |
| `Main/Src/Common/AppInitializer.cpp` | Main | Backend, rxBackendName |
| `Tools/MetalViewportProto/main.cpp` | Tools | 全API（原型工具，非产品） |

**间接依赖（通过RenderBridge头文件泄露RenderX类型）：**

| 文件 | 泄露的类型 |
|------|-----------|
| `RenderBridge/Include/RenderBridge/PersistentGeometryStore.h` | 所有handle, GeometryBlock, DrawListHandle, RxResult |
| `RenderBridge/Include/RenderBridge/TextQuadBuilder.h` | RuntimeHandle, FontHandle, GlyphInfo, VertexFormat |
| `RenderBridge/Include/RenderBridge/OverlayScene.h` | SessionHandle, DrawCommand |
| `RenderBridge/Include/RenderBridge/RenderUploadQueue.h` | PrimitiveTopology, VertexFormat, RxAabb3 |
| `RenderBridge/Include/RenderBridge/RenderSessionHost.h` | 所有handles, Backend, RuntimeDesc |

### 1.3 核心问题总结

1. **类型泄露**：`Render::RT` 命名空间下的所有类型直接暴露在UI业务代码中
2. **无抽象层**：RenderBridge存在但只是"薄的API包装"，没有业务语义抽象
3. **双向依赖风险**：虽然 `CheckDependencies.cmake` 确保了Render不依赖UI，但UI对Render的直接依赖太深，一旦ABI变更影响面巨大
4. **替换成本高**：更换渲染后端需要修改几十个UI文件

---

## 2. 设计目标

1. **零类型泄露**：UI层代码绝不出现 `Render::RT::*` 类型
2. **可替换性**：RenderX可以独立升级/替换，不影响UI业务代码
3. **保持性能**：不引入额外的拷贝或间接层开销
4. **渐进式迁移**：支持逐步替换，不搞一刀切
5. **ABI隔离**：`renderx.h` 的变更只影响RenderBridge内部，不影响UI

---

## 3. 架构设计

### 3.1 新增模块：RenderAbstraction

在RenderBridge和UI之间创建一个纯抽象层：

```
┌─────────────────────────────────────────────┐
│              UI2D / UI3D / Main             │
│  只依赖: IRenderScene, IRenderDevice 等接口    │
├─────────────────────────────────────────────┤
│         RenderAbstraction (新增)             │
│  定义: 业务级类型 + 抽象接口                   │
│  不依赖: renderx.h                           │
├─────────────────────────────────────────────┤
│         RenderXAdapter (新增)                │
│  实现: 所有抽象接口                           │
│  依赖: renderx.h (仅此一处)                   │
├─────────────────────────────────────────────┤
│              RenderX DLL                     │
│         renderx.h (C ABI)                    │
└─────────────────────────────────────────────┘
```

### 3.2 分层规则（升级版 CheckDependencies）

```
规则 0（新）: RenderAbstraction 不得依赖 RenderX
规则 1（已有）: Engine 不得依赖 UI/Render/Main
规则 2（已有）: RenderX/RenderBridge 不得依赖 UI/Main
规则 3（已有）: UI 不得直接依赖 Engine
规则 4（新）: UI 不得直接依赖 RenderX（只能通过 RenderAbstraction）
```

### 3.3 业务类型定义

#### 核心抽象类型

```cpp
// RenderAbstraction/IRenderTypes.h
// 全部使用标准C++类型，零DLL依赖

namespace RA = RenderAbstraction;

// 句柄：完全用 uint64_t 替代 Render::RT::RuntimeHandle 等
struct DeviceHandle { uint64_t value = 0; };
struct SurfaceHandle { uint64_t value = 0; };
struct CommandListHandle { uint64_t value = 0; };
struct BufferHandle { uint64_t value = 0; };
struct TextureHandle { uint64_t value = 0; };

// 顶点格式（业务语义）
enum class VertexFormat {
    PositionColor,      // P3C3 位置+颜色
    PositionColorAlpha, // P3C4 位置+颜色+透明度
    PositionNormal,     // P3N3 位置+法线
    PositionUVColor,    // P2T2C4 位置+UV+颜色 (2D)
    WorldPosUVColor     // P3T2C4 位置+UV+颜色 (3D/World)
};

enum class PrimitiveType {
    Points, Lines, Triangles
};

enum class RenderSpace {
    World, Screen
};

// 绘制指令（业务语义）
struct DrawInstruction {
    BufferHandle vertexBuffer;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    PrimitiveType topology = PrimitiveType::Triangles;
    VertexFormat format = VertexFormat::PositionColor;
    RenderSpace space = RenderSpace::World;
    uint32_t pipelineIndex = 0;
    TextureHandle texture;
    uint64_t sortKey = 0;
    uint64_t userData = 0;
};

// 矩阵类型（用业务矩阵替代 RenderX 的 float[16]）
struct Matrix4x4 {
    float m[4][4];
    // ...
};

// 颜色（用业务类型）
struct ColorF {
    float r, g, b, a;
};

// 2D矩形
struct RectF { float x, y, width, height; };
struct BBox2D { Vec2D min, max; };
struct BBox3D { Vec3D min, max; };

// 光源（业务语义）
struct LightDesc {
    ColorF ambient;
    ColorF diffuse;
    Vec3F direction; // 指向光源
};

// 相机
struct CameraDesc {
    Matrix4x4 viewMatrix;
    Matrix4x4 projectionMatrix;
    RectF viewport; // x,y,width,height
};

// 帧统计
struct FrameStatistics {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t lines = 0;
    uint32_t points = 0;
    uint64_t gpuMemoryBytes = 0;
};
```

### 3.4 抽象接口设计

#### 接口 1: `IRenderDevice`

```cpp
// RenderAbstraction/IRenderDevice.h

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    
    // 设备信息
    virtual StringView backendName() const = 0;
    virtual float maxLineWidth() const = 0;
    
    // 资源创建
    virtual BufferHandle createBuffer(uint64_t sizeBytes, bool cpuWritable) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;
    virtual void uploadBuffer(BufferHandle buffer, uint64_t offset, uint64_t sizeBytes, const void* data) = 0;
    
    virtual TextureHandle createTexture(uint32_t width, uint32_t height, const ColorF* rgba, uint64_t rgbaBytes) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    
    // 管线
    virtual uint32_t getDefaultPipeline(VertexFormat format, RenderSpace space, PrimitiveType topology) = 0;
    virtual uint32_t createPipeline(const PipelineDesc& desc) = 0;
};
```

#### 接口 2: `IRenderSurface`

```cpp
// RenderAbstraction/IRenderSurface.h

class IRenderSurface {
public:
    virtual ~IRenderSurface() = default;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual bool isValid() const = 0;
};
```幾

#### 接口 3: `IRenderCommandList`

```cpp
// RenderAbstraction/IRenderCommandList.h

class IRenderCommandList {
public:
    virtual ~IRenderCommandList() = default;
    
    // 提交绘制指令
    virtual void submit(const DrawInstruction& cmd) = 0;
    virtual void submit(std::initializer_list<const DrawInstruction&> cmds) = 0;
    
    // 变换
    virtual void setViewMatrix(const Matrix4x4& view) = 0;
    virtual void setProjectionMatrix(const Matrix4x4& proj) = 0;
    virtual void setModelMatrix(const Matrix4x4& model) = 0;
    
    // 光照（仅3D）
    virtual void setLighting(const LightDesc& lighting, const Vec3F& viewPos) = 0;
    
    // 清除
    virtual void setClearColor(const ColorF& color) = 0;
    
    // 查询
    virtual FrameStatistics getStatistics() const = 0;
};
```

#### 接口 4: `IRenderScene`

```cpp
// RenderAbstraction/IRenderScene.h

class IRenderScene {
public:
    virtual ~IRenderScene() = default;
    
    // 场景管理
    virtual CommandListHandle createCommandList(uint32_t initialCapacity = 4096) = 0;
    virtual void destroyCommandList(CommandListHandle list) = 0;
    virtual void clearCommandList(CommandListHandle list) = 0;
    
    // 几何
    virtual BufferHandle uploadGeometry(const void* vertices, uint64_t sizeBytes, bool persistent = true) = 0;
    
    // 提交
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void submitToScreen(CommandListHandle list) = 0;
    virtual void submitToSurface(CommandListHandle list, SurfaceHandle surface) = 0;
    
    // 离屏渲染
    virtual TextureHandle createRenderTarget(uint32_t width, uint32_t height) = 0;
    virtual void readPixels(TextureHandle target, void* outPixels) = 0;
    
    // 统计
    virtual FrameStatistics getFrameStatistics() const = 0;
};
```

#### 接口 5: `IRenderFactory`

```cpp
// RenderAbstraction/IRenderFactory.h

class IRenderFactory {
public:
    virtual ~IRenderFactory() = default;
    
    // 设备创建
    virtual std::unique_ptr<IRenderDevice> createDevice(const RenderBackend& backend, 
                                                        LogCallback logger) = 0;
    
    // 表面创建
    virtual std::unique_ptr<IRenderSurface> createSurface(IRenderDevice& device,
                                                           NativeWindowHandle window) = 0;
    
    // 场景创建
    virtual std::unique_ptr<IRenderScene> createScene(IRenderDevice& device) = 0;
};
```

### 3.5 适配层实现：RenderXAdapter

```cpp
// RenderBridge/RenderXAdapter/
// 这个目录完全在 RenderBridge 内部实现，对UI透明

class RenderXDeviceAdapter : public IRenderDevice {
    // 实现所有纯虚函数
    // 内部持有 Render::RT::RuntimeHandle
    // 所有调用转发到 renderx.h 的 rx* 函数
};

class RenderXSurfaceAdapter : public IRenderSurface {
    // 持有 Render::RT::SurfaceHandle
};

class RenderXSceneAdapter : public IRenderScene {
    // 持有 Render::RT::SessionHandle
    // 内部使用 PersistentGeometryStore
    // DrawInstruction → 转换为 DrawCommand
};

class RenderXFactory : public IRenderFactory {
    std::unique_ptr<IRenderDevice> createDevice(...) override;
    std::unique_ptr<IRenderSurface> createSurface(...) override;
    std::unique_ptr<IRenderScene> createScene(...) override;
};
```

### 3.6 关键转换逻辑

DrawInstruction → DrawCommand 的转换封装在 RenderXSceneAdapter 内部：

```cpp
// 转换在 .cpp 中完成，UI完全不感知

DrawCommand toRenderXCmd(const DrawInstruction& cmd) {
    DrawCommand rc{};
    rc.vertexBuffer = static_cast<Render::RT::BufferHandle>(cmd.vertexBuffer.value);
    rc.vertexOffset = cmd.vertexOffset;
    rc.vertexCount = cmd.vertexCount;
    rc.topology = toRenderXPrimitive(cmd.topology);
    rc.space = toRenderXSpace(cmd.space);
    rc.vertexFormat = toRenderXFormat(cmd.format);
    rc.pipelineIndex = cmd.pipelineIndex;
    rc.texture = static_cast<Render::RT::TextureHandle>(cmd.texture.value);
    rc.sortKey = cmd.sortKey;
    return rc;
}
```

---

## 4. 文件结构规划

### 4.1 RenderAbstraction 模块（新）

```
RenderAbstraction/
├── CMakeLists.txt
├── Include/
│   └── RenderAbstraction/
│       ├── IRenderTypes.h           # 业务级类型定义
│       ├── IRenderDevice.h          # 设备接口
│       ├── IRenderSurface.h         # 表面接口
│       ├── IRenderCommandList.h     # 命令列表接口
│       ├── IRenderScene.h           # 场景接口
│       ├── IRenderFactory.h         # 工厂接口
│       └── IRenderExport.h          # 导出宏
└── Src/
    └── (无源码，纯头文件库)
```

### 4.2 RenderXAdapter 实现（放在 RenderBridge 内部）

```
RenderBridge/
├── Include/
│   └── RenderBridge/
│       ├── RenderXAdapter.h         # 对外暴露的工厂入口（仅声明）
│       └── ...                     # 现有头文件逐步迁移
├── Src/
│   ├── RenderXAdapter/
│   │   ├── RenderXDeviceAdapter.cpp
│   │   ├── RenderXSurfaceAdapter.cpp
│   │   ├── RenderXSceneAdapter.cpp
│   │   ├── RenderXFactory.cpp
│   │   └── TypeConversions.h       # DrawInstruction ↔ DrawCommand 转换
│   └── ...                         # 现有源文件
└── CMakeLists.txt                  # 添加 RenderXAdapter 源文件
```

### 4.3 UI层改造后的依赖

```
UI2D CMakeLists.txt:
  LINK_LIBRARIES: ... RenderAbstraction ...
  INCLUDE_DIRS:  ... RenderAbstraction/Include ...
  # 移除: $<$<BOOL:${BUILD_RENDERX}>:RenderX>
  # 移除: $<$<BOOL:${BUILD_RENDERX}>:RenderBridge>

UI3D CMakeLists.txt:
  LINK_LIBRARIES: ... RenderAbstraction ...
  # 移除: RenderX, RenderBridge
```

---

## 5. 迁移路径（分阶段）

### 阶段 1：创建骨架（1-2天）

- 创建 `RenderAbstraction` 模块骨架（CMake + 头文件）
- 定义 `IRenderTypes.h` 业务类型
- 定义核心接口骨架
- RenderXAdapter 类的声明

### 阶段 2：实现适配层（3-5天）

- 实现 `RenderXDeviceAdapter`、`RenderXSurfaceAdapter`、`RenderXSceneAdapter`
- 实现 `TypeConversions.h` 的双向转换
- 实现 `RenderXFactory`
- 确保 `RenderBridge::PersistentGeometryStore` 内部重构为使用业务类型

### 阶段 3：UI层迁移 - UI2D（3-5天）

逐个文件替换：

| 文件 | 改造方式 |
|------|----------|
| `RenderWidget.h/cpp` | 替换 `RuntimeHandle` → `DeviceHandle`, `DrawCommand` → `DrawInstruction`, `rxSession*` → `IRenderScene` 接口 |
| `RenderSceneBuilder.h/cpp` | 使用 `IRenderScene::uploadGeometry()` 和 `CommandListHandle` |
| `BitmapQuadBuilder.h/cpp` | 使用 `IRenderDevice::createTexture()`, `DrawInstruction` |
| `WorldTextQuadBuilder.h/cpp` | 使用抽象字体接口 |
| `ViewRenderCoordinator.h/cpp` | 使用 `DrawInstruction` |

### 阶段 4：UI层迁移 - UI3D（5-7天）

同样逐个文件替换，涉及 Mesh3DBuilder、GizmoRenderer3D、RenderWidget3D 等。

### 阶段 5：清理与验证（2-3天）

- 从 RenderBridge 中移除对 `renderx.h` 的直接include（仅保留 Adapter 内）
- 更新 `CheckDependencies.cmake` 添加规则4
- 更新 CMakeLists.txt 依赖关系
- 确保所有 `#include "render/renderx.h"` 只出现在 `RenderBridge/Src/RenderXAdapter/` 目录下

---

## 6. 依赖方向校验更新

### 6.1 新增规则

```cmake
# CheckDependencies.cmake 新增规则4
set(_abstraction_targets "RenderAbstraction")
set(_ui_targets "UICommon|UI2D|UI3D")
set(_renderx_targets "RenderX|RenderBridge")

# 规则4：UI 不得直接依赖 RenderX
foreach(_t IN LISTS _ui_targets)
    if(TARGET ${_t})
        get_target_property(_libs ${_t} LINK_LIBRARIES)
        foreach(_dep IN LISTS _libs)
            if(_dep MATCHES "${_renderx_targets}")
                message(FATAL_ERROR "UI 目标 ${_t} 直接链接了 ${_dep}；应改为链接 RenderAbstraction")
            endif()
        endforeach()
    endif()
endforeach()
```

### 6.2 更新后的依赖图

```
Engine (ISceneGeometrySink)
  ↑ 实现
RenderAbstraction (接口)
  ↑ 包含
RenderBridge::RenderXAdapter (实现)
  ↑ 调用
RenderX DLL (renderx.h C ABI)

UI2D/UI3D → RenderAbstraction (仅接口)
Main → RenderAbstraction (仅接口)
```

---

## 7. 关键技术决策

### 7.1 为什么不用类型别名（using）

```cpp
// ❌ 不好：仍然是 RenderX 类型的别名，ABI 变更仍会影响 UI
namespace RA = Render::RT;
using DeviceHandle = Render::RT::RuntimeHandle;

// ✅ 正确：完全独立的类型定义
struct DeviceHandle { uint64_t value; };
```

### 7.2 性能考量

- `DrawInstruction` 和 `DrawCommand` 大小一致（或更紧凑），转换只是 memcpy
- Handle 用 `uint64_t` 包装，零额外开销（enum class : uint64_t 也是8字节）
- 接口调用通过 `IRenderScene` 的指针，无虚函数调用开销（每帧submit本身已经是虚函数）
- `beginFrame/endFrame` 保持现有语义

### 7.3 过渡期兼容

在迁移期间，RenderBridge 保持现有功能不变。新代码走 RenderAbstraction 接口，旧代码（RenderXAdapter内部）仍然调用 renderx.h。这确保了：

- 新功能使用新接口
- 旧功能继续工作
- 逐步替换，无停机风险

### 7.4 3D光照的特殊处理

3D 光照（`Lighting3DDesc`）目前由UI层直接构造并传给 `rxSessionSetLighting3D`。在抽象层：

```cpp
// IRenderCommandList.h
virtual void setLighting(const LightDesc& lighting, const Vec3F& viewPos) = 0;
```

`LightDesc` 是业务级结构，`RenderXSceneAdapter::setLighting()` 负责将其转换为 `Lighting3DDesc`。

### 7.5 几何仓的抽象

`PersistentGeometryStore` 是最深的耦合点。抽象方案：

```cpp
// IRenderScene.h
virtual BufferHandle uploadGeometry(const void* vertices, uint64_t sizeBytes, 
                                     bool persistent = true) = 0;
```

UI层不再感知 GeometryStoreHandle、GeometryBlock 等概念。上传几何后获得一个 `BufferHandle`，然后用它创建 `DrawInstruction`。

---

## 8. 预期收益

| 指标 | 改进前 | 改进后 |
|------|--------|--------|
| 直接依赖renderx.h的UI文件数 | 10 | 0 |
| UI代码中的Render::RT类型数 | 30+ | 0 |
| ABI变更影响范围 | 全部UI文件 | 仅RenderXAdapter |
| 替换渲染后端所需修改 | 数十个文件 | 仅Adapter实现 |
| 编译依赖链 | UI→RenderX | UI→RenderAbstraction |
| 单元测试可Mock性 | 差 | 好（Mock IRenderScene） |

---

## 9. 风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| 迁移期间工作量巨大 | 分阶段迁移，每个阶段可独立验证 |
| 转换层引入bug | 充分的单元测试 + 静态断言大小一致 |
| 性能下降 | 转换在 .cpp 中完成，零开销抽象 |
| 接口设计不合理 | 先实现再迭代，接口可扩展 |
| 团队学习成本 | 提供清晰的迁移指南和代码示例 |
