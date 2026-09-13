# Metal 后端实施设计

> 定位：为 RenderX 补齐第二个真实 GPU 后端，解锁 macOS 现代渲染能力
> （GL 4.1 无 compute / 无 indirect draw / 线宽恒 1px），并为 GPU-driven 剔除铺路。
>
> 前置结论：**本次不改公共 ABI**。`Backend::Metal` 与
> `NativeWindowKind::CocoaNsView` 早已在 `renderx.h` 中预留
> （`include/render/renderx.h` 的 `enum class Backend` / `NativeWindowKind`），
> RHI 层的能力位（`computeShaders` / `indirectDraw` / `ShaderLanguage::MetalLib`）
> 与实例化 divisor（`VertexBufferLayout::perInstance`）也已就位。

---

## 0. 现状盘点

| 能力 | 状态 | 位置 |
|------|------|------|
| RHI 抽象（descriptor set / pushConstant / barrier） | 已就绪 | `src/rhi/rhiCommandList.h` |
| compute pipeline + dispatch | GL 已实现 | `src/rhi/gl/glDevice.cpp`、`glCommandList.cpp` |
| indirect draw | GL 已实现 | `glCommandList.cpp` |
| 实例化 divisor | **GL 已实现** | `glCommandList.cpp`（调 `glVertexAttribDivisor`） |
| 资源句柄池（世代式，防悬垂） | 可复用 | `src/rhi/rhiResourcePool.h` |
| Shader 构建期嵌入（含 `.metal` / `.metallib`） | 已就绪 | `CMake/EmbedShaders.cmake` |
| 表面抽象（含 `CocoaNsView`） | 已就绪 | `src/rhi/rhiSurface.h` |
| **Metal 设备 / 表面 / 命令** | **未实现** | `rhiFactory.cpp` 明确报「Phase 7 尚未实现」 |
| **`.mm` 构建集成** | **缺失** | `Renderx/CMakeLists.txt` 的 GLOB 只收 `src/*.cpp` |

一句话：**抽象层和构建机制的准备工作已经做完，缺的是 Metal 后端本体与 `.mm` 的构建接入。**

---

## 1. 目标与非目标

### 1.1 目标

1. macOS 上提供与 GL 后端**等价**的绘制能力（2D 图元、3D 网格、文本、离屏渲染、读回）。
2. 在 macOS 上**补上 GL 4.1 做不到的能力**：compute shader、indirect draw（GPU 剔除的前提）。
3. 公共 ABI、GL 后端、Null 后端**零改动**。
4. `preferredBackend()` 在 macOS 上自动选中 Metal；Metal 不可用时明确失败，不静默回退。

### 1.2 非目标

- 不做 Vulkan（Phase 8，另议）。
- 不在 Windows/Linux 构建中引入 Metal 相关源文件与依赖。
- 不改动 GL 后端的任何行为。
- 不在本阶段接入 GPU-driven 剔除（属 M3 之后的独立一轮）。

---

## 2. 关键设计决策

### D1 Shader 策略：手写 MSL + 构建期 `xcrun metal`

现状：`src/shader/*.vert|.frag|.comp` 是 GLSL，构建期由 `EmbedShaders.cmake` 转为字节数组编入 DLL。

三个选项：

| 方案 | 结论 |
|------|------|
| glslang + SPIRV-Cross 自动 GLSL→MSL 转换 | ❌ 引入两个大型第三方依赖，构建链复杂化，且转换结果未必与手写等价 |
| 运行时 `newLibraryWithSource` 编译 `.metal` 源码 | ⚠️ 无构建期依赖，但 25 个 shader 首帧编译会带来秒级启动延迟 |
| **手写 MSL，构建期 `xcrun metal` → `.metallib` 嵌入** | ✅ 与现有「构建期嵌入、运行期零 IO」理念一致；`.metallib` 已被 `EmbedShaders.cmake` 支持 |

**决策：手写 MSL（`src/shader/metal/*.metal`），macOS 构建期编译为 `.metallib` 并嵌入。**

理由：本项目 GL 侧已经确立了「shader 不依赖运行目录布局」这条契约（`Docs/Mac渲染.md` 记录过 bundle 下路径推导失败导致视口全黑的故障）。运行时编译会把风险从构建期挪回运行期，与既有契约相悖。

代价：GLSL 与 MSL 双份维护。缓解方式是**把共享部分（`rx_push_constants.glsl`）以同样的思路在 MSL 侧建立一份对应声明**，并用测试锁定两侧字段布局一致。

### D2 设备与表面

```
MetalDevice (IGpuDevice)
  ├─ id<MTLDevice>            （一个进程一个）
  ├─ id<MTLCommandQueue>      （一个队列，按帧提交）
  └─ 全部共享资源（管线/纹理/缓冲/绑定组）

MetalSurface (ISurface)
  ├─ NSView*                  （来自 NativeWindow::handleA）
  ├─ CAMetalLayer             （由 Surface 创建并挂到 view 上）
  ├─ 3 帧 in flight 的 semaphore + drawable 管理
  └─ MetalCommandList         （录制到 MTLCommandBuffer）
```

与 GL 的关键差异：**GL 的 `ICommandList` 是「即时执行」的伪录制，Metal 是真录制**。因此 `beginFrame` 取 `MTLCommandBuffer`，`submitFrame` 提交，`present()` 呈现 drawable——三段式与 `rhiSession.cpp` 里已有的「提交与呈现分离」流程天然吻合。

### D3 资源映射

| RHI 概念 | GL 落地 | Metal 落地 |
|----------|---------|-----------|
| `BufferDesc.access = CpuToGpu` | `glBufferData` + 持久映射 | `MTLStorageModeShared` |
| `BufferDesc.access = GpuOnly` | `GL_STATIC_DRAW` | `MTLStorageModePrivate` |
| 顶点绑定 | `glVertexAttribPointer` + VAO | `setVertexBuffer:offset:atIndex:` + 管线里的 `MTLVertexDescriptor` |
| `perInstance` | `glVertexAttribDivisor(1)` | `MTLVertexDescriptor` 的 `stepFunction = PerInstance` |
| `BindGroup` | UBO binding point + 纹理单元 | `setBuffer/setTexture` 直接按 index 绑定（无需预解析名字） |
| `pushConstants` | 映射为一个内部 UBO | `setVertexBytes/setFragmentBytes`（≤4KB，远超 128 字节上限） |
| `RenderPass` | 按附件组合缓存 FBO | `MTLRenderPassDescriptor`（每次现构，开销低） |
| `drawIndexed` | `glDrawElementsBaseVertex` | `drawIndexedPrimitives:...baseVertex:` |
| `readTexture` | `glReadPixels`（左下原点→翻转） | `getBytes`（Metal 纹理原点与 CPU 图像一致，**无需翻转**） |

Metal 的绑定模型比 GL 简单：`(set, binding)` 可以直接映射为 buffer/texture 的 index，`BindingSlot::glName` 字段直接忽略。

### D4 管线状态

`GraphicsPipelineDesc` 到 `MTLRenderPipelineDescriptor` 的映射是直接的，但有两处必须显式处理：

1. **`FillMode::Wireframe`**：Metal 没有多边形线框模式，`Capabilities::wireframeFill` 为 **false**。现有 `Mesh3DWire` 管线依赖 `fillMode = Wireframe`，在 Metal 上必须改为**三角化线框**（应用层或 RT 层生成 LineList），或在能力位为 false 时明确报错。这是必须在 M4 处理的已知缺口。
2. **深度范围**：GL 裁剪空间 z ∈ [-1,1]，Metal 为 [0,1]。若宿主的投影矩阵按 GL 约定生成，Metal 上会出现深度错误。**这是最容易被忽略、且症状隐蔽的一条**（表现为远平面物体消失或 z-fighting）。处理方式见 §6。

### D5 命令录制与状态冗余消除

GL 后端在 `GlCommandList` 内做了绑定冗余消除（管线 / 顶点缓冲+偏移 / 纹理 / pushConstant memcmp）。Metal 侧保留同一套纪律，但收益来源不同：Metal 的 `setVertexBuffer` 等调用本身廉价，真正的成本在**编码器状态切换导致的 GPU 侧管线切换**。因此 M2 的冗余消除仍按「同 GL 的粒度」实现，便于两侧行为对齐与对比验证。

### D6 与宿主的集成

现有 2D 视口走 `ForeignGlContext`（Qt `QOpenGLWidget` 自建上下文）。切到 Metal 后：

- 宿主通过 `SurfaceDesc.window = { CocoaNsView, (NSView*)widget->winId() }` 传入原生视图。
- Metal 后端在 `createSurface` 时创建 `CAMetalLayer` 并挂到该 view 上。
- 宿主**不再**需要提供 GL 上下文。

影响面：`UI/2D` 与 `UI/3D` 的视口控件需要按平台分支创建 surface。这部分改动**不在本次范围内**，但需要在 M1 完成后同步给宿主侧，否则 Metal 后端只有测试能跑、实际 GUI 用不上。

---

## 3. 分阶段计划

每个阶段都以「可在 Mac 上独立验证」为界，避免一次性交付无法定位的问题。

### M1 设备与表面（可出图）

- `MetalDevice`：`MTLDevice`、`MTLCommandQueue`、`Capabilities` 查询
- `MetalSurface`：`CAMetalLayer`、3 帧 in flight、`acquireNextImage` / `present` / `resize`
- `MetalCommandList`：`beginRenderPass` / `endRenderPass` / `setViewport` / `setScissor`
- `rhiFactory.cpp`：`createDevice` 的 Metal 分支改为真实创建；`isBackendAvailable` 更新
- 构建集成：`.mm` 收进源列表、链接 Metal/QuartzCore/Foundation

**验收**：能在 Mac 上创建 Metal 设备 + 表面，清屏为指定颜色，并读回像素验证颜色值正确。

### M2 管线与 2D 绘制（能画图元）

- MSL 版 shader（从 `world_p3c3` / `screen_p3c4` / `point_*` 等开始，覆盖 2D 需要的子集）
- `createShader` / `createGraphicsPipeline`（映射为 `MTLRenderPipelineDescriptor`）
- 缓冲创建/上传/映射、纹理创建/上传、采样器、`BindGroup`
- `draw` / `drawIndexed`、`perInstance` divisor
- pushConstant → `setVertexBytes/setFragmentBytes`

**验收**：2D 图元（线/点/三角/带 alpha）在 Mac 上正确显示，与 GL 后端同一场景**截图逐像素一致**（容差内）。

### M3 高级能力（补 GL 4.1 的短板）

- `createComputePipeline` + `dispatchCompute`
- `drawIndirect` / `drawIndexedIndirect`
- 离屏渲染（`rxTextureCreateRenderTarget` / `rxSessionSetRenderTarget`）
- `readTexture` 读回（注意 Metal 无需 y 翻转）
- 文本图集（R8 覆盖率 / 距离场两种）

**验收**：离屏渲染 + 读回通过；compute 管线可 dispatch 并写回 storage buffer。

### M4 3D 与收尾

- `Mesh3D` / `Mesh3DWire` / `Highlight3D` / `Gizmo3D` 管线
- 光照 UBO（`FrameUniforms`）
- 深度偏移（`setDepthBias:slopeScale:clamp:`）
- **线框缺口处理**（见 D4-1）
- 深度范围一致性（见 §6）

**验收**：3D 模型在 Mac 上正确显示、光照与选中高亮正常，与 GL 视觉一致。

---

## 4. 构建集成

### 4.1 源文件收集

`Renderx/CMakeLists.txt` 当前的 GLOB 只匹配 `.cpp`：

```cmake
file(GLOB_RECURSE RENDER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
```

Metal 后端是 Objective-C++（`.mm`），必须扩展。为避免在 Windows/Linux 上误收，仅在 APPLE 平台加入：

```cmake
# Metal 后端是 Objective-C++，只在 Apple 平台纳入构建。
# 其他平台收进来会因为缺少 Metal 框架直接编译失败。
if(APPLE)
    file(GLOB_RECURSE RENDER_OBJC_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
    )
    list(APPEND RENDER_SOURCES ${RENDER_OBJC_SOURCES})
    enable_language(OBJCXX)
endif()
```

### 4.2 框架链接

```cmake
if(APPLE)
    find_library(METAL_LIBRARY Metal REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${METAL_LIBRARY} ${QUARTZCORE_LIBRARY} ${FOUNDATION_LIBRARY})
endif()
```

### 4.3 Shader 编译链

`EmbedShaders.cmake` 已支持 `.metallib`（二进制嵌入）。需要在 CMakeLists 里补一段**仅 APPLE 生效**的转换：`.metal` → `.metallib`（`xcrun -sdk macosx metal` + `metallib`），产物加进 `RENDERX_SHADER_SOURCES`。

不是 Apple 平台时不生成 `.metallib`，Metal 后端也不参与构建，因此两侧天然一致。

### 4.4 后端工厂声明

`rhiBackendFactory.h` 增加：

```cpp
#if defined(__APPLE__)
    /// Metal 后端。仅 Apple 平台编译；其他平台无此符号。
    IGpuDevice* createMetalDevice(const DeviceDesc& desc);
#endif
```

`rhiFactory.cpp` 的 `isBackendAvailable(Metal)` 与 `createDevice` 的 Metal 分支相应改为真实实现。

---

## 5. 验证方案

### 5.1 无法在开发机验证的部分

Metal 后端**只能在 macOS 上编译与运行**。开发机为 Windows，因此：

- 代码正确性靠「与 GL 后端逐条对齐 + 代码审查」保证；
- 功能正确性靠 Mac 侧分阶段验证，**每个 M 阶段独立交付与验收**；
- 不使用「写完一次性验证」的方式，否则出问题无法定位。

### 5.2 Mac 侧验证步骤

```bash
# 配置与构建（Metal 后端只在 Apple 平台参与编译）
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Debug
cmake --build build-mac --target RenderX --config Debug

# 现有测试必须保持全绿（它们跑 Null 后端，与 Metal 无关）
cmake --build build-mac --target RenderxTests --config Debug
./build-mac/Test/Debug/RenderxTests
```

### 5.3 新增 Metal 冒烟测试

新增 `RenderxMetalTests`（仅 Apple 平台构建），覆盖：

- 创建设备与表面成功、`Capabilities` 报告 `computeShaders = true` / `wireframeFill = false`
- 清屏 + 读回像素，颜色值精确匹配
- 一条 P3C3 三角形，读回中心像素颜色匹配
- 离屏渲染到纹理并读回
- compute dispatch 写回 storage buffer 后读回校验

### 5.4 与 GL 的对比验证

同一场景分别用 GL 与 Metal 渲染，读回像素做**逐像素比对**（允许小容差，主要为抗锯齿与线宽四舍五入差异）。这是防止「Metal 能跑但画错」的核心护栏——仅靠「看起来对」无法发现坐标错位、颜色通道交换一类问题。

---

## 6. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| **深度范围差异**（GL [-1,1] vs Metal [0,1]） | 3D 深度错误、远平面消失，症状隐蔽 | M4 显式验证；确认宿主投影矩阵约定，必要时在 Metal 侧做 z 重映射 |
| **线框缺口**（Metal 无 polygon line） | `Mesh3DWire` 在 Mac 上无效 | `Capabilities::wireframeFill = false`，上层查能力后改走三角化线框 |
| MSL 与 GLSL 双份维护 | 改一处漏一处，两侧渲染不一致 | 对比验证作为护栏；共享声明（pushConstant 块）两侧用测试锁定布局 |
| 开发机无法验证 | 问题积压到 Mac 侧才暴露 | 分 M1-M4 交付，每阶段独立验收 |
| ARC 与手动内存管理混用 | Metal 对象生命周期错误、崩溃 | `.mm` 统一开启 ARC，`id<MTL*>` 由 ARC 管理；C++ 侧只持有裸指针并在析构时置空 |
| 宿主集成未跟上 | Metal 后端只有测试能跑，GUI 用不上 | M1 完成后同步宿主侧改动清单（视口控件按平台创建 surface） |
| macOS 上 `readTexture` 原点约定 | 截图上下颠倒 | Metal 纹理原点与 CPU 图像一致，**不做翻转**；与 GL 侧行为不同，需测试锁定 |

---

## 7. 交付顺序与依赖

```
M1 设备/表面 ──► M2 管线/2D ──► M3 compute/indirect/离屏 ──► M4 3D/收尾
                      │
                      └──► 宿主侧视口接入（可并行，但需 M2 完成才可见效果）
```

下一步动作：**实现 M1**，并同时完成 §4 的构建集成（否则 Mac 上根本编译不到 Metal 源码）。
