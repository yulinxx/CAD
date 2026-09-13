# CAD 激光加工软件框架分析报告

## 一、结论先行

当前工程的总体方向是合理的，但还属于“OpenGL 优先的架构原型”，尚未达到目标中的：

- 2D/3D 长期稳定切换；
- OpenGL/Vulkan/Metal 真正可切换；
- UI、交互、算法、数据、渲染低耦合；
- 百万级 2D 图元稳定渲染；
- 后期可替换算法策略。

最重要的判断是：

> 现有 2D 编辑与增量渲染基础较好，可以继续演进；3D 文档、3D UI 适配、渲染后端和 RenderX 抽象目前还没有真正闭环。

当前不建议直接继续堆叠算法、UI 和功能。应先解决以下结构性问题：

1. OpenGL 是唯一真实图形后端，Vulkan/Metal 目前只是接口和枚举。
2. 3D 同时存在两套文档模型和两套适配路径。
3. 2D 全量渲染和增量渲染存在两套几何离散化实现。
4. UI 层、Main 层仍然直接依赖具体引擎和 RenderX 类型。
5. 3D 没有视锥剔除，无法满足大型模型长期扩展。
6. 场景数据与 GPU 渲染数据之间还没有清晰的不可变快照层。
7. 部分接口是占位实现，却已经被业务代码调用。

---

## 二、当前实际架构

### 1. 2D 主链路

当前 2D 大致是：

```text
Qt UI / Tool / OperationBus
        │
        ▼
SceneDocument2D / SceneEditService
        │
        ▼
Engine2D::SceneManager
        │
        ├── EntityContainer
        ├── SelectionManager
        ├── GroupManager
        ├── Layer
        ├── SpatialIndex
        └── Dirty / Deleted Entity Set
        │
        ▼
SceneNotifier
        │
        ▼
SceneRefreshCoordinator
        │
        ├── 全量路径：
        │      SceneManager::gatherGeometry
        │              ▼
        │      RenderSceneBuilder
        │              ▼
        │      GeometryStore + DrawList
        │
        └── 增量路径：
               EntityToVertices
                       ▼
               RenderWidget::addRenderEntity
                       ▼
               RenderSceneBuilder::upsertEntity
        │
        ▼
RenderWidget : QOpenGLWidget
        │
        ▼
RenderX
        │
        ▼
OpenGL
```

相关代码：

- [SceneRenderContract.h](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/Scene/SceneRenderContract.h:16)
- [SceneManager.h](C:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Core/SceneManager.h:84)
- [SceneRefreshCoordinator.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/SceneRefreshCoordinator.cpp:676)
- [RenderSceneBuilder.h](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Include/Render/RenderSceneBuilder.h:55)
- [EntityToVertices.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:391)

2D 的分层方向基本正确，但实际存在两个问题：

- 全量路径由 `RenderSceneBuilder` 自己离散化；
- 增量路径由 `EntityToVertices::IncrementalVertexSink` 再离散化一次。

代码甚至明确要求两套离散化公式保持一致。这说明目前已经出现了“重复功能 + 依靠人工保证一致”的问题。

---

### 2. 3D 主链路

当前 3D 实际不是一条干净链路，而是：

```text
UiWorkbench
    │
    ├── SceneDocument3D
    │
    └── SceneDocument3DAdapter
             │
             ▼
       UiViewport3D
             │
             ▼
       IRenderer3D
             │
             ▼
       RenderWidget3DAdapter
             │
             ▼
       RenderWidget3D : QOpenGLWidget
             │
             ▼
       Mesh3DBuilder
             │
             ▼
       RenderX
             │
             ▼
       OpenGL
```

`UiWorkbench` 同时创建了：

- `SceneDocument3D`
- `SceneDocument3DAdapter`

但真正传给 3D viewport 的仍然是 `SceneDocument3DAdapter`：

- [UiWorkbench.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:133)
- [UiWorkbench.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:2166)
- [UiWorkbench.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:2253)

这不是简单的“代码重复”，而是两个可能成为真实数据入口的文档模型并存，后期一定会导致：

- 选择状态不同步；
- 删除和清空行为不同；
- 撤销/重做入口不同；
- 3D 树节点和引擎实体生命周期不一致；
- UI 使用错误的文档对象。

---

## 三、已有设计中比较合理的部分

### 1. RenderX 的职责边界方向正确

RenderX 的公共接口基本遵循：

```text
RenderX 只负责：
- GPU 资源
- 几何缓冲
- DrawList
- Pipeline
- Texture
- Frame 提交
- 后端资源管理

RenderX 不负责：
- 场景树
- 图元业务类型
- 选择
- 捕捉
- 单位
- 图层
- 文件读写
- 算法
```

这个边界是正确的，适合未来替换图形后端。

`GeometryStore + DrawList` 的组合也适合编辑型 CAD：

- 几何数据跨帧持久化；
- 只更新修改过的区域；
- DrawList 可以避免每帧重新组织全部命令；
- 图元 ID 可以通过 slot/userData 关联回业务层。

相关接口见：

- [renderx.h](C:/Users/xx/Documents/Cpp/CAD/Renderx/include/render/renderx.h:887)
- [renderx.h](C:/Users/xx/Documents/Cpp/CAD/Renderx/include/render/renderx.h:1152)

但它目前只是“接口边界设计正确”，还不能称为完整的跨后端实现。

---

### 2. Engine 层的几何发射器方向正确

`EntityGeometryEmitter` 将具体 `SyLine`、`SyCircle`、`SyArc` 等类型识别留在 Engine 内部，通过几何原语推给渲染层。

这样可以避免：

```text
UI 直接 dynamic_cast<SyCircle>
UI 直接 dynamic_cast<SyArc>
UI 直接访问具体实体内部数据
```

这是一个明显的改进方向：

- Engine 负责理解实体；
- RenderBridge 负责几何离散化；
- GPU Renderer 只接受顶点和绘制命令。

但是当前离散化仍然分散在多个位置，需要进一步收口。

---

### 3. 2D 场景管理器的职责相对完整

`SceneManager` 目前包含：

- 实体生命周期；
- 选择；
- 图层；
- 群组；
- 空间索引；
- dirty/deleted 集合；
- 场景 generation；
- 场景通知。

这些职责对 2D CAD 编辑是必要的，当前没有明显的严重过度设计。

问题不在于这些功能太多，而在于：

- UI 仍然可以穿透到具体 `SceneManager`；
- 变更通知粒度不够结构化；
- 渲染层读取的是可变对象，而不是版本化快照。

---

### 4. 刷新级别和批量上传思路正确

`SceneRefreshCoordinator` 将刷新分成：

- Repaint；
- LightUpdate；
- Selection；
- FullRefresh。

并且使用批量上传，避免每个图元都单独执行：

```text
makeCurrent
上传
update
doneCurrent
```

这个性能意识是正确的。

但当前协调器承担了太多职责：它同时处理：

- 场景变更；
- 脏图元收集；
- 类型判断；
- 文字同步；
- 图片同步；
- GPU 上传；
- 渲染组件更新；
- 缓存清理。

后期应将其拆成：

```text
SceneChangeTracker
        │
        ▼
RenderBridge
        │
        ▼
RenderUploadQueue
        │
        ▼
ViewportRenderer
```

---

## 四、主要问题分析

## 4.1 后端切换目前没有真正实现

当前 RenderX 枚举中有：

- OpenGL；
- Vulkan；
- Metal；
- Null；
- Auto。

但实际 `rhiFactory.cpp` 明确显示：

- OpenGL 可用；
- Null 可用；
- Metal 返回不可用；
- Vulkan 返回不可用。

相关代码：

- [rhiFactory.cpp](C:/Users/xx/Documents/Cpp/CAD/Renderx/src/rhi/rhiFactory.cpp:51)
- [rhiFactory.cpp](C:/Users/xx/Documents/Cpp/CAD/Renderx/src/rhi/rhiFactory.cpp:87)

目前真实状态是：

```text
OpenGL       已实现
Null         已实现
Vulkan       未实现
Metal        未实现
```

此外，2D `RenderWidget` 固定使用：

```cpp
rd.backend = RT::Backend::OpenGL;
```

并且继承自：

```cpp
QOpenGLWidget
```

见：

- [RenderWidget.cpp](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderWidget.cpp:82)
- [RenderWidget.cpp](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderWidget.cpp:168)

3D 也同样直接继承 `QOpenGLWidget`：

- [RenderWidget3D.h](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Render3D/RenderWidget3D.h:105)

因此现在不能通过改一个枚举就实现：

```text
运行时 OpenGL ⇄ Vulkan ⇄ Metal
```

原因是 Qt Surface、上下文、交换链、Present、Framebuffer 的生命周期都已经被 OpenGL 类型绑定。

### 修改意见

应该引入真正的两层接口：

```text
Qt Viewport Host
        │
        ▼
Backend-neutral RenderSurface
        │
        ▼
RenderX Runtime / Session
        │
        ├── OpenGL Device
        ├── Vulkan Device
        └── Metal Device
```

不要让 `RenderWidget` 同时承担：

- Qt Widget；
- OpenGL Context；
- RenderX Runtime；
- 场景渲染；
- 文字；
- 图片；
- 交互。

可以拆成：

```cpp
class IViewportSurface
{
public:
    virtual SurfaceHandle createSurface(...) = 0;
    virtual void resize(...) = 0;
    virtual void present() = 0;
};

class IViewportRenderer
{
public:
    virtual void submit(RenderFrameView frame) = 0;
};

class RenderSessionHost
{
    Runtime;
    Surface;
    Session;
};
```

Qt 层只负责把窗口或 Native Surface 交给后端适配器。

另外，建议尽早决定采用哪条路线：

1. 继续维护自研 RenderX RHI，并真正实现 Vulkan/Metal；
2. 基于 Qt RHI/QRhi 做统一后端。

不建议同时维护两套功能高度重叠的渲染抽象。

---

## 4.2 RenderX 的“C API”实际是 C linkage 的 C++ API

`renderx.h` 使用了 `extern "C"`，但公共头仍然使用：

- C++ namespace；
- `enum class`；
- C++ 类型；
- C++ inline/constexpr；
- C++ 风格句柄封装。

所以它是：

> C linkage + C++ header + POD layout

而不是可以被 C 编译器直接包含的纯 C ABI。

如果 RenderX 只在同一套 MSVC/Qt 工程内部使用，这没有立即问题。

如果未来计划：

- 第三方插件；
- 不同编译器；
- 不同 CRT；
- 独立 SDK；
- 跨语言绑定；

则需要改成真正的 C ABI：

```c
typedef uint64_t rx_runtime_handle;
typedef uint32_t rx_backend;
typedef struct rx_runtime_desc {...} rx_runtime_desc;
```

否则应明确把它称为“C-linkage C++ ABI”，不要把它宣传为完整 C API。

---

## 4.3 2D 存在两套几何离散化路径

当前有两套逻辑：

### 全量路径

```text
SceneManager::gatherGeometry
    → RenderSceneBuilder
    → emitPolyline / emitCircle / emitArc / emitEllipse
```

### 增量路径

```text
entityToVertices
    → IncrementalVertexSink
    → emitPolyline / emitCircle / emitArc / emitEllipse
```

代码明确要求两者公式保持一致：

- [EntityToVertices.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:6)
- [EntityToVertices.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:140)

这属于典型的重复功能，后期容易出现：

- 全量刷新和增量刷新显示不一致；
- 闭合曲线处理不同；
- 颜色或精度不同；
- 细分参数不同；
- bug 修复需要改两个地方。

### 建议

把离散化算法独立为一个纯算法模块：

```text
RenderGeometry/
    Tessellator2D
    Tessellator3D
    CurveFlattening
    StrokeBuilder
    FillTessellator
```

两条路径共同使用：

```cpp
Tessellator2D::tessellate(entityGeometry, style, output);
```

不要让 `RenderSceneBuilder` 和 `EntityToVertices` 各自实现一遍。

---

## 4.4 EntityToVertices 的缓存存在并发安全问题

缓存定义为静态全局：

```cpp
static std::unordered_map<uint64_t, CachedVertexData> s_vertexCache;
static std::shared_mutex s_cacheMutex;
```

读取时使用了共享锁，但写入没有加锁：

```cpp
auto& entry = s_vertexCache[entityId];
entry.geometryHash = geomHash;
```

删除和清空也没有加锁：

```cpp
s_vertexCache.erase(entityId);
s_vertexCache.clear();
```

见：

- [EntityToVertices.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:46)
- [EntityToVertices.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:121)
- [EntityToVertices.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:363)

这在并行刷新路径下属于实际的数据竞争风险。

此外，静态缓存还存在生命周期问题：

- 缓存按 EntityId 全局共享；
- 不区分文档；
- 不区分 SceneManager；
- 不区分实体 generation；
- 文档关闭后必须依赖外部主动清理；
- 不同文档复用 ID 时存在命中旧数据的可能。

### 建议

缓存应该归属于：

```text
DocumentRenderBridge
    或
SceneRenderCache
```

并使用：

```text
DocumentId + EntityId + GeometryRevision
```

作为缓存键。

最少也要：

- 写入加独占锁；
- erase 加独占锁；
- clear 加独占锁；
- 使用场景 generation；
- 不使用全局静态对象。

---

## 4.5 3D 当前没有视锥剔除

`Mesh3DBuilder` 明确关闭了剔除：

```cpp
listDesc.enableCulling = 0;
```

原因是 RenderX 的 AABB 接口目前是 2D：

```text
(minX, minY, maxX, maxY)
```

见：

- [Mesh3DBuilder.cpp](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/Mesh3DBuilder.cpp:101)
- [renderx.h](C:/Users/xx/Documents/Cpp/CAD/Renderx/include/render/renderx.h:1167)

这在小模型上可以接受，但对大型 3D 场景不行：

```text
相机移动
    → 仍然提交所有网格
    → GPU 处理全部实体
    → 可见区域越小，浪费越大
```

### 建议

不要把 2D AABB 扩展成简单的 3D 四元数组，应新增明确的 3D 契约：

```cpp
struct Aabb3d
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};
```

再引入：

```cpp
struct Frustum
{
    Plane planes[6];
};
```

推荐实现：

```text
RenderX DrawList2D
    - 2D AABB
    - 2D view bounds

RenderX DrawList3D
    - 3D AABB
    - Frustum planes
    - depth sorting
    - optional hierarchical culling
```

不要强行让同一个 2D DrawList 接口同时承担 3D 剔除。

---

## 4.6 3D 接口中存在大量“假抽象”

`IRenderer3D` 的接口包含：

- `QPainter&`；
- `QString`；
- `SceneDocument3DAdapter*`；
- `std::function`；
- `isOpenGL()`；
- `setOrbitMode()`；
- `setMeasureMode()`。

见：

- [IRenderer3D.h](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Render3D/IRenderer3D.h:18)

实际实现中：

- `render(QPainter&, ...)` 不使用 `QPainter`；
- `setOrbitMode()` 是空实现；
- `isOrbitMode()` 恒返回 `true`；
- `isOpenGL()` 恒返回 `true`；
- 部分功能只是包装器。

见：

- [RenderWidget3DAdapter.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/RenderWidget3DAdapter.cpp:221)
- [RenderWidget3DAdapter.cpp](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/RenderWidget3DAdapter.cpp:261)

这类接口会造成调用方误以为功能已经存在。

### 建议

接口只表达真实能力：

```cpp
struct RendererCapabilities
{
    bool supportsToolpathOverlay;
    bool supportsMeshPicking;
    bool supportsOffscreenCapture;
    bool supportsWireframe;
};
```

所有不支持的功能必须：

- 返回明确错误；
- 或由能力查询隐藏；
- 或从接口中删除。

不要保留“接口存在但什么都不做”的状态。

---

## 4.7 3D 目前有未实现但已暴露的功能

以下功能当前明确是占位：

- 刀路覆盖层；
- 网格表面拾取；
- 离屏截图；
- 部分 3D 交互模式。

代码已经明确说明：

- [RenderWidget3D.cpp](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.cpp:529)
- [RenderWidget3D.cpp](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.cpp:567)
- [RenderWidget3D.cpp](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.cpp:1901)

这本身并不是问题，问题是这些 API 已经进入业务功能路径。

对于激光加工软件，刀路预览、拾取和截图通常不是边缘功能，应尽快决定：

- 真正实现；
- 或从当前版本 API 删除；
- 或使用能力状态明确标记为不可用。

---

## 五、重复功能、冗余和过度设计

| 类别 | 当前问题 | 判断 |
|---|---|---|
| 2D 几何离散化 | `RenderSceneBuilder` 和 `EntityToVertices` 各自离散化 | 重复，应合并 |
| 3D 文档 | `SceneDocument3D` 与 `SceneDocument3DAdapter` 并存 | 严重冗余，应只保留一套 |
| 3D 渲染包装 | `UiViewport3D → IRenderer3D → RenderWidget3DAdapter → RenderWidget3D` | 层级过多，且部分接口是假实现 |
| 2D/3D RenderX 生命周期 | Runtime、Surface、Session 创建、销毁、初始化逻辑重复 | 应提取公共 `RenderSessionHost` |
| Operation 系统 | 2D/3D 各有 Bus、Registry、Catalog、Routing | 应共享命令内核，保留维度扩展 |
| Selection 状态 | Engine、Document、UI SelectionSet、RenderWidget 多处持有 | 存在多真源风险 |
| 3D Stubs | Main 中存在多套 `IRenderer3D` Stub | 应统一测试桩 |
| SceneRenderContract | 一个 Sink 同时包含 2D、3D、文字、图片、三角网格 | 抽象过宽 |
| ISceneContext | 只是接口聚合，目前没有实际实现者 | 目前属于未落地抽象 |
| RHI 接口 | 资源、纹理、管线、计算、间接绘制等一次性铺得很广 | 不是错误，但应停止继续扩张，先实现第二后端 |
| C++ 接口 ABI | `ISceneManager` 等接口仍使用 STL 或默认实现 | 不适合宣传为稳定 DLL ABI |

`ISceneManager` 自己的注释也承认 `std::vector`、`std::function` 存在跨 DLL 风险：

- [ISceneManager.h](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/ISceneManager.h:21)

`ISceneContext` 当前只是接口聚合，实际 `SceneManager` 和 `SceneManager3D` 仍然直接继承：

- [ISceneContext.h](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/Scene/ISceneContext.h:30)
- [SceneManager.h](C:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Core/SceneManager.h:84)

因此它现在更像“预留设计”，不是正在工作的核心抽象。

---

## 六、模块依赖边界问题

### 1. UICommon 并不真正 Common

`UI/Common/CMakeLists.txt` 对外依赖：

- Engine2D；
- EnginePersistence；
- Log；
- SQLiteCpp；
- 间接 RenderX。

见：

- [UI/Common/CMakeLists.txt](C:/Users/xx/Documents/Cpp/CAD/UI/Common/CMakeLists.txt:120)
- [UI/Common/CMakeLists.txt](C:/Users/xx/Documents/Cpp/CAD/UI/Common/CMakeLists.txt:136)

这会导致：

```text
UICommon
    └── Engine2D
```

因此 UICommon 不是纯 UI 公共层，而是“应用公共层”。

建议拆成：

```text
UIFoundation
    - Qt 基础
    - CommandId
    - ActionModel
    - Viewport 接口
    - UI 状态

RenderContract
    - RenderFrame
    - RenderStyle
    - RenderEntityId
    - Overlay 数据

UI2D
    └── Engine2D

UI3D
    └── Engine3D
```

---

### 2. Engine3D 私有依赖 Engine2D

`Engine3D/CMakeLists.txt` 中私有链接了 `Engine2D`：

- [Engine/3D/CMakeLists.txt](C:/Users/xx/Documents/Cpp/CAD/Engine/3D/CMakeLists.txt:68)
- [Engine/3D/CMakeLists.txt](C:/Users/xx/Documents/Cpp/CAD/Engine/3D/CMakeLists.txt:76)

如果 3D 引擎只是暂时复用了公共类型，可以接受。

但如果目标是 2D/3D 长期独立策略，这个依赖方向不理想：

```text
Engine3D → Engine2D
```

应将真正共享的内容移动到：

```text
EngineCommon
GeometryCommon
MathCommon
```

而不是让 3D 依赖 2D。

---

### 3. UI2D/UI3D 直接依赖 RenderX

当前 UI2D/UI3D 都直接把 RenderX 放到公共链接边界中：

- [UI/2D/CMakeLists.txt](C:/Users/xx/Documents/Cpp/CAD/UI/2D/CMakeLists.txt:173)
- [UI/3D/CMakeLists.txt](C:/Users/xx/Documents/Cpp/CAD/UI/3D/CMakeLists.txt:152)

这样 UI 组件天然被绑定到 RenderX，后期如果换 QRhi、软件渲染或其它渲染实现，会影响大量 UI 代码。

建议改成：

```text
UI2D/UI3D
    → RenderBridge
        → RenderX
```

UI 只依赖 `IViewportRenderer` 或 `RenderSceneHandle`，不直接依赖 RenderX 的 Runtime/Surface/Session。

---

## 七、2D/3D 切换需要先明确数据模型

当前工程存在独立的：

- `Engine2D::SceneManager`
- `Engine3D::SceneManager3D`

这在以下场景下没有问题：

```text
2D CAD 文档
3D 模型文档
两者是完全不同的文档类型
```

但如果目标是同一个文档长期进行：

```text
2D 显示 ⇄ 3D 显示
```

就不能让 2D 和 3D 各自维护一份可变真源。

需要选择一种模型：

### 模型 A：单一设计数据源

```text
DocumentModel
    ├── 2D View Adapter
    └── 3D View Adapter
```

适合 2D 图元本身具有 3D 属性的情况。

### 模型 B：设计场景 + 3D 派生场景

```text
DesignDocument
    ├── 2D Design View
    └── 3D Preview / Relief / Stock / Toolpath View
```

适合激光加工软件中：

- 2D 是设计图；
- 3D 是浮雕、材料、加工深度、刀路或仿真预览。

当前代码更接近模型 B，但没有明确建模，导致 3D 文档和 3D 适配器同时存在。

建议将两者明确区分：

```text
DesignDocument
ManufacturingPreviewDocument
```

3D 预览应是可重建的派生结果，而不是第二个偷偷修改设计数据的场景真源。

---

## 八、建议的目标架构

```text
┌─────────────────────────────────────────────┐
│ Qt UI / Workbench / Dock / Toolbar          │
│ 只负责界面、输入、命令展示、布局              │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ Application Layer                            │
│ Command / Undo / Selection / DocumentSession │
│ ChangeSet / Workbench State                  │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ Domain / Engine                              │
│ Document / Entity / Layer / Group / Geometry │
│ SceneManager2D / SceneManager3D              │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ Algorithm Layer                              │
│ Strategy / Job / Preview Result / Progress   │
│ 不依赖 Qt、RenderX、具体 Viewport             │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ RenderBridge / RenderData                   │
│ Snapshot / Tessellation / Chunk / Bounds    │
│ Text / Image / Toolpath / Overlay            │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ Backend-neutral Renderer                    │
│ RenderFrame / RenderSurface / Picking       │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ RenderX Runtime                             │
│ OpenGL / Vulkan / Metal / Null              │
└─────────────────────────────────────────────┘
```

核心原则：

- Engine 不知道 Qt；
- Algorithm 不知道 UI；
- RenderX 不知道 Engine；
- UI 不直接操作 GPU 资源；
- SceneManager 不直接暴露给 Renderer；
- Renderer 不直接读取可变实体；
- 视口渲染消费版本化 Render Snapshot。

---

## 九、具体修改优先级

### P0：必须优先处理

1. 合并 `SceneDocument3D` 和 `SceneDocument3DAdapter`，只保留一套正式文档入口。
2. 处理 `SceneDocument3D::removeEntity()` 的空实现问题。
3. 修复 `EntityToVertices` 缓存写入、删除、清空时的锁问题。
4. 将 2D 离散化算法合并成一个公共 `Tessellator2D`。
5. 移除或重写 `RenderWidget3DAdapter` 中的空方法和恒真能力。
6. 将 UICommon 对 Engine2D、RenderX 的公共依赖降级或移除。
7. 让 Engine3D 不再依赖 Engine2D。
8. 明确 2D/3D 是同一文档的两个视图，还是两个不同文档。

### P1：结构性重构

1. 新建轻量 `RenderBridge` 模块。 — ✅ 已完成 (`RenderBridge` 模块：`RenderSessionHost` 会话生命周期 + `HostCallbacks` 宿主回调，2D/3D 视口共用)
2. 引入：

```text
SceneChangeSet
DocumentRevision
RenderEntityProxy
RenderSnapshot
```

— ✅ 已完成 `SceneChangeSet`(追加式修订日志) 与 `RenderSnapshot`(只读快照)；`DocumentRevision` 并入变更流修订号，`RenderEntityProxy` 由 `RenderEntitySnapshot` 承担。

3. 将增量刷新从“按实体指针读取”改为“按变更快照读取”。 — ✅ 已完成（`SceneRefreshCoordinator` 的过滤循环、并行离散化、串行分支三处改读快照）
4. 抽取公共：

```text
RenderSessionHost
PersistentGeometryStore
RenderUploadQueue
OverlayScene
```

— `RenderSessionHost` ✅ 已完成；`PersistentGeometryStore` ✅ 已完成（`RenderBridge::PersistentGeometryStore`：几何仓 + 绘制列表 + 槽位台账，2D/3D 两个 builder 共用；2D 全量刷新改为按段位哈希差量更新，撤销/重做不再全场景重传）；`RenderUploadQueue` 部分完成（`RenderBridge::RenderUploadQueue` 已落地命令模型、线程边界与消费契约，**尚未接入视口** —— 队列的收益要等渲染线程独立才兑现，现在接入只是把同步上传拆成「入队 + 同线程立刻 drain」，因此留到 P2 与多后端/渲染线程一起收口）；`OverlayScene` ✅ 已完成（`RenderBridge::OverlayScene`：覆盖层按 `OverlayLayerId` 拆成 9 个独立层，提交端（瞬态环分配 + DrawCommand）从 UI2D 移入 RenderBridge，见专题 24）。

5. 统一 2D/3D 的命令内核：

```text
CommandId
CommandDescriptor
CommandContext
CommandState
OperationRegistry
```

2D/3D 只注册不同的命令扩展。

6. 统一 Selection Model，避免 Engine、Document、UI、RenderWidget 各维护一份选择状态。

### P2：真正实现多后端

建议先做一个最小垂直切片：

```text
一组 2D 线段
一个 3D 三角网格
一个文字对象
一个选择高亮
```

要求同一份 RenderFrame 能够分别使用：

- OpenGL；
- Vulkan 或 Metal；
- Null。

并验证：

- 同样的顶点和命令；
- 同样的图层顺序；
- 同样的选择结果；
- 同样的截图输出；
- 后端不可用时有明确错误。

不要先把 Vulkan/Metal 的全部 RHI 能力都设计出来，再寻找实际用例。

### P3：百万级图元优化

当前“每个实体一个 GeometryStore block + 一个 DrawList slot”的方式适合编辑性，但不一定适合百万级图元。

建议采用：

```text
Scene
 └── Layer
      └── Spatial Tile / Chunk
           └── Material Batch
                └── GPU Buffer Range
```

编辑时：

- 只重建受影响的 Chunk；
- 不重新上传整个场景；
- EntityId → Chunk/Range 保留反查；
- 视口只提交可见 Chunk；
- 静态几何尽量合批；
- 动态编辑实体可单独放入小块。

还需要补充：

- 2D 空间剔除；
- 3D 视锥剔除；
- GeometryStore 碎片整理；
- 大块分配策略；
- GPU indirect draw；
- 内存和 draw call 统计。

---

## 十、算法层的建议

算法层建议统一为：

```cpp
class IAlgorithmStrategy
{
public:
    virtual AlgorithmResult run(const AlgorithmInput& input,
                                CancellationToken token,
                                ProgressSink progress) = 0;
};
```

其中：

```text
AlgorithmInput
    - 不可变 DocumentSnapshot
    - 选中对象
    - 参数
    - 材料/加工设置

AlgorithmResult
    - 几何修改
    - 刀路
    - 预览网格
    - 警告和错误
```

执行流程：

```text
UI 命令
    → ApplicationService
    → Strategy
    → AlgorithmResult
    → Command / Undo
    → SceneChangeSet
    → RenderBridge
```

算法不应直接：

- 调用 `RenderWidget`；
- 调用 RenderX；
- 持有 Qt 控件；
- 修改 UI 状态；
- 直接修改 SceneManager 内部对象。

这样后期可以替换：

```text
NestingStrategy
NestingStrategyFast
NestingStrategyQuality
ReliefStrategyCPU
ReliefStrategyGPU
```

而不影响 UI 和渲染层。

---

## 十一、最终判断

### 当前框架可以保留的部分

- Engine 与 RenderX 的总体边界；
- GeometryStore + DrawList；
- 2D SceneManager 的实体、图层、空间索引、脏标记；
- Engine 侧几何发射器；
- 2D 增量刷新思想；
- Workbench 中的依赖注入和服务组合方向。

### 当前必须重构的部分

- 3D 双文档模型；
- 3D 多层包装器；
- 2D 两套离散化路径；
- RenderWidget 对 QOpenGLWidget 的强绑定；
- RenderX 后端实现；
- 3D 剔除；
- 全局渲染缓存；
- UICommon 和 Engine3D 的依赖方向；
- Selection、Command、SceneDocument 的多真源问题。

### 是否存在过度设计

存在，但主要不是“类太多”，而是：

> 同一职责存在多套并行抽象，其中一些抽象还没有真实实现。

最明显的是：

- 两套 3D 文档；
- 两套 2D 几何离散化；
- 两套 3D 渲染入口；
- 两套 2D/3D Operation 系统；
- 多处 Selection 状态；
- 过早铺开的 RHI 能力；
- 未实现功能的接口占位。

因此正确的方向不是继续增加更多接口，而是先减少平行路径，形成一条清晰主链：

```text
Domain
  → ChangeSet
  → RenderBridge
  → RenderFrame
  → Backend Renderer
```

本轮只进行了代码和架构分析，没有修改工作区文件。








# 详细重构方案与代码级修改建议

下面按上一份报告中的每个问题逐项展开。建议整体采用“先收口、再抽象、后扩展”的顺序，避免在现有多条并行链路上继续增加接口。

---

# 1. OpenGL/Vulkan/Metal 不能真正切换

## 当前问题

当前真实后端只有：

```text
OpenGL：可用
Null：可用
Vulkan：未实现
Metal：未实现
```

`rhiFactory.cpp` 中 Metal/Vulkan 直接返回失败：

[ rhiFactory.cpp ](C:/Users/xx/Documents/Cpp/CAD/Renderx/src/rhi/rhiFactory.cpp:51)

2D 和 3D 视口又直接继承 `QOpenGLWidget`，并固定创建 OpenGL Runtime：

[ RenderWidget.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderWidget.cpp:82)

[ RenderWidget.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderWidget.cpp:168)

## 重构目标

将 Qt Widget、窗口 Surface、渲染器、图形后端分离：

```text
Qt Viewport
    │
    ▼
IViewportSurface
    │
    ▼
ViewportRenderer
    │
    ▼
RenderX Runtime / Session
    │
    ├── OpenGLDevice
    ├── VulkanDevice
    └── MetalDevice
```

Qt 层不能再直接持有 OpenGL Runtime。

## 建议新增接口

新建：

```text
RenderBridge/
    Include/RenderBridge/GraphicsBackend.h
    Include/RenderBridge/NativeSurface.h
    Include/RenderBridge/IViewportRenderer.h
    Include/RenderBridge/RenderSessionHost.h
```

### 后端枚举

```cpp
#pragma once

#include <cstdint>

namespace RenderBridge
{
    enum class GraphicsBackend : uint8_t
    {
        Auto = 0,
        OpenGL,
        Vulkan,
        Metal,
        Null
    };

    struct BackendCapabilities
    {
        GraphicsBackend backend = GraphicsBackend::Null;

        uint8_t available = 0;
        uint8_t supportsCompute = 0;
        uint8_t supportsOffscreen = 0;
        uint8_t supportsPicking = 0;
        uint8_t supports3DCulling = 0;

        uint32_t maxTextureSize = 0;
        uint32_t maxUniformBufferSize = 0;
    };
}
```

### Native Surface 描述

不要用一个 `void* windowHandle` 含义模糊地传所有平台对象。

```cpp
#pragma once

#include <cstdint>

namespace RenderBridge
{
    enum class NativeSurfaceKind : uint8_t
    {
        None = 0,
        ForeignOpenGLContext,
        Win32Window,
        VulkanSurface,
        MetalLayer
    };

    struct NativeSurfaceDesc
    {
        NativeSurfaceKind kind = NativeSurfaceKind::None;

        void* handleA = nullptr;
        void* handleB = nullptr;

        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t highDpi = 0;
    };
}
```

以后：

```text
OpenGL：
ForeignOpenGLContext + QOpenGLContext

Vulkan：
Win32Window / XcbWindow / WaylandSurface

Metal：
CAMetalLayer
```

它们不能全部通过 `ForeignGlContext` 伪装。

### 后端无关的视口渲染器

```cpp
class IViewportRenderer
{
public:
    virtual ~IViewportRenderer() = default;

    virtual bool initialize(
        RenderBridge::GraphicsBackend backend,
        const RenderBridge::NativeSurfaceDesc& surface) = 0;

    virtual void shutdown() = 0;

    virtual bool resize(uint32_t width, uint32_t height) = 0;

    virtual void render(const RenderFrame& frame) = 0;

    virtual RenderBridge::BackendCapabilities capabilities() const = 0;
};
```

`RenderFrame` 不应包含 Qt 类型，也不应包含 `QPainter`。

## RenderSessionHost

将 2D 和 3D 中重复的 Runtime/Surface/Session 生命周期抽出来：

```cpp
class RenderSessionHost
{
public:
    bool create(
        Render::RT::Backend backend,
        const Render::RT::RuntimeDesc& runtimeDesc,
        const Render::RT::SurfaceDesc& surfaceDesc);

    void destroy();

    bool resize(uint32_t width, uint32_t height);

    bool beginFrame();
    bool endFrame();

    Render::RT::RuntimeHandle runtime() const;
    Render::RT::SessionHandle session() const;
    Render::RT::SurfaceHandle surface() const;

private:
    Render::RT::RuntimeHandle m_runtime{};
    Render::RT::SurfaceHandle m_surface{};
    Render::RT::SessionHandle m_session{};
};
```

之后：

```cpp
class RenderWidget2D : public QWidget
{
    std::unique_ptr<RenderSessionHost> m_session;
};

class RenderWidget3D : public QWidget
{
    std::unique_ptr<RenderSessionHost> m_session;
};
```

而不是两个 Widget 各自重复创建和销毁 RenderX 对象。

## 实施顺序

1. 先保留现有 OpenGL。
2. 将 `RenderWidget` 中 Runtime 生命周期抽到 `RenderSessionHost`。
3. 将 `QOpenGLWidget` 依赖限制在 OpenGL Adapter。
4. 实现一个 Vulkan 或 Metal 最小后端。
5. 用同一份 `RenderFrame` 验证两个后端。
6. 最后再开放 Auto 选择。

不建议同时开始 Vulkan 和 Metal。应根据主要平台选择一个第二后端：

```text
Windows/Linux 优先 Vulkan
macOS 优先 Metal
```

---

# 2. 3D 存在两套文档模型

## 当前问题

当前同时存在：

```text
SceneDocument3D
SceneDocument3DAdapter
```

`UiWorkbench` 同时创建两个对象：

[ UiWorkbench.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:2166)

但真正传给 3D viewport 的仍是：

```cpp
viewport->setSceneDocument(
    m_serviceOwner->sceneDocumentAdapter.get());
```

[ UiWorkbench.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:2253)

此外，`SceneDocument3D::removeEntity()` 当前只是空实现。

## 重构目标

只保留一个正式文档对象：

```text
SceneDocument3D
    ├── SceneManager3D
    ├── SceneEditService3D
    ├── SelectionModel
    └── SceneTreeModel
```

`SceneNode` 不应成为第二个文档，只能是 UI 树模型。

## 建议结构

```cpp
class SceneDocument3D final : public UI::ISceneDocument
{
public:
    Eg::SceneManager3D& scene();
    SceneEditService3D& editService();
    SelectionModel& selection();
    SceneTreeModel& treeModel();

    bool removeEntity(Eg::EntityId id);
    void clear();

private:
    std::unique_ptr<Eg::SceneManager3D> m_scene;
    std::unique_ptr<SceneEditService3D> m_editService;
    std::unique_ptr<SelectionModel> m_selection;
    std::unique_ptr<SceneTreeModel> m_treeModel;
};
```

`SceneDocument3DAdapter` 的职责拆分为：

```text
SceneDocument3D       文档和编辑入口
SceneTreeModel        UI 树结构
SelectionModel        选择状态
SceneEditService3D    命令、撤销、实际修改
```

## SceneTreeModel

```cpp
class SceneTreeModel
{
public:
    void rebuild(const Eg::SceneManager3D& scene);

    void onEntityAdded(Eg::EntityId id);
    void onEntityRemoved(Eg::EntityId id);
    void onEntityRenamed(Eg::EntityId id);

    std::vector<Eg::EntityId> rootEntities() const;
};
```

`SceneNode::selected()` 不应直接读取 Engine 内部选择状态：

```cpp
bool SceneNode::selected() const
{
    return m_selectionModel->contains(m_engineEntityId);
}
```

而不是：

```cpp
return m_engineScene->findMeshById(id)->selected();
```

## 3D 删除操作

当前 `SceneDocument3D::removeEntity()` 不能继续空实现。

推荐：

```cpp
bool SceneDocument3D::removeEntity(Eg::EntityId id)
{
    if (!m_editService)
    {
        return false;
    }

    return m_editService->deleteEntity(id);
}
```

并在 `SceneEditService3D` 内部完成：

```text
检查实体
创建 Undo Command
SceneManager3D::removeEntity
发布 SceneChangeSet
刷新 SelectionModel
刷新 SceneTreeModel
```

`clear()` 也必须经过编辑服务：

```cpp
void SceneDocument3D::clear()
{
    if (m_editService)
    {
        m_editService->clearScene();
    }
}
```

不能直接绕过 Undo 调用：

```cpp
m_sceneManager->clearScene();
```

## 如果 3D 是派生预览

如果 3D 只是浮雕、材料、刀路或加工模拟结果，建议明确命名：

```text
DesignDocument
ManufacturingPreviewDocument
```

3D 预览由 2D 设计数据和算法结果生成，不能与设计文档并列成为第二个可编辑真源。

---

# 3. 2D 全量和增量有两套离散化代码

## 当前问题

全量路径：

```text
SceneManager::gatherGeometry
    → RenderSceneBuilder
```

增量路径：

```text
EntityToVertices
    → IncrementalVertexSink
```

两个地方都处理：

- Polyline；
- Circle；
- Arc；
- Ellipse；
- Triangle；
- LineLoop。

代码已经明确要求两者公式保持一致：

[ EntityToVertices.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:140)

## 重构目标

只保留一套：

```text
Engine Geometry Emitter
        ↓
Tessellator2D
        ↓
RenderGeometry
        ↓
RenderSceneBuilder
```

## 建议新增 Tessellator2D

```cpp
namespace RenderGeometry
{
    struct TessellationOptions
    {
        double tolerance = 0.01;
        uint32_t maxSegments = 4096;
        double cameraScale = 1.0;
    };

    struct VertexP3C3
    {
        float px, py, pz;
        float cr, cg, cb;
    };

    struct TessellatedPiece
    {
        Render::PrimitiveType topology{};
        std::vector<VertexP3C3> vertices;
        Render::BBox2d bounds;
    };

    class Tessellator2D
    {
    public:
        explicit Tessellator2D(TessellationOptions options);

        void polyline(
            const Ut::Vec2d* points,
            size_t count,
            bool closed,
            const Ut::Color& color,
            TessellatedPiece& output);

        void circle(
            const Ut::Vec2d& center,
            double radius,
            const Ut::Color& color,
            TessellatedPiece& output);

        void arc(
            const Ut::Vec2d& center,
            double radius,
            double startAngle,
            double endAngle,
            const Ut::Color& color,
            TessellatedPiece& output);

        void ellipse(
            const Ut::Vec2d& center,
            double radiusX,
            double radiusY,
            double rotation,
            double startAngle,
            double endAngle,
            bool fullEllipse,
            const Ut::Color& color,
            TessellatedPiece& output);
    };
}
```

## 让全量和增量共用同一个 Sink

```cpp
class TessellatingSink final : public Eg::ISceneGeometrySink
{
public:
    TessellatingSink(
        RenderGeometry::Tessellator2D& tessellator,
        RenderGeometry::IGeometryOutput& output);

    void setCurrentEntityId(uint64_t id) override;

    void emitPolyline(...) override;
    void emitCircle(...) override;
    void emitArc(...) override;
    void emitEllipse(...) override;

private:
    RenderGeometry::Tessellator2D& m_tessellator;
    RenderGeometry::IGeometryOutput& m_output;
    uint64_t m_currentEntityId = 0;
};
```

全量：

```cpp
void RenderSceneBuilder::rebuild(const Eg::ISceneDataSource& source)
{
    clear();

    Tessellator2D tessellator(currentOptions());
    RenderSceneOutput output(*this, m_currentEntityId);

    TessellatingSink sink(tessellator, output);
    source.gatherGeometry(sink);
}
```

增量：

```cpp
bool entityToVertices(
    const Eg::SyEntity* entity,
    std::vector<Render::VertexP3C3>& output,
    Render::PrimitiveType& type,
    const TessellationOptions& options)
{
    Tessellator2D tessellator(options);
    EntityVertexOutput vertexOutput(output, type);

    TessellatingSink sink(tessellator, vertexOutput);

    return Eg::emitEntityGeometry(*entity, sink);
}
```

这样后续修改圆弧细分算法，只改一个地方。

---

# 4. EntityToVertices 的缓存存在数据竞争和生命周期问题

— ✅ 已完成（2026-09-13）：缓存已整块删除，不是「加锁修一修」。`entityToVertices`
退回纯函数，`eraseEntityVertexCache` / `clearEntityVertexCache` 两个外部失效 API 与
`SceneRefreshCoordinator` 里的三处调用一并去掉。判断「几何与上一轮是否相同」现在只由
`RenderSceneBuilder` 的段位哈希回答 —— 它属于持有几何块的那一层，自比对、不需要任何
外部失效调用，因此本节列出的数据竞争与生命周期问题在结构上不再存在。下述原分析保留
作为背景，其中「最低限度修复」已不再需要执行。

## 当前问题

缓存是进程级静态对象：

```cpp
static std::unordered_map<uint64_t, CachedVertexData> s_vertexCache;
static std::shared_mutex s_cacheMutex;
```

读取使用锁，但写入没有锁：

[ EntityToVertices.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:121)

删除和清空也没有锁：

[ EntityToVertices.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:363)

## 最低限度修复

```cpp
void storeCache(
    uint64_t entityId,
    uint64_t geometryHash,
    const std::vector<Render::VertexP3C3>& vertices,
    Render::PrimitiveType type)
{
    std::unique_lock lock(s_cacheMutex);

    auto& entry = s_vertexCache[entityId];
    entry.geometryHash = geometryHash;
    entry.vertices = vertices;
    entry.primType = type;
}
```

```cpp
void eraseEntityVertexCache(uint64_t entityId)
{
    std::unique_lock lock(s_cacheMutex);
    s_vertexCache.erase(entityId);
}

void clearEntityVertexCache()
{
    std::unique_lock lock(s_cacheMutex);
    s_vertexCache.clear();
}
```

同时补充：

```cpp
#include <mutex>
```

## 推荐重构

不要使用静态缓存，改成文档或渲染桥接对象拥有：

```cpp
class SceneRenderCache
{
public:
    bool find(
        EntityId id,
        uint64_t geometryRevision,
        CachedVertexData& out) const;

    void put(
        EntityId id,
        uint64_t geometryRevision,
        CachedVertexData value);

    void erase(EntityId id);
    void clear();

private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<EntityId, CachedVertexData> m_cache;
};
```

缓存键至少包含：

```text
DocumentId
EntityId
GeometryRevision
TessellationRevision
```

```cpp
struct CacheKey
{
    uint64_t documentId;
    uint64_t entityId;
    uint64_t geometryRevision;
    uint64_t tessellationRevision;

    bool operator==(const CacheKey&) const = default;
};
```

## 不建议使用 hash 作为正确性依据

`contentHash` 只能作为性能优化，不能作为数据正确性的唯一依据。

推荐实体保存版本号：

```cpp
struct EntityRevision
{
    uint64_t geometry = 0;
    uint64_t style = 0;
    uint64_t visibility = 0;
};
```

判断：

```cpp
if (entry.geometryRevision == entity.geometryRevision())
{
    // 可以跳过几何重建
}
```

Hash 只作为额外校验。

---

# 5. 3D 没有视锥剔除

## 当前问题

`Mesh3DBuilder` 明确关闭剔除：

[ Mesh3DBuilder.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/Mesh3DBuilder.cpp:101)

原因是 RenderX 当前只有二维 AABB：

```text
minX, minY, maxX, maxY
```

## 重构目标

2D 和 3D 使用两个明确的 DrawList 能力。

### 方案一：新增 3D API

```cpp
struct RxAabb3
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

struct RxFrustum
{
    float planes[6][4];
};
```

新增：

```cpp
RxResult rxDrawListUpsert3D(
    RuntimeHandle runtime,
    DrawListHandle list,
    uint32_t slot,
    const DrawCommand* command,
    const RxAabb3* bounds);
```

```cpp
RxResult rxSessionSubmitDrawList3D(
    SessionHandle session,
    DrawListHandle list,
    const RxFrustum* frustum);
```

### 方案二：统一 Bounds 结构

```cpp
enum class BoundsKind : uint8_t
{
    None = 0,
    Aabb2,
    Aabb3
};

struct Bounds
{
    BoundsKind kind;
    uint8_t reserved[3];
    float values[6];
};
```

这种方式可以减少函数数量，但 ABI 处理要更严格。

## Mesh3DBuilder 修改

```cpp
struct Entry
{
    uint64_t blockId = 0;
    uint32_t slot = 0;

    RxAabb3 bounds{};
    uint8_t boundsValid = 0;

    uint64_t geometryRevision = 0;
};
```

```cpp
void Mesh3DBuilder::writeCommand(uint64_t id, Entry& entry)
{
    Render::RT::DrawCommand command{};

    command.vertexBuffer = entry.buffer;
    command.vertexOffset = entry.byteOffset;
    command.vertexCount = entry.vertexCount;

    rxDrawListUpsert3D(
        m_runtime,
        m_drawList,
        entry.slot,
        &command,
        entry.boundsValid ? &entry.bounds : nullptr);
}
```

## 推荐实施顺序

先实现 CPU 视锥剔除：

```text
Camera Frustum
    → Entity AABB
    → visible list
    → DrawList submit
```

后续再增加 GPU indirect culling。

不要一开始就使用 Compute Shader 剔除。CPU 版本更容易验证正确性。

---

# 6. 场景数据和渲染数据之间缺少快照层

## 当前问题

`SceneRefreshCoordinator` 通过：

```cpp
SceneManager::findEntityById()
```

拿到可变实体指针，再转换为渲染数据。

`SceneManager` 的注释也要求：

```text
只能在没有并发写时读取
```

[ SceneManager.h ](C:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Core/SceneManager.h:76)

这会产生两个风险：

- UI 修改实体时，后台渲染线程读取到中间状态；
- 多个 Viewport 使用共享 dirty 集合时互相清理状态。

## 建议引入 SceneChangeSet

```cpp
enum class SceneChangeKind : uint8_t
{
    Added,
    Removed,
    GeometryChanged,
    StyleChanged,
    VisibilityChanged,
    LayerChanged,
    SelectionChanged,
    StructureChanged
};

struct SceneChange
{
    EntityId entityId = 0;
    SceneChangeKind kind{};
    uint64_t documentRevision = 0;
    uint64_t geometryRevision = 0;
    uint64_t styleRevision = 0;
};

struct SceneChangeSet
{
    uint64_t fromRevision = 0;
    uint64_t toRevision = 0;
    std::vector<SceneChange> changes;
};
```

## SceneManager 不再暴露 dirty 集合

目前：

```cpp
std::unordered_set<EntityId> dirtyEntities() const;
std::unordered_set<EntityId> deletedEntityIds() const;
void markClean();
```

建议改成：

```cpp
class ISceneChangeStream
{
public:
    using Cursor = uint64_t;

    virtual Cursor currentRevision() const = 0;

    virtual bool readChanges(
        Cursor from,
        SceneChangeSet& output) const = 0;
};
```

每个渲染视口拥有自己的 cursor：

```cpp
class SceneRenderBridge
{
    uint64_t m_lastRevision = 0;

    void synchronize()
    {
        SceneChangeSet changes;

        if (!m_scene->readChanges(m_lastRevision, changes))
        {
            rebuildAll();
            m_lastRevision = m_scene->currentRevision();
            return;
        }

        applyChanges(changes);
        m_lastRevision = changes.toRevision;
    }
};
```

这样不会出现：

```text
Viewport A 调用 markClean()
Viewport B 丢失自己的 dirty 信息
```

## Render Snapshot

对于真正的多线程渲染，建议增加：

```cpp
struct RenderEntitySnapshot
{
    EntityId id = 0;
    EntityType type{};
    uint64_t geometryRevision = 0;
    uint64_t styleRevision = 0;

    Render::BBox2d bounds2D;
    Render::BBox3f bounds3D;

    std::shared_ptr<const GeometryData> geometry;
    RenderStyle style;
    uint8_t visible = 1;
};

class SceneSnapshot
{
public:
    uint64_t revision() const;

    const RenderEntitySnapshot* find(EntityId id) const;
};
```

渲染层只读取：

```cpp
std::shared_ptr<const SceneSnapshot>
```

而不是直接读取正在编辑的实体对象。

---

# 7. SceneRenderContract 抽象过宽

## 当前问题

当前 `ISceneGeometrySink` 同时包含：

- 2D 折线；
- 圆；
- 圆弧；
- 椭圆；
- 文字；
- 图片；
- 2D 三角形；
- 3D 三角网格；
- 3D 包围盒。

[ SceneRenderContract.h ](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/Scene/SceneRenderContract.h:28)

结果是：

- 3D Builder 必须实现大量 2D 空函数；
- 2D Builder 必须实现 3D 空函数；
- 文本、图片与几何资源生命周期完全不同，却放在一个接口中。

## 推荐拆分

```cpp
class IScene2DGeometrySink
{
public:
    virtual ~IScene2DGeometrySink() = default;

    virtual void setCurrentEntityId(EntityId) = 0;
    virtual void emitPolyline(...) = 0;
    virtual void emitCircle(...) = 0;
    virtual void emitArc(...) = 0;
    virtual void emitEllipse(...) = 0;
    virtual void emitTriangles(...) = 0;
};
```

```cpp
class IScene3DGeometrySink
{
public:
    virtual ~IScene3DGeometrySink() = default;

    virtual void setCurrentEntityId(EntityId) = 0;
    virtual void emitTriangleSoup(...) = 0;
    virtual void emitBBox(...) = 0;
};
```

```cpp
class ITextSink
{
public:
    virtual ~ITextSink() = default;

    virtual void emitText(const TextPrimitive&) = 0;
};
```

```cpp
class IImageSink
{
public:
    virtual ~IImageSink() = default;

    virtual void emitImage(const ImagePrimitive&) = 0;
};
```

### 数据源也拆分

```cpp
class ISceneDataSource2D
{
public:
    virtual ~ISceneDataSource2D() = default;

    virtual void gatherGeometry(IScene2DGeometrySink&) const = 0;
    virtual void gatherText(ITextSink&) const = 0;
    virtual void gatherImages(IImageSink&) const = 0;
};
```

```cpp
class ISceneDataSource3D
{
public:
    virtual ~ISceneDataSource3D() = default;

    virtual void gatherGeometry(IScene3DGeometrySink&) const = 0;
};
```

这样不需要再写很多空实现。

---

# 8. C++ ABI 和跨 DLL 接口设计不统一

## 当前问题

`renderx.h` 的方向较好，但它仍然是 C++ 头文件，不是纯 C ABI。

`ISceneManager` 的注释也明确承认 STL 在虚函数中存在 ABI 风险：

[ ISceneManager.h ](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/ISceneManager.h:21)

`SceneDocumentBase` 又使用了 `const char*`，并通过默认实现隐藏未实现功能：

[ SceneDocumentBase.h ](C:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/SceneDocumentBase.h:15)

## 建议分成两种接口

### 进程内部 C++ 接口

允许：

```cpp
std::string
std::vector
std::function
std::shared_ptr
QObject
```

但必须规定：

```text
只在同一编译器、同一 CRT、同一进程内部使用
```

例如：

```cpp
namespace Internal
{
    class ISceneManager
    {
    public:
        virtual ~ISceneManager() = default;
        virtual std::vector<EntityId> entities() const = 0;
    };
}
```

### DLL/插件 C ABI

只使用：

```text
uint8_t
uint32_t
uint64_t
float
double
const void*
函数指针
显式内存所有权
```

例如：

```c
typedef uint64_t rx_entity_id;

typedef struct rx_vec3
{
    float x;
    float y;
    float z;
} rx_vec3;

typedef struct rx_toolpath_line
{
    const float* points;
    uint32_t point_count;
    float color[4];
    uint8_t travel;
    uint8_t reserved[3];
} rx_toolpath_line;
```

当前 `RenderToolpathOverlayLine::travel` 是 `bool`，如果声称跨 DLL 安全，应改成 `uint8_t`。

## SceneDocumentBase 建议

不要用字符串 ID 作为核心接口：

```cpp
virtual bool removeEntity(const char* id) = 0;
```

改为：

```cpp
using EntityId = uint64_t;

class ISceneDocument
{
public:
    virtual ~ISceneDocument() = default;

    virtual bool removeEntity(EntityId id) = 0;
    virtual void clear() = 0;

    virtual bool isModified() const = 0;
};
```

字符串只用于 UI 显示和文件格式。

## 默认实现应减少

当前：

```cpp
virtual void setDocumentName(const char*) {}
virtual bool isModified() const { return false; }
```

容易导致某个实现忘记实现却没有报错。

建议：

- 真正必需的接口设为纯虚；
- 可选能力单独放到 capability interface；
- 不用默认空实现掩盖功能缺失。

---

# 9. UICommon 模块依赖方向不合理

## 当前问题

`UICommon` 对外依赖：

- Engine2D；
- EnginePersistence；
- Log；
- SQLiteCpp；
- RenderX。

见：

[ UI/Common/CMakeLists.txt ](C:/Users/xx/Documents/Cpp/CAD/UI/Common/CMakeLists.txt:136)

这导致 UICommon 并不是真正的公共 UI 层。

另外 `RenderTypes.h` 直接 include：

```cpp
Engine2D/Interaction/SnapEngine.h
```

这样一个 UI 公共头又反向依赖 Engine2D。

## 建议的 CMake 目标

```text
Utility
EngineCommon
InteractionCommon
RenderContract
RenderBridge
Engine2D
Engine3D
UIFoundation
UICommon
UI2D
UI3D
Main
```

推荐依赖：

```text
UIFoundation
    → Qt + Utility + EngineCommon

UICommon
    → UIFoundation + RenderContract

RenderContract
    → Utility

RenderBridge
    → RenderContract + RenderX

UI2D
    → UICommon + Engine2D + RenderBridge

UI3D
    → UICommon + Engine3D + RenderBridge
```

## CMake 修改方向

`UI/Common/CMakeLists.txt`：

```cmake
target_link_libraries(UICommon
    PUBLIC
        Utility
        EngineCommon
        UIFoundation
)
```

删除：

```cmake
Engine2D
EnginePersistence
RenderX
SQLiteCpp
```

`UI/2D/CMakeLists.txt`：

```cmake
target_link_libraries(UI2D
    PUBLIC
        UICommon
        Engine2D
        RenderBridge
)
```

`UI/3D/CMakeLists.txt`：

```cmake
target_link_libraries(UI3D
    PUBLIC
        UICommon
        Engine3D
        RenderBridge
)
```

RenderX 尽量只在 `RenderBridge` 的实现层出现。

## SnapFlag

将 `SnapFlag` 移动到：

```text
Engine/Common/Interaction/SnapTypes.h
```

或：

```text
InteractionCommon
```

不要让 UICommon 为了 SnapFlag 依赖完整的 Engine2D。

---

# 10. Engine3D 依赖 Engine2D

## 当前问题

`Engine3D/CMakeLists.txt` 私有链接 `Engine2D`：

[ Engine/3D/CMakeLists.txt ](C:/Users/xx/Documents/Cpp/CAD/Engine/3D/CMakeLists.txt:76)

这会造成：

```text
Engine3D → Engine2D
```

如果以后 2D 和 3D 使用不同算法或独立加载，依赖方向会限制演进。

## 建议

将真正共享的类型移到 `EngineCommon`：

```text
EngineCommon/
    EntityId
    EntityType
    GeometryPrimitive
    SceneChange
    SelectionTypes
    LayerId
    GroupId
```

如果 Engine3D 使用了 2D 的数学、颜色或公共几何类型，应迁移到：

```text
Utility
EngineCommon
GeometryCommon
```

Engine3D 的 CMake 应改为：

```cmake
target_link_libraries(Engine3D
    PUBLIC
        Utility
        EngineCommon
        Log
    PRIVATE
        Boost::boost
)
```

如果文件解析需要同时访问 Engine2D 和 Engine3D，应由 FileIO 依赖二者：

```text
FileIO → Engine2D
FileIO → Engine3D
```

而不是：

```text
Engine3D → Engine2D
```

---

# 11. 2D/3D 操作总线重复

## 当前问题

2D 和 3D 各自拥有：

```text
OperationBus
OperationRegistry
CommandCatalog
OperationRouting
```

虽然当前已有 `OperationBusBase`，但上层仍然存在两套命令系统。

## 重构目标

共享命令内核，2D/3D 只提供扩展命令。

## 公共命令定义

```cpp
using CommandId = uint32_t;

enum class CommandScope : uint8_t
{
    Global = 0,
    Document,
    Viewport,
    Selection
};

struct CommandDescriptor
{
    CommandId id = 0;
    const char* name = nullptr;
    CommandScope scope = CommandScope::Global;

    uint8_t checkable = 0;
    uint8_t visible = 1;
    uint8_t enabled = 1;
};
```

```cpp
struct CommandContext
{
    DocumentSession* document = nullptr;
    ViewportContext* viewport = nullptr;
    SelectionModel* selection = nullptr;
};
```

```cpp
class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual CommandId id() const = 0;
    virtual bool canExecute(const CommandContext&) const = 0;
    virtual CommandResult execute(const CommandContext&) = 0;
};
```

## 2D/3D 注册

```cpp
class CommandRegistry
{
public:
    void registerCommand(std::unique_ptr<ICommand>);
    ICommand* find(CommandId id) const;
};
```

2D：

```cpp
registry.registerCommand(std::make_unique<DrawLineCommand>());
registry.registerCommand(std::make_unique<DrawCircleCommand>());
```

3D：

```cpp
registry.registerCommand(std::make_unique<ViewFrontCommand>());
registry.registerCommand(std::make_unique<SplitByPickPlaneCommand>());
```

UI 配置文件只引用：

```json
{
    "command": "view.fit",
    "text": "Fit View",
    "shortcut": "F"
}
```

而不直接引用 C++ 类。

---

# 12. Selection 状态存在多个真源

## 当前问题

选择状态分散在：

```text
SceneManager
SelectionManager
SceneDocument
SelectionSet
RenderWidget
SceneNode
```

这样很容易出现：

```text
Engine 已取消选择
UI 树仍显示选中
Renderer 仍绘制高亮
```

## 推荐职责划分

```text
SelectionModel
    唯一的应用层选择真源

Engine
    接收选择查询结果或选中 ID

RenderBridge
    消费 SelectionSnapshot

SceneTreeModel
    观察 SelectionModel
```

## SelectionModel

```cpp
class SelectionModel
{
public:
    using ChangeCallback = std::function<void()>;

    bool contains(EntityId id) const;

    void setSelected(EntityId id, bool selected);
    void replace(const std::vector<EntityId>& ids);
    void clear();

    std::vector<EntityId> ids() const;

    uint64_t revision() const;
};
```

渲染层只接收：

```cpp
struct SelectionSnapshot
{
    uint64_t revision = 0;
    std::vector<EntityId> selectedIds;
};
```

`SceneNode`：

```cpp
bool SceneNode::selected() const
{
    return m_selectionModel &&
           m_selectionModel->contains(m_engineEntityId);
}
```

不再直接读取 `SceneManager3D`。

---

# 13. SceneRefreshCoordinator 职责过多

## 当前问题

目前它同时负责：

- 读取场景 dirty；
- 读取实体；
- 调用几何转换；
- 管理缓存；
- 上传 GPU；
- 管理图片；
- 管理文字；
- 管理删除；
- 触发 Qt update；
- 并行处理。

相关代码集中在：

[ SceneRefreshCoordinator.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/SceneRefreshCoordinator.cpp:676)

## 建议拆成四个类

### SceneChangeTracker

```cpp
class SceneChangeTracker
{
public:
    SceneChangeSet collect();
};
```

### SceneRenderBridge

```cpp
class SceneRenderBridge
{
public:
    void apply(const SceneChangeSet& changes);

private:
    void rebuildEntity(EntityId id);
    void removeEntity(EntityId id);
    void updateStyle(EntityId id);
};
```

### RenderUploadQueue

```cpp
class RenderUploadQueue
{
public:
    void enqueue(RenderUploadCommand command);

    void flush(RenderSessionHost& session);
};
```

### ViewportRefreshScheduler

```cpp
class ViewportRefreshScheduler
{
public:
    void requestRepaint();
    void requestSceneUpdate();
    void requestFullRebuild();

private:
    QTimer* m_timer = nullptr;
};
```

最终：

```text
SceneManager
    → SceneChangeTracker
    → SceneRenderBridge
    → RenderUploadQueue
    → RenderWidget
```

Qt Timer 只负责调度，不应负责场景转换和 GPU 资源策略。

---

# 14. GPU 资源操作和 Qt UI 线程耦合

## 当前问题

当前代码通过 `makeCurrent()` 在场景刷新和实体上传过程中直接操作 OpenGL 资源。

这在 QOpenGLWidget 模式下可能可以运行，但会造成：

- 渲染线程无法独立；
- 后期 Vulkan/Metal 无法照搬；
- 后台算法线程不能安全提交；
- 资源释放时容易出现上下文生命周期问题。

## 建议使用上传队列

```cpp
struct RenderUploadCommand
{
    enum class Type : uint8_t
    {
        CreateGeometry,
        UpdateGeometry,
        RemoveGeometry,
        UpdateMaterial,
        UpdateOverlay
    };

    Type type{};
    EntityId entityId = 0;

    std::shared_ptr<const RenderGeometry> geometry;
    RenderStyle style;
};
```

后台线程只生成命令：

```cpp
m_uploadQueue.enqueue({
    RenderUploadCommand::Type::UpdateGeometry,
    id,
    geometry,
    style
});
```

渲染帧开始时统一执行：

```cpp
void ViewportRenderer::renderFrame()
{
    m_uploadQueue.flush(*m_session);

    m_session->beginFrame();
    submitScene();
    submitOverlay();
    m_session->endFrame();
}
```

这样：

```text
后台线程：生成 CPU RenderData
渲染线程：操作 GPU
Qt 主线程：处理输入和界面
```

三者职责清晰。

---

# 15. 2D 百万级图元不能长期采用“每实体一个 GPU 块”

## 当前问题

当前 `RenderSceneBuilder` 的组织形式是：

```text
EntityId
    → 多个 Piece
        → DrawList Slot
        → GeometryStore Block
```

这适合编辑性，但当实体数量达到几十万或百万时，会有：

- 大量 `unordered_map` 节点；
- 大量 DrawList slot；
- 大量 GPU 小块；
- 内存碎片；
- 合批效果差；
- 更新时频繁释放和重新分配。

## 建议引入 Chunk

```cpp
struct RenderChunkKey
{
    uint32_t layerId = 0;
    int32_t tileX = 0;
    int32_t tileY = 0;
    uint32_t materialId = 0;

    bool operator==(const RenderChunkKey&) const = default;
};
```

```cpp
struct EntityRenderRange
{
    EntityId entityId = 0;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};
```

```cpp
struct RenderChunk
{
    RenderChunkKey key;

    Render::RT::GeometryBlock geometry;
    Render::RT::DrawCommand command;

    std::vector<EntityRenderRange> ranges;

    Render::BBox2d bounds;
    uint64_t revision = 0;
};
```

场景结构：

```text
Layer
 └── Tile
      └── Material Batch
           └── RenderChunk
                └── EntityRenderRange
```

编辑单个实体时：

```text
找到 EntityRenderRange
    → 标记所在 Chunk 脏
    → 只重建该 Chunk
```

## GeometryStore 更新优化

当前 `upsertEntity()` 倾向于释放旧 Piece 后重新分配。

可以增加：

```cpp
bool updateInPlace(
    GeometryBlock block,
    const void* data,
    uint32_t bytes);
```

逻辑：

```cpp
if (oldBlock.capacity >= newBytes)
{
    rxGeometryWrite(oldBlock, data, newBytes);
}
else
{
    allocateNewBlock();
    freeOldBlock();
}
```

这样拖动或修改同等规模图元时，不会频繁造成碎片。

## 颜色数据优化

当前 2D 顶点包含颜色：

```cpp
struct VertexP3C3
{
    float px, py, pz;
    float cr, cg, cb;
};
```

颜色变化会导致整个顶点流更新。

可以根据图层组织方式改成：

```cpp
struct VertexP3
{
    float px, py, pz;
};
```

颜色放到：

```text
Material
DrawCommand
Instance Buffer
```

如果同一 Chunk 颜色相同，颜色完全不应存在每个顶点中。

---

# 16. 3D Mesh3DBuilder 应提取公共资源管理代码

## 当前问题

`RenderSceneBuilder` 和 `Mesh3DBuilder` 有大量同构逻辑：

- Runtime 生命周期；
- GeometryStore；
- DrawList；
- Slot 分配；
- Entity → Entry；
- 删除和回收；
- GeometryStore flush；
- DrawCommand 更新。

但目前两套代码各自实现。

## 建议提取内部公共类

```cpp
template <typename Entry>
class PersistentRenderScene
{
public:
    bool initialize(Render::RT::RuntimeHandle runtime);
    void shutdown();

    uint32_t acquireSlot();
    void releaseSlot(uint32_t slot);

    void removeSlot(uint32_t slot);

    Render::RT::GeometryStoreHandle geometryStore() const;
    Render::RT::DrawListHandle drawList() const;

protected:
    Render::RT::RuntimeHandle m_runtime{};
    Render::RT::GeometryStoreHandle m_store{};
    Render::RT::DrawListHandle m_drawList{};

    std::vector<uint32_t> m_freeSlots;
    uint32_t m_nextSlot = 0;
};
```

2D：

```cpp
class RenderSceneBuilder
    : public PersistentRenderScene<Render2DEntry>
{
};
```

3D：

```cpp
class Mesh3DBuilder
    : public PersistentRenderScene<Mesh3DEntry>
{
};
```

不要把 2D/3D 的几何处理也模板化到一起，只共享：

```text
资源生命周期
槽位管理
块管理
命令台账
```

2D 和 3D 的顶点格式、材质、剔除、拓扑仍然独立。

---

# 17. 3D 假抽象和占位接口

## 当前问题

`IRenderer3D` 包含：

```cpp
QPainter&
QString
std::function
SceneDocument3DAdapter*
isOpenGL()
```

[ IRenderer3D.h ](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Render3D/IRenderer3D.h:18)

但实际：

- `render(QPainter&)` 不使用 QPainter；
- `isOrbitMode()` 恒返回 true；
- `isOpenGL()` 恒返回 true；
- `setOrbitMode()` 没有真实逻辑。

## 建议删除旧接口

新的接口应该是：

```cpp
class IViewportRenderer
{
public:
    virtual ~IViewportRenderer() = default;

    virtual bool initialize(const ViewportCreateInfo&) = 0;
    virtual void shutdown() = 0;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void render(const RenderFrameView&) = 0;

    virtual RendererCapabilities capabilities() const = 0;
};
```

相机和交互不要塞到 Renderer：

```cpp
class CameraController3D;
class NavigationController3D;
class SelectionController3D;
```

职责：

```text
Renderer：绘制
CameraController：相机状态
InputRouter：输入路由
SelectionController：拾取与选择
```

---

# 18. 刀路覆盖、表面拾取、离屏渲染应改成真实能力接口

## 当前问题

当前以下功能为占位：

- `setToolpathOverlay()` 丢弃数据；
- `hasToolpathOverlay()` 恒 false；
- Mesh surface pick 不执行；
- `captureOffscreen()` 返回空图像。

[ RenderWidget3D.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.cpp:529)

## 刀路覆盖层

不要让业务直接把刀路数组交给 `RenderWidget3D`。

新增：

```cpp
struct ToolpathSegment
{
    Ut::Vec3f start;
    Ut::Vec3f end;
    Ut::Color color;
    uint8_t travel = 0;
};

struct ToolpathOverlay
{
    uint64_t revision = 0;
    std::vector<ToolpathSegment> segments;
};
```

渲染层统一处理：

```cpp
class OverlayScene
{
public:
    void setToolpath(const ToolpathOverlay&);
    void clearToolpath();

    void submit(RenderFrame&);
};
```

## 表面拾取

当前 `SceneManager3D` 已经有：

```cpp
raycastHitFirst(...)
```

可以先实现 CPU 拾取：

```cpp
struct PickResult
{
    uint8_t hit = 0;
    EntityId entityId = 0;
    Ut::Vec3f position;
    Ut::Vec3f normal;
    uint32_t triangleIndex = 0;
};
```

```cpp
PickResult PickingService3D::pick(
    const Ray3f& ray,
    const SceneSnapshot3D& snapshot);
```

之后再升级 GPU ID Buffer。

不要让 `RenderWidget3D` 保存业务回调：

```cpp
setMeshSurfacePickHandler(...)
```

建议由 `PickingService3D` 返回结果，Controller 决定如何处理。

## 离屏渲染

RenderX 已经有 RenderTarget/Texture 概念，应该让 RenderX 返回像素缓冲：

```cpp
struct RenderImage
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowPitch = 0;
    std::vector<uint8_t> rgba8;
};
```

```cpp
bool IViewportRenderer::renderOffscreen(
    const RenderFrameView& frame,
    uint32_t width,
    uint32_t height,
    RenderImage& output);
```

Qt 层再转换：

```cpp
QImage toQImage(const RenderImage& image);
```

不要让底层 Renderer 直接返回 `QImage`。

---

# 19. RenderX RHI 设计较大，但实际后端太少

## 当前问题

当前 RHI 已经设计了：

- Buffer；
- Texture；
- Sampler；
- BindGroup；
- Graphics Pipeline；
- Compute Pipeline；
- CommandList；
- Indirect Draw；
- Readback；
- Surface；
- Shader Language。

但真实实现仍只有 OpenGL。

这不是架构错误，但存在“接口先行过度设计”的风险。

## 建议采用最小能力集

先定义所有后端必须支持的核心子集：

```text
必需：
- Vertex Buffer
- Index Buffer
- Uniform/Push Constants
- Texture
- Sampler
- Graphics Pipeline
- Depth Test
- Alpha Blend
- Render Target
- Readback

可选：
- Compute
- Indirect Draw
- Multi Draw Indirect
- Storage Buffer
- Async Readback
```

能力查询：

```cpp
struct BackendCapabilities
{
    uint8_t graphics = 1;
    uint8_t textures = 1;
    uint8_t depth = 1;
    uint8_t blending = 1;

    uint8_t compute = 0;
    uint8_t indirectDraw = 0;
    uint8_t multiDrawIndirect = 0;
};
```

渲染器必须根据能力降级：

```cpp
if (caps.compute)
{
    gpuCulling();
}
else
{
    cpuCulling();
}
```

Null 后端不能把所有能力都报告为 true，否则会掩盖真实后端缺陷。

## Shader 方案

当前构建配置支持 `.spv`、`.metal`、`.metallib`，但实际 shader 列表主要是 GLSL：

[ Renderx/CMakeLists.txt ](C:/Users/xx/Documents/Cpp/CAD/Renderx/CMakeLists.txt:82)

建议明确采用一种方案：

### 方案 A：统一源代码，构建期转换

```text
shader source
    → SPIR-V
    → Vulkan
    → SPIRV-Cross → MSL
```

OpenGL 可使用 GLSL 或 SPIR-V 转换结果。

### 方案 B：每后端独立 Shader

```text
shader/
    common/
    opengl/
    vulkan/
    metal/
```

对于 CAD 的固定管线数量，方案 B 更简单，但维护文件更多。

不要只设计 shader loader，而不提供实际的 shader 编译和验证流程。

---

# 20. RenderX GeometryStore 增长语义需要统一

当前不同代码位置对 GeometryStore 扩容后 BufferHandle 是否稳定的注释存在不一致。

建议明确契约：

```cpp
struct GeometryBlock
{
    uint64_t blockId = 0;
    BufferHandle buffer = BufferHandle::Invalid;
    uint32_t byteOffset = 0;
    uint32_t byteSize = 0;
};
```

扩容后规定：

### 方案 A：BufferHandle 稳定

底层内部迁移数据，但句柄语义保持不变。

优点：

```text
DrawCommand 不需要刷新
```

### 方案 B：BufferHandle 失效

扩容后所有 Block 必须重新查询。

优点：

```text
底层实现简单
```

但必须提供：

```cpp
RxResult rxGeometryStoreRefreshBlock(
    RuntimeHandle runtime,
    GeometryStoreHandle store,
    uint64_t blockId,
    GeometryBlock* output);
```

无论选择哪种方案，`RenderSceneBuilder` 和 `Mesh3DBuilder` 必须使用同一契约，并增加测试：

```text
连续分配直到 GeometryStore 扩容
验证旧图元仍然正常绘制
验证所有 DrawCommand 指向有效 Buffer
```

---

# 21. 算法层与 UI/Renderer 解耦

## 当前问题

当前已经有算法服务，但算法、UI、3D 预览之间仍然存在直接联系，例如刀路预览直接调用 Widget 接口。

## 推荐结构

```text
AlgorithmInput
    ↓
IAlgorithmStrategy
    ↓
AlgorithmResult
    ↓
Application Command
    ↓
SceneChangeSet
    ↓
RenderBridge
```

## 算法接口

```cpp
struct AlgorithmInput
{
    std::shared_ptr<const DocumentSnapshot> document;
    std::vector<EntityId> selectedEntities;
    AlgorithmParameters parameters;
};
```

```cpp
struct AlgorithmResult
{
    bool success = false;

    std::vector<GeometryPatch> geometryPatches;
    std::shared_ptr<const ToolpathData> toolpath;
    std::shared_ptr<const PreviewMesh> previewMesh;

    std::vector<AlgorithmMessage> messages;
};
```

```cpp
class IAlgorithmStrategy
{
public:
    virtual ~IAlgorithmStrategy() = default;

    virtual const char* strategyId() const = 0;

    virtual AlgorithmResult run(
        const AlgorithmInput& input,
        CancellationToken cancellation,
        ProgressSink progress) = 0;
};
```

应用层：

```cpp
class AlgorithmApplicationService
{
public:
    AlgorithmResult execute(
        const std::string& strategyId,
        const AlgorithmParameters& parameters);
};
```

算法实现不能 include：

```cpp
QWidget
QDialog
RenderWidget
renderx.h
QPainter
```

## 策略注册

```cpp
class AlgorithmStrategyRegistry
{
public:
    void registerStrategy(std::unique_ptr<IAlgorithmStrategy>);
    IAlgorithmStrategy* find(std::string_view id) const;
};
```

以后可以添加：

```text
nesting.fast
nesting.quality
relief.cpu
relief.gpu
toolpath.contour
toolpath.hatch
```

而不修改 UI 主流程。

---

# 22. UI 定制应围绕 CommandDescriptor，而不是具体类

## 当前问题

当前 UI 配置化方向已经存在，但底层 UI 仍然大量依赖具体服务、具体文档和具体 Renderer。

## 建议 UI 配置只引用稳定标识

```json
{
    "menus": [
        {
            "id": "main.file",
            "items": [
                "document.new",
                "document.open",
                "document.save"
            ]
        }
    ],
    "toolbars": [
        {
            "id": "viewport.navigation",
            "items": [
                "view.fit",
                "view.front",
                "view.top"
            ]
        }
    ]
}
```

代码侧：

```cpp
struct ActionState
{
    bool visible = true;
    bool enabled = true;
    bool checked = false;
    std::string text;
    std::string icon;
};

class IActionModel
{
public:
    virtual ActionState state(CommandId id) const = 0;
    virtual void trigger(CommandId id) = 0;
};
```

UI 只依赖：

```cpp
IActionModel
```

不直接依赖：

```cpp
SceneManager3D
SceneEditService3D
RenderWidget3D
```

后端能力不支持时：

```cpp
if (!renderer.capabilities().supportsOffscreenCapture)
{
    action.setVisible(false);
}
```

不要把菜单显示出来，点击后才发现函数是空实现。

---

# 23. RenderTypes 应从 UICommon 中继续拆分

当前 `RenderTypes.h` 虽然已经不放在 RenderX 内部，但仍然混合了：

- 顶点格式；
- CAD Overlay；
- SnapFlag；
- UI 文本；
- 2D/3D 渲染类型；
- STL 容器。

建议拆成：

```text
RenderContract/
    VertexTypes.h
    PrimitiveTypes.h
    RenderStyle.h
    RenderFrame.h

EditorOverlay/
    SelectionOverlay.h
    SnapOverlay.h
    ToolPreview.h

TextRender/
    TextPrimitive.h
    TextLayout.h

ImageRender/
    ImagePrimitive.h
```

例如：

```cpp
// RenderContract/VertexTypes.h
struct VertexP3C3 { ... };

// EditorOverlay/SelectionOverlay.h
struct SelectionOutline { ... };

// TextRender/TextPrimitive.h
struct TextPrimitive { ... };
```

`RenderContract` 不应 include：

```cpp
Engine2D/Interaction/SnapEngine.h
```

`SnapFlag` 应放到更低层的 InteractionCommon。

---

# 24. OverlayState 的字段过多，应拆成 Layer

— ✅ 已完成（2026-09-13）：覆盖层已整体移入 `RenderBridge::OverlayScene`，并按要求拆成
独立层。落点与本文设想的两处差异，都是落地时才看清的约束：

- **层号顺序 = 提交顺序 = 叠放顺序**，写死在 `OverlayLayerId` 枚举里，不再靠一个共享的
  自增 `seq`（那个 `seq` 会随调用顺序漂移）。枚举按原有提交次序排列，因此叠放不变。
- 设成 `replaceLayer(id, shared_ptr<const OverlayLayer>)` 需要所有层共用一种载荷类型，
  但这九层的载荷结构互不相同（包围盒 / 四边形列表 / 带弧长的轮廓路径 / 标记组 / 形状+颜色），
  塞进一个通用 POD 只会变成一个 `void*` 袋子。因此改为**按层的强类型 setter**
  （`setSelectionBox` / `setSelectionOutlines` / `setSelectionHandles` / …）+ 统一的
  `clearLayer(id)` / `clear()`；「各工具只管理自己的图层」这一条由类型保证 ——
  写哪层只影响哪层，不再需要「只清 update 实际携带的组」那条防御性注释来兜。

另外，`OverlayState` 的 `snapType`（`Engine2D::SnapEngine::SnapFlag`）不再进入渲染侧：
形状与颜色的映射留在 UI2D 的 `ViewRenderCoordinator`，`OverlayScene` 只收中性的
`SnapMarkerShape` + `Render::Color`，渲染桥接层因此不依赖 Engine2D 的捕捉语义。

## 当前问题

`RenderOverlayUpdate` 拥有很多：

```cpp
hasPreviewPoints
hasControlLines
hasSelectionBox
hasSelectionHandles
hasSelectionOutlines
hasSnapIndicator
hasUiTexts
```

这种“一个结构体承载所有局部修改”的方式长期会越来越复杂。

## 推荐改成图层

```cpp
enum class OverlayLayerId : uint8_t
{
    ToolPreview,
    ControlLines,
    Selection,
    Snap,
    UiText,
    Toolpath
};
```

```cpp
class OverlayScene
{
public:
    void replaceLayer(
        OverlayLayerId id,
        std::shared_ptr<const OverlayLayer> layer);

    void clearLayer(OverlayLayerId id);

    void submit(RenderFrame& frame) const;
};
```

各工具只管理自己的图层：

```cpp
overlayScene.replaceLayer(
    OverlayLayerId::ToolPreview,
    buildLineToolPreview());
```

选择系统：

```cpp
overlayScene.replaceLayer(
    OverlayLayerId::Selection,
    buildSelectionOverlay());
```

这样不需要每次修改都理解整个 `RenderOverlayUpdate`。

---

# 25. 2D/3D 相机、输入和视口接口需要拆开

## 当前问题

当前 `IViewportHost`、`IRenderer3D`、`RenderWidget` 同时涉及：

- 输入事件；
- 相机；
- 渲染；
- 选择；
- 测量；
- OpenGL；
- UI 更新。

## 推荐分层

```text
ViewportWidget
    接收 Qt 事件

ViewportInputRouter
    把事件转成统一输入事件

NavigationController
    平移、旋转、缩放

SelectionController
    拾取和框选

CameraModel
    相机状态

ViewportRenderer
    只绘制
```

统一事件：

```cpp
struct PointerEvent
{
    enum class Type : uint8_t
    {
        Press,
        Release,
        Move,
        Wheel
    };

    Type type{};
    float x = 0;
    float y = 0;
    uint32_t buttons = 0;
    uint32_t modifiers = 0;
};
```

2D：

```cpp
class InputRouter2D
{
    void route(const PointerEvent&);
};
```

3D：

```cpp
class InputRouter3D
{
    void route(const PointerEvent&);
};
```

共享的是事件协议，不是让 2D/3D 共用一个巨大 Widget 接口。

---

# 26. 小型重复和代码清理

## `SceneDocument2D` 中重复设置脏标记

当前 `createLine()` 存在重复：

```cpp
if (added)
{
    m_isModified = true;
}
if (added)
{
    m_isModified = true;
}
```

[ SceneDocument2D.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Documents/SceneDocument2D.cpp:101)

建议统一：

```cpp
if (!added)
{
    return {};
}

m_isModified = true;
return QString::number(added->id);
```

## 工厂名称

当前 `Renderer3DFactory` 的枚举只有：

```text
Compatible
None
```

但它实际不是后端工厂，只是 Qt 3D Widget 包装器工厂。

应改名为：

```text
ViewportRendererFactory
```

真正的后端工厂应位于：

```text
RenderBridge::BackendRendererFactory
```

## Stub 文件

Main 中存在多份 3D `IRenderer3D` Stub。建议统一：

```text
Testing/RenderStubs/
    NullViewportRenderer
    FakeRenderSession
    FakeSelectionRenderer
```

不要在多个模块复制同名接口。

---

# 27. 文档和代码需要建立一致性检查

当前文档中有部分内容与代码不一致，例如：

- README 描述的 CMake 版本与实际版本不一致；
- 文档说 3D 已经有统一主链，但代码中仍保留双文档；
- 文档说后端可扩展，但 Vulkan/Metal 仍未实现；
- 部分接口文档写成已支持，实际是占位。

建议在 README 增加真实状态表：

```markdown
| 功能 | 状态 |
|---|---|
| OpenGL | 已实现 |
| Null | 已实现 |
| Vulkan | 未实现 |
| Metal | 未实现 |
| 2D 增量更新 | 已实现 |
| 3D 视锥剔除 | 未实现 |
| 3D 刀路覆盖层 | 占位 |
| 3D 表面拾取 | CPU/占位 |
| 3D 离屏渲染 | 未实现 |
```

同时增加自动检查：

```text
Backend::Vulkan == available
    → 必须存在 Vulkan smoke test

supportsOffscreen == true
    → 必须有离屏截图测试

supportsMeshPicking == true
    → 必须有实际命中测试
```

---

# 推荐实施顺序

## 第一阶段：清理和止血

优先修改：

1. 合并 `SceneDocument3D` / `SceneDocument3DAdapter`。
2. 修复 `EntityToVertices` 缓存锁。
3. 合并 2D Tessellation。
4. 删除 3D Renderer 假接口。
5. 修复 `SceneDocument3D::removeEntity()` 和 `clear()`。
6. 修正 UICommon 和 Engine3D 依赖方向。
7. 明确当前后端能力状态。

## 第二阶段：建立 RenderBridge

新增：

```text
RenderContract
RenderBridge
RenderSessionHost
SceneChangeSet
SceneRenderCache
RenderUploadQueue
```

并将：

```text
RenderWidget
RenderWidget3D
SceneRefreshCoordinator
Mesh3DBuilder
```

逐步迁移到 RenderBridge。

## 第三阶段：实现统一场景更新

目标链路：

```text
SceneManager
    → SceneChangeSet
    → SceneSnapshot
    → RenderBridge
    → RenderUploadQueue
    → Renderer
```

移除：

```text
全局 dirty 集合
全局 vertex cache
直接读取实体指针
渲染层主动调用 SceneManager
```

## 第四阶段：实现真正后端切换

先完成：

```text
OpenGL + Null + 一个第二后端
```

验证：

```text
同一 RenderFrame
同一场景数据
同一选择结果
同一 2D/3D 视图
不同图形后端
```

## 第五阶段：大型场景优化

最后处理：

- Chunk；
- 空间剔除；
- 3D Frustum；
- GeometryStore 碎片整理；
- Indirect Draw；
- GPU Culling；
- 多视口共享资源。

---

# 最终建议

目前最值得立即做的不是继续增加 Vulkan/Metal 枚举，也不是继续增加 UI 接口，而是先完成这五项：

```text
1. 只保留一个 3D 文档模型
2. 只保留一套 2D Tessellation
3. 建立 RenderBridge 和 RenderSnapshot
4. 将 Qt/OpenGL 从通用 Renderer 接口中移除
5. 修复缓存并发和 3D 无剔除问题
```

完成后，项目才具备继续实现：

```text
OpenGL ⇄ Vulkan/Metal
2D ⇄ 3D
算法策略替换
百万级 2D 图元
UI 配置化
```

的稳定基础。# 详细重构方案与代码级修改建议

下面按上一份报告中的每个问题逐项展开。建议整体采用“先收口、再抽象、后扩展”的顺序，避免在现有多条并行链路上继续增加接口。

---

# 1. OpenGL/Vulkan/Metal 不能真正切换

## 当前问题

当前真实后端只有：

```text
OpenGL：可用
Null：可用
Vulkan：未实现
Metal：未实现
```

`rhiFactory.cpp` 中 Metal/Vulkan 直接返回失败：

[ rhiFactory.cpp ](C:/Users/xx/Documents/Cpp/CAD/Renderx/src/rhi/rhiFactory.cpp:51)

2D 和 3D 视口又直接继承 `QOpenGLWidget`，并固定创建 OpenGL Runtime：

[ RenderWidget.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderWidget.cpp:82)

[ RenderWidget.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderWidget.cpp:168)

## 重构目标

将 Qt Widget、窗口 Surface、渲染器、图形后端分离：

```text
Qt Viewport
    │
    ▼
IViewportSurface
    │
    ▼
ViewportRenderer
    │
    ▼
RenderX Runtime / Session
    │
    ├── OpenGLDevice
    ├── VulkanDevice
    └── MetalDevice
```

Qt 层不能再直接持有 OpenGL Runtime。

## 建议新增接口

新建：

```text
RenderBridge/
    Include/RenderBridge/GraphicsBackend.h
    Include/RenderBridge/NativeSurface.h
    Include/RenderBridge/IViewportRenderer.h
    Include/RenderBridge/RenderSessionHost.h
```

### 后端枚举

```cpp
#pragma once

#include <cstdint>

namespace RenderBridge
{
    enum class GraphicsBackend : uint8_t
    {
        Auto = 0,
        OpenGL,
        Vulkan,
        Metal,
        Null
    };

    struct BackendCapabilities
    {
        GraphicsBackend backend = GraphicsBackend::Null;

        uint8_t available = 0;
        uint8_t supportsCompute = 0;
        uint8_t supportsOffscreen = 0;
        uint8_t supportsPicking = 0;
        uint8_t supports3DCulling = 0;

        uint32_t maxTextureSize = 0;
        uint32_t maxUniformBufferSize = 0;
    };
}
```

### Native Surface 描述

不要用一个 `void* windowHandle` 含义模糊地传所有平台对象。

```cpp
#pragma once

#include <cstdint>

namespace RenderBridge
{
    enum class NativeSurfaceKind : uint8_t
    {
        None = 0,
        ForeignOpenGLContext,
        Win32Window,
        VulkanSurface,
        MetalLayer
    };

    struct NativeSurfaceDesc
    {
        NativeSurfaceKind kind = NativeSurfaceKind::None;

        void* handleA = nullptr;
        void* handleB = nullptr;

        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t highDpi = 0;
    };
}
```

以后：

```text
OpenGL：
ForeignOpenGLContext + QOpenGLContext

Vulkan：
Win32Window / XcbWindow / WaylandSurface

Metal：
CAMetalLayer
```

它们不能全部通过 `ForeignGlContext` 伪装。

### 后端无关的视口渲染器

```cpp
class IViewportRenderer
{
public:
    virtual ~IViewportRenderer() = default;

    virtual bool initialize(
        RenderBridge::GraphicsBackend backend,
        const RenderBridge::NativeSurfaceDesc& surface) = 0;

    virtual void shutdown() = 0;

    virtual bool resize(uint32_t width, uint32_t height) = 0;

    virtual void render(const RenderFrame& frame) = 0;

    virtual RenderBridge::BackendCapabilities capabilities() const = 0;
};
```

`RenderFrame` 不应包含 Qt 类型，也不应包含 `QPainter`。

## RenderSessionHost

将 2D 和 3D 中重复的 Runtime/Surface/Session 生命周期抽出来：

```cpp
class RenderSessionHost
{
public:
    bool create(
        Render::RT::Backend backend,
        const Render::RT::RuntimeDesc& runtimeDesc,
        const Render::RT::SurfaceDesc& surfaceDesc);

    void destroy();

    bool resize(uint32_t width, uint32_t height);

    bool beginFrame();
    bool endFrame();

    Render::RT::RuntimeHandle runtime() const;
    Render::RT::SessionHandle session() const;
    Render::RT::SurfaceHandle surface() const;

private:
    Render::RT::RuntimeHandle m_runtime{};
    Render::RT::SurfaceHandle m_surface{};
    Render::RT::SessionHandle m_session{};
};
```

之后：

```cpp
class RenderWidget2D : public QWidget
{
    std::unique_ptr<RenderSessionHost> m_session;
};

class RenderWidget3D : public QWidget
{
    std::unique_ptr<RenderSessionHost> m_session;
};
```

而不是两个 Widget 各自重复创建和销毁 RenderX 对象。

## 实施顺序

1. 先保留现有 OpenGL。
2. 将 `RenderWidget` 中 Runtime 生命周期抽到 `RenderSessionHost`。
3. 将 `QOpenGLWidget` 依赖限制在 OpenGL Adapter。
4. 实现一个 Vulkan 或 Metal 最小后端。
5. 用同一份 `RenderFrame` 验证两个后端。
6. 最后再开放 Auto 选择。

不建议同时开始 Vulkan 和 Metal。应根据主要平台选择一个第二后端：

```text
Windows/Linux 优先 Vulkan
macOS 优先 Metal
```

---

# 2. 3D 存在两套文档模型

## 当前问题

当前同时存在：

```text
SceneDocument3D
SceneDocument3DAdapter
```

`UiWorkbench` 同时创建两个对象：

[ UiWorkbench.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:2166)

但真正传给 3D viewport 的仍是：

```cpp
viewport->setSceneDocument(
    m_serviceOwner->sceneDocumentAdapter.get());
```

[ UiWorkbench.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/UiWorkbench.cpp:2253)

此外，`SceneDocument3D::removeEntity()` 当前只是空实现。

## 重构目标

只保留一个正式文档对象：

```text
SceneDocument3D
    ├── SceneManager3D
    ├── SceneEditService3D
    ├── SelectionModel
    └── SceneTreeModel
```

`SceneNode` 不应成为第二个文档，只能是 UI 树模型。

## 建议结构

```cpp
class SceneDocument3D final : public UI::ISceneDocument
{
public:
    Eg::SceneManager3D& scene();
    SceneEditService3D& editService();
    SelectionModel& selection();
    SceneTreeModel& treeModel();

    bool removeEntity(Eg::EntityId id);
    void clear();

private:
    std::unique_ptr<Eg::SceneManager3D> m_scene;
    std::unique_ptr<SceneEditService3D> m_editService;
    std::unique_ptr<SelectionModel> m_selection;
    std::unique_ptr<SceneTreeModel> m_treeModel;
};
```

`SceneDocument3DAdapter` 的职责拆分为：

```text
SceneDocument3D       文档和编辑入口
SceneTreeModel        UI 树结构
SelectionModel        选择状态
SceneEditService3D    命令、撤销、实际修改
```

## SceneTreeModel

```cpp
class SceneTreeModel
{
public:
    void rebuild(const Eg::SceneManager3D& scene);

    void onEntityAdded(Eg::EntityId id);
    void onEntityRemoved(Eg::EntityId id);
    void onEntityRenamed(Eg::EntityId id);

    std::vector<Eg::EntityId> rootEntities() const;
};
```

`SceneNode::selected()` 不应直接读取 Engine 内部选择状态：

```cpp
bool SceneNode::selected() const
{
    return m_selectionModel->contains(m_engineEntityId);
}
```

而不是：

```cpp
return m_engineScene->findMeshById(id)->selected();
```

## 3D 删除操作

当前 `SceneDocument3D::removeEntity()` 不能继续空实现。

推荐：

```cpp
bool SceneDocument3D::removeEntity(Eg::EntityId id)
{
    if (!m_editService)
    {
        return false;
    }

    return m_editService->deleteEntity(id);
}
```

并在 `SceneEditService3D` 内部完成：

```text
检查实体
创建 Undo Command
SceneManager3D::removeEntity
发布 SceneChangeSet
刷新 SelectionModel
刷新 SceneTreeModel
```

`clear()` 也必须经过编辑服务：

```cpp
void SceneDocument3D::clear()
{
    if (m_editService)
    {
        m_editService->clearScene();
    }
}
```

不能直接绕过 Undo 调用：

```cpp
m_sceneManager->clearScene();
```

## 如果 3D 是派生预览

如果 3D 只是浮雕、材料、刀路或加工模拟结果，建议明确命名：

```text
DesignDocument
ManufacturingPreviewDocument
```

3D 预览由 2D 设计数据和算法结果生成，不能与设计文档并列成为第二个可编辑真源。

---

# 3. 2D 全量和增量有两套离散化代码

## 当前问题

全量路径：

```text
SceneManager::gatherGeometry
    → RenderSceneBuilder
```

增量路径：

```text
EntityToVertices
    → IncrementalVertexSink
```

两个地方都处理：

- Polyline；
- Circle；
- Arc；
- Ellipse；
- Triangle；
- LineLoop。

代码已经明确要求两者公式保持一致：

[ EntityToVertices.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:140)

## 重构目标

只保留一套：

```text
Engine Geometry Emitter
        ↓
Tessellator2D
        ↓
RenderGeometry
        ↓
RenderSceneBuilder
```

## 建议新增 Tessellator2D

```cpp
namespace RenderGeometry
{
    struct TessellationOptions
    {
        double tolerance = 0.01;
        uint32_t maxSegments = 4096;
        double cameraScale = 1.0;
    };

    struct VertexP3C3
    {
        float px, py, pz;
        float cr, cg, cb;
    };

    struct TessellatedPiece
    {
        Render::PrimitiveType topology{};
        std::vector<VertexP3C3> vertices;
        Render::BBox2d bounds;
    };

    class Tessellator2D
    {
    public:
        explicit Tessellator2D(TessellationOptions options);

        void polyline(
            const Ut::Vec2d* points,
            size_t count,
            bool closed,
            const Ut::Color& color,
            TessellatedPiece& output);

        void circle(
            const Ut::Vec2d& center,
            double radius,
            const Ut::Color& color,
            TessellatedPiece& output);

        void arc(
            const Ut::Vec2d& center,
            double radius,
            double startAngle,
            double endAngle,
            const Ut::Color& color,
            TessellatedPiece& output);

        void ellipse(
            const Ut::Vec2d& center,
            double radiusX,
            double radiusY,
            double rotation,
            double startAngle,
            double endAngle,
            bool fullEllipse,
            const Ut::Color& color,
            TessellatedPiece& output);
    };
}
```

## 让全量和增量共用同一个 Sink

```cpp
class TessellatingSink final : public Eg::ISceneGeometrySink
{
public:
    TessellatingSink(
        RenderGeometry::Tessellator2D& tessellator,
        RenderGeometry::IGeometryOutput& output);

    void setCurrentEntityId(uint64_t id) override;

    void emitPolyline(...) override;
    void emitCircle(...) override;
    void emitArc(...) override;
    void emitEllipse(...) override;

private:
    RenderGeometry::Tessellator2D& m_tessellator;
    RenderGeometry::IGeometryOutput& m_output;
    uint64_t m_currentEntityId = 0;
};
```

全量：

```cpp
void RenderSceneBuilder::rebuild(const Eg::ISceneDataSource& source)
{
    clear();

    Tessellator2D tessellator(currentOptions());
    RenderSceneOutput output(*this, m_currentEntityId);

    TessellatingSink sink(tessellator, output);
    source.gatherGeometry(sink);
}
```

增量：

```cpp
bool entityToVertices(
    const Eg::SyEntity* entity,
    std::vector<Render::VertexP3C3>& output,
    Render::PrimitiveType& type,
    const TessellationOptions& options)
{
    Tessellator2D tessellator(options);
    EntityVertexOutput vertexOutput(output, type);

    TessellatingSink sink(tessellator, vertexOutput);

    return Eg::emitEntityGeometry(*entity, sink);
}
```

这样后续修改圆弧细分算法，只改一个地方。

---

# 4. EntityToVertices 的缓存存在数据竞争和生命周期问题

— ✅ 已完成（2026-09-13）：缓存已整块删除，不是「加锁修一修」。`entityToVertices`
退回纯函数，`eraseEntityVertexCache` / `clearEntityVertexCache` 两个外部失效 API 与
`SceneRefreshCoordinator` 里的三处调用一并去掉。判断「几何与上一轮是否相同」现在只由
`RenderSceneBuilder` 的段位哈希回答 —— 它属于持有几何块的那一层，自比对、不需要任何
外部失效调用，因此本节列出的数据竞争与生命周期问题在结构上不再存在。下述原分析保留
作为背景，其中「最低限度修复」已不再需要执行。

## 当前问题

缓存是进程级静态对象：

```cpp
static std::unordered_map<uint64_t, CachedVertexData> s_vertexCache;
static std::shared_mutex s_cacheMutex;
```

读取使用锁，但写入没有锁：

[ EntityToVertices.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:121)

删除和清空也没有锁：

[ EntityToVertices.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/EntityToVertices.cpp:363)

## 最低限度修复

```cpp
void storeCache(
    uint64_t entityId,
    uint64_t geometryHash,
    const std::vector<Render::VertexP3C3>& vertices,
    Render::PrimitiveType type)
{
    std::unique_lock lock(s_cacheMutex);

    auto& entry = s_vertexCache[entityId];
    entry.geometryHash = geometryHash;
    entry.vertices = vertices;
    entry.primType = type;
}
```

```cpp
void eraseEntityVertexCache(uint64_t entityId)
{
    std::unique_lock lock(s_cacheMutex);
    s_vertexCache.erase(entityId);
}

void clearEntityVertexCache()
{
    std::unique_lock lock(s_cacheMutex);
    s_vertexCache.clear();
}
```

同时补充：

```cpp
#include <mutex>
```

## 推荐重构

不要使用静态缓存，改成文档或渲染桥接对象拥有：

```cpp
class SceneRenderCache
{
public:
    bool find(
        EntityId id,
        uint64_t geometryRevision,
        CachedVertexData& out) const;

    void put(
        EntityId id,
        uint64_t geometryRevision,
        CachedVertexData value);

    void erase(EntityId id);
    void clear();

private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<EntityId, CachedVertexData> m_cache;
};
```

缓存键至少包含：

```text
DocumentId
EntityId
GeometryRevision
TessellationRevision
```

```cpp
struct CacheKey
{
    uint64_t documentId;
    uint64_t entityId;
    uint64_t geometryRevision;
    uint64_t tessellationRevision;

    bool operator==(const CacheKey&) const = default;
};
```

## 不建议使用 hash 作为正确性依据

`contentHash` 只能作为性能优化，不能作为数据正确性的唯一依据。

推荐实体保存版本号：

```cpp
struct EntityRevision
{
    uint64_t geometry = 0;
    uint64_t style = 0;
    uint64_t visibility = 0;
};
```

判断：

```cpp
if (entry.geometryRevision == entity.geometryRevision())
{
    // 可以跳过几何重建
}
```

Hash 只作为额外校验。

---

# 5. 3D 没有视锥剔除

## 当前问题

`Mesh3DBuilder` 明确关闭剔除：

[ Mesh3DBuilder.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/Mesh3DBuilder.cpp:101)

原因是 RenderX 当前只有二维 AABB：

```text
minX, minY, maxX, maxY
```

## 重构目标

2D 和 3D 使用两个明确的 DrawList 能力。

### 方案一：新增 3D API

```cpp
struct RxAabb3
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

struct RxFrustum
{
    float planes[6][4];
};
```

新增：

```cpp
RxResult rxDrawListUpsert3D(
    RuntimeHandle runtime,
    DrawListHandle list,
    uint32_t slot,
    const DrawCommand* command,
    const RxAabb3* bounds);
```

```cpp
RxResult rxSessionSubmitDrawList3D(
    SessionHandle session,
    DrawListHandle list,
    const RxFrustum* frustum);
```

### 方案二：统一 Bounds 结构

```cpp
enum class BoundsKind : uint8_t
{
    None = 0,
    Aabb2,
    Aabb3
};

struct Bounds
{
    BoundsKind kind;
    uint8_t reserved[3];
    float values[6];
};
```

这种方式可以减少函数数量，但 ABI 处理要更严格。

## Mesh3DBuilder 修改

```cpp
struct Entry
{
    uint64_t blockId = 0;
    uint32_t slot = 0;

    RxAabb3 bounds{};
    uint8_t boundsValid = 0;

    uint64_t geometryRevision = 0;
};
```

```cpp
void Mesh3DBuilder::writeCommand(uint64_t id, Entry& entry)
{
    Render::RT::DrawCommand command{};

    command.vertexBuffer = entry.buffer;
    command.vertexOffset = entry.byteOffset;
    command.vertexCount = entry.vertexCount;

    rxDrawListUpsert3D(
        m_runtime,
        m_drawList,
        entry.slot,
        &command,
        entry.boundsValid ? &entry.bounds : nullptr);
}
```

## 推荐实施顺序

先实现 CPU 视锥剔除：

```text
Camera Frustum
    → Entity AABB
    → visible list
    → DrawList submit
```

后续再增加 GPU indirect culling。

不要一开始就使用 Compute Shader 剔除。CPU 版本更容易验证正确性。

---

# 6. 场景数据和渲染数据之间缺少快照层

## 当前问题

`SceneRefreshCoordinator` 通过：

```cpp
SceneManager::findEntityById()
```

拿到可变实体指针，再转换为渲染数据。

`SceneManager` 的注释也要求：

```text
只能在没有并发写时读取
```

[ SceneManager.h ](C:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Core/SceneManager.h:76)

这会产生两个风险：

- UI 修改实体时，后台渲染线程读取到中间状态；
- 多个 Viewport 使用共享 dirty 集合时互相清理状态。

## 建议引入 SceneChangeSet

```cpp
enum class SceneChangeKind : uint8_t
{
    Added,
    Removed,
    GeometryChanged,
    StyleChanged,
    VisibilityChanged,
    LayerChanged,
    SelectionChanged,
    StructureChanged
};

struct SceneChange
{
    EntityId entityId = 0;
    SceneChangeKind kind{};
    uint64_t documentRevision = 0;
    uint64_t geometryRevision = 0;
    uint64_t styleRevision = 0;
};

struct SceneChangeSet
{
    uint64_t fromRevision = 0;
    uint64_t toRevision = 0;
    std::vector<SceneChange> changes;
};
```

## SceneManager 不再暴露 dirty 集合

目前：

```cpp
std::unordered_set<EntityId> dirtyEntities() const;
std::unordered_set<EntityId> deletedEntityIds() const;
void markClean();
```

建议改成：

```cpp
class ISceneChangeStream
{
public:
    using Cursor = uint64_t;

    virtual Cursor currentRevision() const = 0;

    virtual bool readChanges(
        Cursor from,
        SceneChangeSet& output) const = 0;
};
```

每个渲染视口拥有自己的 cursor：

```cpp
class SceneRenderBridge
{
    uint64_t m_lastRevision = 0;

    void synchronize()
    {
        SceneChangeSet changes;

        if (!m_scene->readChanges(m_lastRevision, changes))
        {
            rebuildAll();
            m_lastRevision = m_scene->currentRevision();
            return;
        }

        applyChanges(changes);
        m_lastRevision = changes.toRevision;
    }
};
```

这样不会出现：

```text
Viewport A 调用 markClean()
Viewport B 丢失自己的 dirty 信息
```

## Render Snapshot

对于真正的多线程渲染，建议增加：

```cpp
struct RenderEntitySnapshot
{
    EntityId id = 0;
    EntityType type{};
    uint64_t geometryRevision = 0;
    uint64_t styleRevision = 0;

    Render::BBox2d bounds2D;
    Render::BBox3f bounds3D;

    std::shared_ptr<const GeometryData> geometry;
    RenderStyle style;
    uint8_t visible = 1;
};

class SceneSnapshot
{
public:
    uint64_t revision() const;

    const RenderEntitySnapshot* find(EntityId id) const;
};
```

渲染层只读取：

```cpp
std::shared_ptr<const SceneSnapshot>
```

而不是直接读取正在编辑的实体对象。

---

# 7. SceneRenderContract 抽象过宽

## 当前问题

当前 `ISceneGeometrySink` 同时包含：

- 2D 折线；
- 圆；
- 圆弧；
- 椭圆；
- 文字；
- 图片；
- 2D 三角形；
- 3D 三角网格；
- 3D 包围盒。

[ SceneRenderContract.h ](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/Scene/SceneRenderContract.h:28)

结果是：

- 3D Builder 必须实现大量 2D 空函数；
- 2D Builder 必须实现 3D 空函数；
- 文本、图片与几何资源生命周期完全不同，却放在一个接口中。

## 推荐拆分

```cpp
class IScene2DGeometrySink
{
public:
    virtual ~IScene2DGeometrySink() = default;

    virtual void setCurrentEntityId(EntityId) = 0;
    virtual void emitPolyline(...) = 0;
    virtual void emitCircle(...) = 0;
    virtual void emitArc(...) = 0;
    virtual void emitEllipse(...) = 0;
    virtual void emitTriangles(...) = 0;
};
```

```cpp
class IScene3DGeometrySink
{
public:
    virtual ~IScene3DGeometrySink() = default;

    virtual void setCurrentEntityId(EntityId) = 0;
    virtual void emitTriangleSoup(...) = 0;
    virtual void emitBBox(...) = 0;
};
```

```cpp
class ITextSink
{
public:
    virtual ~ITextSink() = default;

    virtual void emitText(const TextPrimitive&) = 0;
};
```

```cpp
class IImageSink
{
public:
    virtual ~IImageSink() = default;

    virtual void emitImage(const ImagePrimitive&) = 0;
};
```

### 数据源也拆分

```cpp
class ISceneDataSource2D
{
public:
    virtual ~ISceneDataSource2D() = default;

    virtual void gatherGeometry(IScene2DGeometrySink&) const = 0;
    virtual void gatherText(ITextSink&) const = 0;
    virtual void gatherImages(IImageSink&) const = 0;
};
```

```cpp
class ISceneDataSource3D
{
public:
    virtual ~ISceneDataSource3D() = default;

    virtual void gatherGeometry(IScene3DGeometrySink&) const = 0;
};
```

这样不需要再写很多空实现。

---

# 8. C++ ABI 和跨 DLL 接口设计不统一

## 当前问题

`renderx.h` 的方向较好，但它仍然是 C++ 头文件，不是纯 C ABI。

`ISceneManager` 的注释也明确承认 STL 在虚函数中存在 ABI 风险：

[ ISceneManager.h ](C:/Users/xx/Documents/Cpp/CAD/Engine/Common/Include/Engine/ISceneManager.h:21)

`SceneDocumentBase` 又使用了 `const char*`，并通过默认实现隐藏未实现功能：

[ SceneDocumentBase.h ](C:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/SceneDocumentBase.h:15)

## 建议分成两种接口

### 进程内部 C++ 接口

允许：

```cpp
std::string
std::vector
std::function
std::shared_ptr
QObject
```

但必须规定：

```text
只在同一编译器、同一 CRT、同一进程内部使用
```

例如：

```cpp
namespace Internal
{
    class ISceneManager
    {
    public:
        virtual ~ISceneManager() = default;
        virtual std::vector<EntityId> entities() const = 0;
    };
}
```

### DLL/插件 C ABI

只使用：

```text
uint8_t
uint32_t
uint64_t
float
double
const void*
函数指针
显式内存所有权
```

例如：

```c
typedef uint64_t rx_entity_id;

typedef struct rx_vec3
{
    float x;
    float y;
    float z;
} rx_vec3;

typedef struct rx_toolpath_line
{
    const float* points;
    uint32_t point_count;
    float color[4];
    uint8_t travel;
    uint8_t reserved[3];
} rx_toolpath_line;
```

当前 `RenderToolpathOverlayLine::travel` 是 `bool`，如果声称跨 DLL 安全，应改成 `uint8_t`。

## SceneDocumentBase 建议

不要用字符串 ID 作为核心接口：

```cpp
virtual bool removeEntity(const char* id) = 0;
```

改为：

```cpp
using EntityId = uint64_t;

class ISceneDocument
{
public:
    virtual ~ISceneDocument() = default;

    virtual bool removeEntity(EntityId id) = 0;
    virtual void clear() = 0;

    virtual bool isModified() const = 0;
};
```

字符串只用于 UI 显示和文件格式。

## 默认实现应减少

当前：

```cpp
virtual void setDocumentName(const char*) {}
virtual bool isModified() const { return false; }
```

容易导致某个实现忘记实现却没有报错。

建议：

- 真正必需的接口设为纯虚；
- 可选能力单独放到 capability interface；
- 不用默认空实现掩盖功能缺失。

---

# 9. UICommon 模块依赖方向不合理

## 当前问题

`UICommon` 对外依赖：

- Engine2D；
- EnginePersistence；
- Log；
- SQLiteCpp；
- RenderX。

见：

[ UI/Common/CMakeLists.txt ](C:/Users/xx/Documents/Cpp/CAD/UI/Common/CMakeLists.txt:136)

这导致 UICommon 并不是真正的公共 UI 层。

另外 `RenderTypes.h` 直接 include：

```cpp
Engine2D/Interaction/SnapEngine.h
```

这样一个 UI 公共头又反向依赖 Engine2D。

## 建议的 CMake 目标

```text
Utility
EngineCommon
InteractionCommon
RenderContract
RenderBridge
Engine2D
Engine3D
UIFoundation
UICommon
UI2D
UI3D
Main
```

推荐依赖：

```text
UIFoundation
    → Qt + Utility + EngineCommon

UICommon
    → UIFoundation + RenderContract

RenderContract
    → Utility

RenderBridge
    → RenderContract + RenderX

UI2D
    → UICommon + Engine2D + RenderBridge

UI3D
    → UICommon + Engine3D + RenderBridge
```

## CMake 修改方向

`UI/Common/CMakeLists.txt`：

```cmake
target_link_libraries(UICommon
    PUBLIC
        Utility
        EngineCommon
        UIFoundation
)
```

删除：

```cmake
Engine2D
EnginePersistence
RenderX
SQLiteCpp
```

`UI/2D/CMakeLists.txt`：

```cmake
target_link_libraries(UI2D
    PUBLIC
        UICommon
        Engine2D
        RenderBridge
)
```

`UI/3D/CMakeLists.txt`：

```cmake
target_link_libraries(UI3D
    PUBLIC
        UICommon
        Engine3D
        RenderBridge
)
```

RenderX 尽量只在 `RenderBridge` 的实现层出现。

## SnapFlag

将 `SnapFlag` 移动到：

```text
Engine/Common/Interaction/SnapTypes.h
```

或：

```text
InteractionCommon
```

不要让 UICommon 为了 SnapFlag 依赖完整的 Engine2D。

---

# 10. Engine3D 依赖 Engine2D

## 当前问题

`Engine3D/CMakeLists.txt` 私有链接 `Engine2D`：

[ Engine/3D/CMakeLists.txt ](C:/Users/xx/Documents/Cpp/CAD/Engine/3D/CMakeLists.txt:76)

这会造成：

```text
Engine3D → Engine2D
```

如果以后 2D 和 3D 使用不同算法或独立加载，依赖方向会限制演进。

## 建议

将真正共享的类型移到 `EngineCommon`：

```text
EngineCommon/
    EntityId
    EntityType
    GeometryPrimitive
    SceneChange
    SelectionTypes
    LayerId
    GroupId
```

如果 Engine3D 使用了 2D 的数学、颜色或公共几何类型，应迁移到：

```text
Utility
EngineCommon
GeometryCommon
```

Engine3D 的 CMake 应改为：

```cmake
target_link_libraries(Engine3D
    PUBLIC
        Utility
        EngineCommon
        Log
    PRIVATE
        Boost::boost
)
```

如果文件解析需要同时访问 Engine2D 和 Engine3D，应由 FileIO 依赖二者：

```text
FileIO → Engine2D
FileIO → Engine3D
```

而不是：

```text
Engine3D → Engine2D
```

---

# 11. 2D/3D 操作总线重复

## 当前问题

2D 和 3D 各自拥有：

```text
OperationBus
OperationRegistry
CommandCatalog
OperationRouting
```

虽然当前已有 `OperationBusBase`，但上层仍然存在两套命令系统。

## 重构目标

共享命令内核，2D/3D 只提供扩展命令。

## 公共命令定义

```cpp
using CommandId = uint32_t;

enum class CommandScope : uint8_t
{
    Global = 0,
    Document,
    Viewport,
    Selection
};

struct CommandDescriptor
{
    CommandId id = 0;
    const char* name = nullptr;
    CommandScope scope = CommandScope::Global;

    uint8_t checkable = 0;
    uint8_t visible = 1;
    uint8_t enabled = 1;
};
```

```cpp
struct CommandContext
{
    DocumentSession* document = nullptr;
    ViewportContext* viewport = nullptr;
    SelectionModel* selection = nullptr;
};
```

```cpp
class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual CommandId id() const = 0;
    virtual bool canExecute(const CommandContext&) const = 0;
    virtual CommandResult execute(const CommandContext&) = 0;
};
```

## 2D/3D 注册

```cpp
class CommandRegistry
{
public:
    void registerCommand(std::unique_ptr<ICommand>);
    ICommand* find(CommandId id) const;
};
```

2D：

```cpp
registry.registerCommand(std::make_unique<DrawLineCommand>());
registry.registerCommand(std::make_unique<DrawCircleCommand>());
```

3D：

```cpp
registry.registerCommand(std::make_unique<ViewFrontCommand>());
registry.registerCommand(std::make_unique<SplitByPickPlaneCommand>());
```

UI 配置文件只引用：

```json
{
    "command": "view.fit",
    "text": "Fit View",
    "shortcut": "F"
}
```

而不直接引用 C++ 类。

---

# 12. Selection 状态存在多个真源

## 当前问题

选择状态分散在：

```text
SceneManager
SelectionManager
SceneDocument
SelectionSet
RenderWidget
SceneNode
```

这样很容易出现：

```text
Engine 已取消选择
UI 树仍显示选中
Renderer 仍绘制高亮
```

## 推荐职责划分

```text
SelectionModel
    唯一的应用层选择真源

Engine
    接收选择查询结果或选中 ID

RenderBridge
    消费 SelectionSnapshot

SceneTreeModel
    观察 SelectionModel
```

## SelectionModel

```cpp
class SelectionModel
{
public:
    using ChangeCallback = std::function<void()>;

    bool contains(EntityId id) const;

    void setSelected(EntityId id, bool selected);
    void replace(const std::vector<EntityId>& ids);
    void clear();

    std::vector<EntityId> ids() const;

    uint64_t revision() const;
};
```

渲染层只接收：

```cpp
struct SelectionSnapshot
{
    uint64_t revision = 0;
    std::vector<EntityId> selectedIds;
};
```

`SceneNode`：

```cpp
bool SceneNode::selected() const
{
    return m_selectionModel &&
           m_selectionModel->contains(m_engineEntityId);
}
```

不再直接读取 `SceneManager3D`。

---

# 13. SceneRefreshCoordinator 职责过多

## 当前问题

目前它同时负责：

- 读取场景 dirty；
- 读取实体；
- 调用几何转换；
- 管理缓存；
- 上传 GPU；
- 管理图片；
- 管理文字；
- 管理删除；
- 触发 Qt update；
- 并行处理。

相关代码集中在：

[ SceneRefreshCoordinator.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/SceneRefreshCoordinator.cpp:676)

## 建议拆成四个类

### SceneChangeTracker

```cpp
class SceneChangeTracker
{
public:
    SceneChangeSet collect();
};
```

### SceneRenderBridge

```cpp
class SceneRenderBridge
{
public:
    void apply(const SceneChangeSet& changes);

private:
    void rebuildEntity(EntityId id);
    void removeEntity(EntityId id);
    void updateStyle(EntityId id);
};
```

### RenderUploadQueue

```cpp
class RenderUploadQueue
{
public:
    void enqueue(RenderUploadCommand command);

    void flush(RenderSessionHost& session);
};
```

### ViewportRefreshScheduler

```cpp
class ViewportRefreshScheduler
{
public:
    void requestRepaint();
    void requestSceneUpdate();
    void requestFullRebuild();

private:
    QTimer* m_timer = nullptr;
};
```

最终：

```text
SceneManager
    → SceneChangeTracker
    → SceneRenderBridge
    → RenderUploadQueue
    → RenderWidget
```

Qt Timer 只负责调度，不应负责场景转换和 GPU 资源策略。

---

# 14. GPU 资源操作和 Qt UI 线程耦合

## 当前问题

当前代码通过 `makeCurrent()` 在场景刷新和实体上传过程中直接操作 OpenGL 资源。

这在 QOpenGLWidget 模式下可能可以运行，但会造成：

- 渲染线程无法独立；
- 后期 Vulkan/Metal 无法照搬；
- 后台算法线程不能安全提交；
- 资源释放时容易出现上下文生命周期问题。

## 建议使用上传队列

```cpp
struct RenderUploadCommand
{
    enum class Type : uint8_t
    {
        CreateGeometry,
        UpdateGeometry,
        RemoveGeometry,
        UpdateMaterial,
        UpdateOverlay
    };

    Type type{};
    EntityId entityId = 0;

    std::shared_ptr<const RenderGeometry> geometry;
    RenderStyle style;
};
```

后台线程只生成命令：

```cpp
m_uploadQueue.enqueue({
    RenderUploadCommand::Type::UpdateGeometry,
    id,
    geometry,
    style
});
```

渲染帧开始时统一执行：

```cpp
void ViewportRenderer::renderFrame()
{
    m_uploadQueue.flush(*m_session);

    m_session->beginFrame();
    submitScene();
    submitOverlay();
    m_session->endFrame();
}
```

这样：

```text
后台线程：生成 CPU RenderData
渲染线程：操作 GPU
Qt 主线程：处理输入和界面
```

三者职责清晰。

---

# 15. 2D 百万级图元不能长期采用“每实体一个 GPU 块”

## 当前问题

当前 `RenderSceneBuilder` 的组织形式是：

```text
EntityId
    → 多个 Piece
        → DrawList Slot
        → GeometryStore Block
```

这适合编辑性，但当实体数量达到几十万或百万时，会有：

- 大量 `unordered_map` 节点；
- 大量 DrawList slot；
- 大量 GPU 小块；
- 内存碎片；
- 合批效果差；
- 更新时频繁释放和重新分配。

## 建议引入 Chunk

```cpp
struct RenderChunkKey
{
    uint32_t layerId = 0;
    int32_t tileX = 0;
    int32_t tileY = 0;
    uint32_t materialId = 0;

    bool operator==(const RenderChunkKey&) const = default;
};
```

```cpp
struct EntityRenderRange
{
    EntityId entityId = 0;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};
```

```cpp
struct RenderChunk
{
    RenderChunkKey key;

    Render::RT::GeometryBlock geometry;
    Render::RT::DrawCommand command;

    std::vector<EntityRenderRange> ranges;

    Render::BBox2d bounds;
    uint64_t revision = 0;
};
```

场景结构：

```text
Layer
 └── Tile
      └── Material Batch
           └── RenderChunk
                └── EntityRenderRange
```

编辑单个实体时：

```text
找到 EntityRenderRange
    → 标记所在 Chunk 脏
    → 只重建该 Chunk
```

## GeometryStore 更新优化

当前 `upsertEntity()` 倾向于释放旧 Piece 后重新分配。

可以增加：

```cpp
bool updateInPlace(
    GeometryBlock block,
    const void* data,
    uint32_t bytes);
```

逻辑：

```cpp
if (oldBlock.capacity >= newBytes)
{
    rxGeometryWrite(oldBlock, data, newBytes);
}
else
{
    allocateNewBlock();
    freeOldBlock();
}
```

这样拖动或修改同等规模图元时，不会频繁造成碎片。

## 颜色数据优化

当前 2D 顶点包含颜色：

```cpp
struct VertexP3C3
{
    float px, py, pz;
    float cr, cg, cb;
};
```

颜色变化会导致整个顶点流更新。

可以根据图层组织方式改成：

```cpp
struct VertexP3
{
    float px, py, pz;
};
```

颜色放到：

```text
Material
DrawCommand
Instance Buffer
```

如果同一 Chunk 颜色相同，颜色完全不应存在每个顶点中。

---

# 16. 3D Mesh3DBuilder 应提取公共资源管理代码

## 当前问题

`RenderSceneBuilder` 和 `Mesh3DBuilder` 有大量同构逻辑：

- Runtime 生命周期；
- GeometryStore；
- DrawList；
- Slot 分配；
- Entity → Entry；
- 删除和回收；
- GeometryStore flush；
- DrawCommand 更新。

但目前两套代码各自实现。

## 建议提取内部公共类

```cpp
template <typename Entry>
class PersistentRenderScene
{
public:
    bool initialize(Render::RT::RuntimeHandle runtime);
    void shutdown();

    uint32_t acquireSlot();
    void releaseSlot(uint32_t slot);

    void removeSlot(uint32_t slot);

    Render::RT::GeometryStoreHandle geometryStore() const;
    Render::RT::DrawListHandle drawList() const;

protected:
    Render::RT::RuntimeHandle m_runtime{};
    Render::RT::GeometryStoreHandle m_store{};
    Render::RT::DrawListHandle m_drawList{};

    std::vector<uint32_t> m_freeSlots;
    uint32_t m_nextSlot = 0;
};
```

2D：

```cpp
class RenderSceneBuilder
    : public PersistentRenderScene<Render2DEntry>
{
};
```

3D：

```cpp
class Mesh3DBuilder
    : public PersistentRenderScene<Mesh3DEntry>
{
};
```

不要把 2D/3D 的几何处理也模板化到一起，只共享：

```text
资源生命周期
槽位管理
块管理
命令台账
```

2D 和 3D 的顶点格式、材质、剔除、拓扑仍然独立。

---

# 17. 3D 假抽象和占位接口

## 当前问题

`IRenderer3D` 包含：

```cpp
QPainter&
QString
std::function
SceneDocument3DAdapter*
isOpenGL()
```

[ IRenderer3D.h ](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Render3D/IRenderer3D.h:18)

但实际：

- `render(QPainter&)` 不使用 QPainter；
- `isOrbitMode()` 恒返回 true；
- `isOpenGL()` 恒返回 true；
- `setOrbitMode()` 没有真实逻辑。

## 建议删除旧接口

新的接口应该是：

```cpp
class IViewportRenderer
{
public:
    virtual ~IViewportRenderer() = default;

    virtual bool initialize(const ViewportCreateInfo&) = 0;
    virtual void shutdown() = 0;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void render(const RenderFrameView&) = 0;

    virtual RendererCapabilities capabilities() const = 0;
};
```

相机和交互不要塞到 Renderer：

```cpp
class CameraController3D;
class NavigationController3D;
class SelectionController3D;
```

职责：

```text
Renderer：绘制
CameraController：相机状态
InputRouter：输入路由
SelectionController：拾取与选择
```

---

# 18. 刀路覆盖、表面拾取、离屏渲染应改成真实能力接口

## 当前问题

当前以下功能为占位：

- `setToolpathOverlay()` 丢弃数据；
- `hasToolpathOverlay()` 恒 false；
- Mesh surface pick 不执行；
- `captureOffscreen()` 返回空图像。

[ RenderWidget3D.cpp ](C:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.cpp:529)

## 刀路覆盖层

不要让业务直接把刀路数组交给 `RenderWidget3D`。

新增：

```cpp
struct ToolpathSegment
{
    Ut::Vec3f start;
    Ut::Vec3f end;
    Ut::Color color;
    uint8_t travel = 0;
};

struct ToolpathOverlay
{
    uint64_t revision = 0;
    std::vector<ToolpathSegment> segments;
};
```

渲染层统一处理：

```cpp
class OverlayScene
{
public:
    void setToolpath(const ToolpathOverlay&);
    void clearToolpath();

    void submit(RenderFrame&);
};
```

## 表面拾取

当前 `SceneManager3D` 已经有：

```cpp
raycastHitFirst(...)
```

可以先实现 CPU 拾取：

```cpp
struct PickResult
{
    uint8_t hit = 0;
    EntityId entityId = 0;
    Ut::Vec3f position;
    Ut::Vec3f normal;
    uint32_t triangleIndex = 0;
};
```

```cpp
PickResult PickingService3D::pick(
    const Ray3f& ray,
    const SceneSnapshot3D& snapshot);
```

之后再升级 GPU ID Buffer。

不要让 `RenderWidget3D` 保存业务回调：

```cpp
setMeshSurfacePickHandler(...)
```

建议由 `PickingService3D` 返回结果，Controller 决定如何处理。

## 离屏渲染

RenderX 已经有 RenderTarget/Texture 概念，应该让 RenderX 返回像素缓冲：

```cpp
struct RenderImage
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowPitch = 0;
    std::vector<uint8_t> rgba8;
};
```

```cpp
bool IViewportRenderer::renderOffscreen(
    const RenderFrameView& frame,
    uint32_t width,
    uint32_t height,
    RenderImage& output);
```

Qt 层再转换：

```cpp
QImage toQImage(const RenderImage& image);
```

不要让底层 Renderer 直接返回 `QImage`。

---

# 19. RenderX RHI 设计较大，但实际后端太少

## 当前问题

当前 RHI 已经设计了：

- Buffer；
- Texture；
- Sampler；
- BindGroup；
- Graphics Pipeline；
- Compute Pipeline；
- CommandList；
- Indirect Draw；
- Readback；
- Surface；
- Shader Language。

但真实实现仍只有 OpenGL。

这不是架构错误，但存在“接口先行过度设计”的风险。

## 建议采用最小能力集

先定义所有后端必须支持的核心子集：

```text
必需：
- Vertex Buffer
- Index Buffer
- Uniform/Push Constants
- Texture
- Sampler
- Graphics Pipeline
- Depth Test
- Alpha Blend
- Render Target
- Readback

可选：
- Compute
- Indirect Draw
- Multi Draw Indirect
- Storage Buffer
- Async Readback
```

能力查询：

```cpp
struct BackendCapabilities
{
    uint8_t graphics = 1;
    uint8_t textures = 1;
    uint8_t depth = 1;
    uint8_t blending = 1;

    uint8_t compute = 0;
    uint8_t indirectDraw = 0;
    uint8_t multiDrawIndirect = 0;
};
```

渲染器必须根据能力降级：

```cpp
if (caps.compute)
{
    gpuCulling();
}
else
{
    cpuCulling();
}
```

Null 后端不能把所有能力都报告为 true，否则会掩盖真实后端缺陷。

## Shader 方案

当前构建配置支持 `.spv`、`.metal`、`.metallib`，但实际 shader 列表主要是 GLSL：

[ Renderx/CMakeLists.txt ](C:/Users/xx/Documents/Cpp/CAD/Renderx/CMakeLists.txt:82)

建议明确采用一种方案：

### 方案 A：统一源代码，构建期转换

```text
shader source
    → SPIR-V
    → Vulkan
    → SPIRV-Cross → MSL
```

OpenGL 可使用 GLSL 或 SPIR-V 转换结果。

### 方案 B：每后端独立 Shader

```text
shader/
    common/
    opengl/
    vulkan/
    metal/
```

对于 CAD 的固定管线数量，方案 B 更简单，但维护文件更多。

不要只设计 shader loader，而不提供实际的 shader 编译和验证流程。

---

# 20. RenderX GeometryStore 增长语义需要统一

当前不同代码位置对 GeometryStore 扩容后 BufferHandle 是否稳定的注释存在不一致。

建议明确契约：

```cpp
struct GeometryBlock
{
    uint64_t blockId = 0;
    BufferHandle buffer = BufferHandle::Invalid;
    uint32_t byteOffset = 0;
    uint32_t byteSize = 0;
};
```

扩容后规定：

### 方案 A：BufferHandle 稳定

底层内部迁移数据，但句柄语义保持不变。

优点：

```text
DrawCommand 不需要刷新
```

### 方案 B：BufferHandle 失效

扩容后所有 Block 必须重新查询。

优点：

```text
底层实现简单
```

但必须提供：

```cpp
RxResult rxGeometryStoreRefreshBlock(
    RuntimeHandle runtime,
    GeometryStoreHandle store,
    uint64_t blockId,
    GeometryBlock* output);
```

无论选择哪种方案，`RenderSceneBuilder` 和 `Mesh3DBuilder` 必须使用同一契约，并增加测试：

```text
连续分配直到 GeometryStore 扩容
验证旧图元仍然正常绘制
验证所有 DrawCommand 指向有效 Buffer
```

---

# 21. 算法层与 UI/Renderer 解耦

## 当前问题

当前已经有算法服务，但算法、UI、3D 预览之间仍然存在直接联系，例如刀路预览直接调用 Widget 接口。

## 推荐结构

```text
AlgorithmInput
    ↓
IAlgorithmStrategy
    ↓
AlgorithmResult
    ↓
Application Command
    ↓
SceneChangeSet
    ↓
RenderBridge
```

## 算法接口

```cpp
struct AlgorithmInput
{
    std::shared_ptr<const DocumentSnapshot> document;
    std::vector<EntityId> selectedEntities;
    AlgorithmParameters parameters;
};
```

```cpp
struct AlgorithmResult
{
    bool success = false;

    std::vector<GeometryPatch> geometryPatches;
    std::shared_ptr<const ToolpathData> toolpath;
    std::shared_ptr<const PreviewMesh> previewMesh;

    std::vector<AlgorithmMessage> messages;
};
```

```cpp
class IAlgorithmStrategy
{
public:
    virtual ~IAlgorithmStrategy() = default;

    virtual const char* strategyId() const = 0;

    virtual AlgorithmResult run(
        const AlgorithmInput& input,
        CancellationToken cancellation,
        ProgressSink progress) = 0;
};
```

应用层：

```cpp
class AlgorithmApplicationService
{
public:
    AlgorithmResult execute(
        const std::string& strategyId,
        const AlgorithmParameters& parameters);
};
```

算法实现不能 include：

```cpp
QWidget
QDialog
RenderWidget
renderx.h
QPainter
```

## 策略注册

```cpp
class AlgorithmStrategyRegistry
{
public:
    void registerStrategy(std::unique_ptr<IAlgorithmStrategy>);
    IAlgorithmStrategy* find(std::string_view id) const;
};
```

以后可以添加：

```text
nesting.fast
nesting.quality
relief.cpu
relief.gpu
toolpath.contour
toolpath.hatch
```

而不修改 UI 主流程。

---

# 22. UI 定制应围绕 CommandDescriptor，而不是具体类

## 当前问题

当前 UI 配置化方向已经存在，但底层 UI 仍然大量依赖具体服务、具体文档和具体 Renderer。

## 建议 UI 配置只引用稳定标识

```json
{
    "menus": [
        {
            "id": "main.file",
            "items": [
                "document.new",
                "document.open",
                "document.save"
            ]
        }
    ],
    "toolbars": [
        {
            "id": "viewport.navigation",
            "items": [
                "view.fit",
                "view.front",
                "view.top"
            ]
        }
    ]
}
```

代码侧：

```cpp
struct ActionState
{
    bool visible = true;
    bool enabled = true;
    bool checked = false;
    std::string text;
    std::string icon;
};

class IActionModel
{
public:
    virtual ActionState state(CommandId id) const = 0;
    virtual void trigger(CommandId id) = 0;
};
```

UI 只依赖：

```cpp
IActionModel
```

不直接依赖：

```cpp
SceneManager3D
SceneEditService3D
RenderWidget3D
```

后端能力不支持时：

```cpp
if (!renderer.capabilities().supportsOffscreenCapture)
{
    action.setVisible(false);
}
```

不要把菜单显示出来，点击后才发现函数是空实现。

---

# 23. RenderTypes 应从 UICommon 中继续拆分

当前 `RenderTypes.h` 虽然已经不放在 RenderX 内部，但仍然混合了：

- 顶点格式；
- CAD Overlay；
- SnapFlag；
- UI 文本；
- 2D/3D 渲染类型；
- STL 容器。

建议拆成：

```text
RenderContract/
    VertexTypes.h
    PrimitiveTypes.h
    RenderStyle.h
    RenderFrame.h

EditorOverlay/
    SelectionOverlay.h
    SnapOverlay.h
    ToolPreview.h

TextRender/
    TextPrimitive.h
    TextLayout.h

ImageRender/
    ImagePrimitive.h
```

例如：

```cpp
// RenderContract/VertexTypes.h
struct VertexP3C3 { ... };

// EditorOverlay/SelectionOverlay.h
struct SelectionOutline { ... };

// TextRender/TextPrimitive.h
struct TextPrimitive { ... };
```

`RenderContract` 不应 include：

```cpp
Engine2D/Interaction/SnapEngine.h
```

`SnapFlag` 应放到更低层的 InteractionCommon。

---

# 24. OverlayState 的字段过多，应拆成 Layer

— ✅ 已完成（2026-09-13）：覆盖层已整体移入 `RenderBridge::OverlayScene`，并按要求拆成
独立层。落点与本文设想的两处差异，都是落地时才看清的约束：

- **层号顺序 = 提交顺序 = 叠放顺序**，写死在 `OverlayLayerId` 枚举里，不再靠一个共享的
  自增 `seq`（那个 `seq` 会随调用顺序漂移）。枚举按原有提交次序排列，因此叠放不变。
- 设成 `replaceLayer(id, shared_ptr<const OverlayLayer>)` 需要所有层共用一种载荷类型，
  但这九层的载荷结构互不相同（包围盒 / 四边形列表 / 带弧长的轮廓路径 / 标记组 / 形状+颜色），
  塞进一个通用 POD 只会变成一个 `void*` 袋子。因此改为**按层的强类型 setter**
  （`setSelectionBox` / `setSelectionOutlines` / `setSelectionHandles` / …）+ 统一的
  `clearLayer(id)` / `clear()`；「各工具只管理自己的图层」这一条由类型保证 ——
  写哪层只影响哪层，不再需要「只清 update 实际携带的组」那条防御性注释来兜。

另外，`OverlayState` 的 `snapType`（`Engine2D::SnapEngine::SnapFlag`）不再进入渲染侧：
形状与颜色的映射留在 UI2D 的 `ViewRenderCoordinator`，`OverlayScene` 只收中性的
`SnapMarkerShape` + `Render::Color`，渲染桥接层因此不依赖 Engine2D 的捕捉语义。

## 当前问题

`RenderOverlayUpdate` 拥有很多：

```cpp
hasPreviewPoints
hasControlLines
hasSelectionBox
hasSelectionHandles
hasSelectionOutlines
hasSnapIndicator
hasUiTexts
```

这种“一个结构体承载所有局部修改”的方式长期会越来越复杂。

## 推荐改成图层

```cpp
enum class OverlayLayerId : uint8_t
{
    ToolPreview,
    ControlLines,
    Selection,
    Snap,
    UiText,
    Toolpath
};
```

```cpp
class OverlayScene
{
public:
    void replaceLayer(
        OverlayLayerId id,
        std::shared_ptr<const OverlayLayer> layer);

    void clearLayer(OverlayLayerId id);

    void submit(RenderFrame& frame) const;
};
```

各工具只管理自己的图层：

```cpp
overlayScene.replaceLayer(
    OverlayLayerId::ToolPreview,
    buildLineToolPreview());
```

选择系统：

```cpp
overlayScene.replaceLayer(
    OverlayLayerId::Selection,
    buildSelectionOverlay());
```

这样不需要每次修改都理解整个 `RenderOverlayUpdate`。

---

# 25. 2D/3D 相机、输入和视口接口需要拆开

## 当前问题

当前 `IViewportHost`、`IRenderer3D`、`RenderWidget` 同时涉及：

- 输入事件；
- 相机；
- 渲染；
- 选择；
- 测量；
- OpenGL；
- UI 更新。

## 推荐分层

```text
ViewportWidget
    接收 Qt 事件

ViewportInputRouter
    把事件转成统一输入事件

NavigationController
    平移、旋转、缩放

SelectionController
    拾取和框选

CameraModel
    相机状态

ViewportRenderer
    只绘制
```

统一事件：

```cpp
struct PointerEvent
{
    enum class Type : uint8_t
    {
        Press,
        Release,
        Move,
        Wheel
    };

    Type type{};
    float x = 0;
    float y = 0;
    uint32_t buttons = 0;
    uint32_t modifiers = 0;
};
```

2D：

```cpp
class InputRouter2D
{
    void route(const PointerEvent&);
};
```

3D：

```cpp
class InputRouter3D
{
    void route(const PointerEvent&);
};
```

共享的是事件协议，不是让 2D/3D 共用一个巨大 Widget 接口。

---

# 26. 小型重复和代码清理

## `SceneDocument2D` 中重复设置脏标记

当前 `createLine()` 存在重复：

```cpp
if (added)
{
    m_isModified = true;
}
if (added)
{
    m_isModified = true;
}
```

[ SceneDocument2D.cpp ](C:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Documents/SceneDocument2D.cpp:101)

建议统一：

```cpp
if (!added)
{
    return {};
}

m_isModified = true;
return QString::number(added->id);
```

## 工厂名称

当前 `Renderer3DFactory` 的枚举只有：

```text
Compatible
None
```

但它实际不是后端工厂，只是 Qt 3D Widget 包装器工厂。

应改名为：

```text
ViewportRendererFactory
```

真正的后端工厂应位于：

```text
RenderBridge::BackendRendererFactory
```

## Stub 文件

Main 中存在多份 3D `IRenderer3D` Stub。建议统一：

```text
Testing/RenderStubs/
    NullViewportRenderer
    FakeRenderSession
    FakeSelectionRenderer
```

不要在多个模块复制同名接口。

---

# 27. 文档和代码需要建立一致性检查

当前文档中有部分内容与代码不一致，例如：

- README 描述的 CMake 版本与实际版本不一致；
- 文档说 3D 已经有统一主链，但代码中仍保留双文档；
- 文档说后端可扩展，但 Vulkan/Metal 仍未实现；
- 部分接口文档写成已支持，实际是占位。

建议在 README 增加真实状态表：

```markdown
| 功能 | 状态 |
|---|---|
| OpenGL | 已实现 |
| Null | 已实现 |
| Vulkan | 未实现 |
| Metal | 未实现 |
| 2D 增量更新 | 已实现 |
| 3D 视锥剔除 | 未实现 |
| 3D 刀路覆盖层 | 占位 |
| 3D 表面拾取 | CPU/占位 |
| 3D 离屏渲染 | 未实现 |
```

同时增加自动检查：

```text
Backend::Vulkan == available
    → 必须存在 Vulkan smoke test

supportsOffscreen == true
    → 必须有离屏截图测试

supportsMeshPicking == true
    → 必须有实际命中测试
```

---

# 推荐实施顺序

## 第一阶段：清理和止血

优先修改：

1. 合并 `SceneDocument3D` / `SceneDocument3DAdapter`。
2. 修复 `EntityToVertices` 缓存锁。
3. 合并 2D Tessellation。
4. 删除 3D Renderer 假接口。
5. 修复 `SceneDocument3D::removeEntity()` 和 `clear()`。
6. 修正 UICommon 和 Engine3D 依赖方向。
7. 明确当前后端能力状态。

## 第二阶段：建立 RenderBridge

新增：

```text
RenderContract
RenderBridge
RenderSessionHost
SceneChangeSet
SceneRenderCache
RenderUploadQueue
```

并将：

```text
RenderWidget
RenderWidget3D
SceneRefreshCoordinator
Mesh3DBuilder
```

逐步迁移到 RenderBridge。

## 第三阶段：实现统一场景更新

目标链路：

```text
SceneManager
    → SceneChangeSet
    → SceneSnapshot
    → RenderBridge
    → RenderUploadQueue
    → Renderer
```

移除：

```text
全局 dirty 集合
全局 vertex cache
直接读取实体指针
渲染层主动调用 SceneManager
```

## 第四阶段：实现真正后端切换

先完成：

```text
OpenGL + Null + 一个第二后端
```

验证：

```text
同一 RenderFrame
同一场景数据
同一选择结果
同一 2D/3D 视图
不同图形后端
```

## 第五阶段：大型场景优化

最后处理：

- Chunk；
- 空间剔除；
- 3D Frustum；
- GeometryStore 碎片整理；
- Indirect Draw；
- GPU Culling；
- 多视口共享资源。

---

# 最终建议

目前最值得立即做的不是继续增加 Vulkan/Metal 枚举，也不是继续增加 UI 接口，而是先完成这五项：

```text
1. 只保留一个 3D 文档模型
2. 只保留一套 2D Tessellation
3. 建立 RenderBridge 和 RenderSnapshot
4. 将 Qt/OpenGL 从通用 Renderer 接口中移除
5. 修复缓存并发和 3D 无剔除问题
```

完成后，项目才具备继续实现：

```text
OpenGL ⇄ Vulkan/Metal
2D ⇄ 3D
算法策略替换
百万级 2D 图元
UI 配置化
```

的稳定基础。