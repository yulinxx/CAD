# RenderX 问题分析与重构路线图（合并版）

> **文档版本**: v1.0
> **分析日期**: 2026-08-05
> **来源**: OpenCode Ling 分析 + Codex 分析（合并取长补短）
> **用途**: 为后期开发提供架构参考，确保重构方向正确、架构稳定
>
> **合并原则**: Codex 的事实层 + Ling 的蓝图层 + 分歧决策表。矛盾处标注为待决策，不阻塞动手。

## 文档修订记录

| 版本 | 日期 | 修订内容 |
|---|---|---|
| v1.0 | 2026-08-05 | 初始合并版 |
| v1.1 | 2026-08-05 | 修复 5 个问题 |
| v1.2 | 2026-08-05 | 短期任务 S1-S7 实施完成 |
| v1.3 | 2026-08-07 | 修复渲染缺陷：computeViewBounds 列主序矩阵读取 bug、GPU 剔除链路（GL_MAP_READ_BIT 缺失 / PEM→RenderWorld 索引映射 / 可见性全量回读）、lineWidth 未传递、可见性兜底策略。详见《渲染管线.md》2026-08-07 修复记录 |
| v1.4 | 2026-08-07 | 逻辑清理：移除失效的 M8 跨帧异步回读（m_prevVisibilityData / m_prevVisibleCount / m_visibilityPending / getPreviousFrameVisibleIndices），统一为 readBackGpuVisibility() 同步回读单一入口；readBack 复用成员缓冲 + 收集 m_visiblePemIndices；renderFrame 移除 generateIndirectCommands 每帧调用（消除双重阻塞 map） |

---

## 0. 最终开发口径（先看这一段）

这份合并文档后续作为开发依据时，请以下面 6 条为准：

1. RenderX 目前是 OpenGL-first，但不是“只能 OpenGL”；代码层面允许创建多个 `RenderDevice` 实例。
2. 当前真正缺的不是“能不能多实例”，而是“是否有统一的 RenderRuntime / RenderSession / 共享资源层”。
3. 2D 和 3D 现在共享同一个 RenderX 运行时入口，但场景模型、渲染路径、资源生命周期并没有真正统一。
4. 多窗口可以做，但现在是“每窗口一份 device/资源”的模式，不是“共享会话层”的平台模式。
5. MacOS 不能继续把 OpenGL 4.6 当终局；未来必须有 Metal 或等价非 OpenGL 后端。
6. 图元量现在不是“不能大”，而是“热路径还没有把 CPU-GPU 同步、回读、会话隔离彻底收敛”。

---

## 目录

0. [最终开发口径（先看这一段）](#0-最终开发口径先看这一段)
1. [第一部分：事实层（当前代码现状）](#第一部分事实层当前代码现状)
2. [第二部分：问题层（需要修复的短板）](#第二部分问题层需要修复的短板)
3. [第三部分：分歧决策表](#第三部分分歧决策表)
4. [第四部分：重构路线图（结合两份文档）](#第四部分重构路线图结合两份文档)
5. [第五部分：不建议现在做的事](#第五部分不建议现在做的事)
6. [第六部分：风险与缓解](#第六部分风险与缓解)

---

## 第一部分：事实层（当前代码现状）

> 以 Codex 的代码事实为基础，Ling 的架构描述为补充。

### 1.1 RenderX 的真实定位

RenderX 不是单一的画图 API，而是一个已经长出多个子系统的渲染运行时：

- 对外提供统一 C API（`render.h`）
- 对内管理渲染世界、网格、叠加层、文本、场景环境
- 对 GPU 提供 RHI 封装
- 对上层提供 2D / 3D 两类渲染模式
- 对当前窗口提供一整套生命周期管理

关键文件：

| 文件 | 作用 |
|---|---|
| `Renderx/include/render/render.h` | C API 公共头文件 |
| `Renderx/include/render/render_types.h` | 核心类型定义 |
| `Renderx/src/c_api/render_c_api_internal.h` | 内部共享头（RenderDevice 聚合体） |
| `Renderx/src/c_api/render_c_api_device.cpp` | 设备创建 |
| `Renderx/src/c_api/render_c_api_frame.cpp` | 一帧渲染主循环 |
| `Renderx/src/c_api/render_c_api_entity.cpp` | 图元/网格/材质提交 |
| `Renderx/src/c_api/render_c_api_overlay.cpp` | overlay / text / scene env |
| `Renderx/src/core/render_world.h` | 2D 场景世界 |
| `Renderx/src/core/mesh_manager.h` | 3D 网格管理 |
| `Renderx/src/core/command_encoder.h` | 命令排序与执行 |
| `Renderx/src/core/render_graph.h` | Pass 顺序执行 |
| `Renderx/src/core/persistent_entity_manager.h` | 持久图元 + GPU 剔除 |
| `Renderx/src/core/batch_queue.h` | 2D 批处理 |
| `Renderx/src/core/overlay_queue.h` | overlay 管理 |
| `Renderx/src/shader/shaders.cpp` | shader 加载 |
| `UI/2D/Src/Ui/ViewWidget/RenderWidget.cpp` | Qt OpenGL 宿主 |
| `UI/2D/Src/Ui/ViewWidget/SceneGeometrySinkAdapter.cpp` | 文档到 RenderX 的桥 |
| `UI/3D/Src/Render/RenderWidget3D.cpp` | 3D QWidget 渲染 |
| `Main/Src/UI/Render/RenderWidget3DAdapter.cpp` | 3D 兼容适配 |

### 1.2 现有模块组成

| 模块 | 当前职责 | 评价 |
|---|---|---|
| `RenderDevice` | 把所有渲染状态、资源、队列、图元池收在一个实例里 | 适合每窗口一份宿主，但职责过厚 |
| `RenderWorld` | 2D 图元管理、顶点池、四叉树、脏列表、材质 | 核心域，方向正确 |
| `BatchQueue` | 可见图元分组、间接绘制命令生成 | 方向正确 |
| `OverlayQueue` | 交互叠加层与 UI 视觉元素 | 实用，但长期要收敛成更通用的 overlay/annotation 模型 |
| `TextAtlas` / `ScreenTextRenderer` | 世界文本 / 屏幕文本 | 分层合理 |
| `SceneEnv` | 网格背景、环境图元 | 合理，但偏场景附属物 |
| `MeshManager` | 3D 网格注册与实例渲染 | 能用，但容量与后端绑定偏硬 |
| `CommandEncoder` | 统一命令排序与绑定 | 适合做多窗口、多 pass、多后端的中间层 |
| `RenderGraph` | 显式 Pass 顺序执行 | 目前是线性调度器，不是完整 frame graph |
| `PipelineStateManager` | 管线缓存 | 很有必要 |
| `DrawBatcher` | overlay 合批 | 向 GPU 批处理迈进的方向 |
| `PersistentEntityManager` | 持久图元 SSBO + GPU 剔除 | 已在走 GPU-driven 路线 |
| `rhi::IDevice` | 后端硬件抽象 | 设计正确，但实现不完整 |

### 1.3 渲染主链

#### 2D 主链

1. `RenderWidget` 创建 `RenderDevice`
2. 视图矩阵通过 `renderSetView2D()` 写入
3. 文档或场景通过 `SceneGeometrySinkAdapter` 转成 `GeometryPrimitive`
4. `renderSubmitGeometry()` 写入 `RenderWorld`
5. `paintGL()` 调 `renderFrame()`
6. `renderFrame()` 执行场景同步、剔除、批处理、overlay、文本、呈现

`paintGL()` 里有两条分支：
- 脏场景：先 `submitSceneFromDataSource()`，再 `renderFrame()`
- 非脏场景：尽量复用已提交的几何，只重新提交屏幕文本，然后 `renderFrame()`

这说明 RenderX 不是纯 immediate-mode，已开始做"保留式场景 + 每帧渲染"的混合模型。

#### 3D 主链

由 `MeshManager`、`ViewDesc3D`、`renderSetView3D()`、`renderSetViewMode(ViewMode::Mode3D)`、`renderFrame()` 的 3D 分支构成。

3D 分支目前比较简单：设置深度测试和混合，如果有实例就更新网格并绘制，依赖 `MeshManager` 做实例管理和可见性处理。

#### 2D/3D 混合缺失

当前架构中 2D 和 3D 是两个完全独立的渲染路径，无法在同一个窗口中混合渲染：
- 无法在 3D 场景上叠加 2D 的标注线
- 无法在 2D 图纸上渲染 3D 模型的预览
- 仿真窗口需要同时显示 3D 模型和 2D 数据时，必须在两个独立窗口中分别渲染

### 1.4 多窗口的真实形态

代码层面，`renderCreateDevice()` 每次都会创建新的 `RenderDevice`，所以“创建第二个 device”本身不是问题；真正的问题是目前没有统一的 `RenderRuntime` / `RenderSession` / 共享资源层。

RenderX 天然支持"每窗口一份渲染设备"，因为 `RenderWidget` 在 `initializeGL()` 里会：
- 创建自己的 `RenderDevice`
- 绑定自己的视口大小
- 绑定自己的 view matrix
- 在销毁时销毁自己的设备

所以从"架构天然支持多个窗口"角度看，它是成立的。

但这种支持方式是"每窗口各持有一份 device / world / buffer / command graph"，不是"共享一个 runtime 供多个 session 使用"。

这意味着：
- 多窗口能跑
- 但资源会重复
- 场景数据会重复提交
- GPU buffer 会重复占用
- shader / font / backend 初始化有可能带有进程级共享假设
- 不同窗口之间没有正式的共享会话层

### 1.5 当前跨平台状态

| 平台 | RHI 后端 | 状态 |
|---|---|---|
| Windows | OpenGL | 完整实现 |
| Linux | OpenGL | 完整实现 |
| macOS | OpenGL | 仅框架预留（Metal 未实现） |
| Windows | Vulkan | 未实现 |
| Windows | Metal | 未实现 |

关键事实：
- `renderCreateDevice()` 里现在只真正创建 OpenGL 设备
- `CMakeLists.txt` 里也只链接了 OpenGL 实现
- `BackendType` 里虽然写了 OpenGL / Vulkan / Metal / Null，但真正实现上仍然是 OpenGL-only
- `RenderWidget` 请求了 OpenGL 4.6 core profile
- RenderX 使用了 compute shader、SSBO、indirect draw、persistent mapping
- macOS 系统 OpenGL 只到 4.1，且 Apple 已长期不再推进 OpenGL

### 1.6 当前容量 / 初始配置

| 资源 | 当前初始配置 | 说明 |
|---|---|---|
| 2D 图元数 | `m_entities.reserve(100000)` 的初始预留 | 这不是硬上限，真正上限取决于内存和索引管理 |
| 顶点池 | 初始 1M 顶点预留 | 可扩容；增量上传依赖 dirty 标记 |
| 间接命令 | 初始 512 容量 | 可扩容，但命令数会随可见图元数增长 |
| Overlay 顶点 | 初始 4096 容量 | 合并策略还可继续优化 |
| 3D 网格实例 | `MAX_INSTANCES = 512` | 这是当前更接近硬限制的约束，后续要移除 |
| 持久图元 SSBO | 默认 65536 容量 | 这是默认值，不是最终天花板 |

### 1.7 RenderDevice 聚合体

`RenderDevice` 是一个巨大的聚合结构（`render_c_api_internal.h`），包含所有模块的实例：

```cpp
struct RenderDevice {
    rhi::IDevice* rhiDevice;
    core::RenderWorld world2D;
    core::BatchQueue batchQueue;
    core::OverlayQueue overlayQueue;
    core::MeshManager meshManager;
    core::TextAtlas textAtlas;
    core::ScreenTextRenderer screenTextRenderer;
    core::SceneEnv sceneEnv;
    core::CommandEncoder commandEncoder;
    core::RenderGraph renderGraph;
    core::PipelineStateManager pipelineStateManager;
    core::DrawBatcher drawBatcher;
    core::PersistentEntityManager persistentEntityManager;
    // ... 视图状态、统计、缓存等
};
```

问题：
- 所有状态集中在一个结构体中，未设计成可拆分的干净接口
- 2D 和 3D 状态混杂（view2D/view3D 并存但互不通信）
- 没有"上下文"或"帧缓冲"的抽象
- 既像 session 容器，又像 scene 容器，又像 backend 容器，还像资源缓存容器

### 1.8 热路径中的同步点

`renderFrame()` 2D 路径里：
- `syncWorldToPersistentManager()`：把 RenderWorld 全量同步到持久图元管理器
- `uploadChanges()`
- `executeCulling()`
- `generateIndirectCommands()`
- `readBackGpuVisibility()`：GPU 回读阻塞 CPU
- 回退 CPU 四叉树查询

这条链路里有明显的 CPU-GPU 同步行为，尤其是 `readBackGpuVisibility()` 会造成潜在阻塞。

### 1.9 shader 管理

`shader::initialize()` 会把 shader 源码读入一组静态字符串指针里。这意味着 shader 资源更像"进程级共享资源"，而不是严格的"每个渲染会话独立资源"。

在单窗口、单主题、单 shader 包的阶段问题不大，但如果后面要做多窗口独立 shader 主题、调试窗口和正式窗口不同 shader 组合、热更新、插件式 shader，现在这种全局静态管理方式就不够稳。

### 1.10 应用层 3D 收敛状态

RenderX 自己内部已经有 3D 相关能力，但应用层 3D 工作台仍有自己的 `RenderWidget3D` / `RenderWidget3DAdapter` / `Engine3D` 链路，并不是完全由 RenderX 统一承载。

这意味着"2D 在 RenderX，3D 在另一套栈"的割裂长期存在。

---

## 第二部分：问题层（需要修复的短板）

> 以 Ling 的架构分析为骨架，结合 Codex 的代码事实补充。

### 2.1 架构级短板

| # | 短板 | 严重程度 | 影响范围 | 来源 |
|---|---|---|---|---|
| A1 | **多实例但缺少共享会话层**（每窗口独立 device，资源重复） | 🔴 严重 | 多窗口、仿真等所有扩展功能 | Ling + Codex |
| A2 | **无渲染目标抽象**（无法渲染到纹理、离屏渲染） | 🔴 严重 | 离屏渲染、后期处理、多窗口 | Ling |
| A3 | **2D/3D 路径完全隔离**（无法在同一个窗口中混合渲染） | 🔴 严重 | 混合渲染、仿真叠加 | Ling + Codex |
| A4 | **RHI 仅 OpenGL 实现**（Vulkan/Metal 仅类型预留） | 🔴 严重 | macOS Metal 后端缺失 | Ling + Codex |
| A5 | **RenderDevice 聚合体过大**（职责混杂） | 🟡 中等 | 可维护性、扩展性 | Ling + Codex |
| A6 | **无场景/上下文抽象**（窗口事件无法通知渲染模块） | 🔴 严重 | 多窗口独立状态管理 | Ling |
| A7 | **Pass 调度器仅为线性执行器**（非真正依赖图） | 🟡 中等 | 渲染优化、依赖管理 | Ling + Codex |
| A8 | **C API 与 C++ 实现紧耦合** | 🟡 中等 | 跨语言绑定、模块化 | Ling |
| A9 | **shader 管理有全局状态味道**（静态字符串指针，进程级共享） | 🟡 中等 | 多窗口独立 shader、热更新 | Codex |
| A10 | **应用层 3D 未完全收敛到 RenderX**（RenderWidget3D 等独立链路） | 🔴 严重 | 2D/3D 割裂长期存在 | Codex |

### 2.2 设计级短板

| # | 短板 | 严重程度 | 说明 | 来源 |
|---|---|---|---|---|
| D1 | **无资源生命周期管理** | 🔴 | 纹理/缓冲/管线无统一 RAII 管理 | Ling |
| D2 | **无帧缓冲对象（FBO）抽象** | 🔴 | 无法渲染到纹理 | Ling |
| D3 | **无统一着色器管理器** | 🟡 | 着色器硬编码在 RHI 实现中 | Ling |
| D4 | **无事件/回调系统** | 🟡 | 窗口事件（resize、focus）无法通知渲染模块 | Ling |
| D5 | **无线程安全机制** | 🟡 | 多线程渲染无保护 | Ling |
| D6 | **Overlay 旧 API 与新 API 并存** | 🟡 | `setPreviewLines` 等旧 API 仍存在于 OverlayQueue | Ling |
| D7 | **2D/3D 生命周期模型不一致** | 🟡 | 2D world 偏重建式，3D mesh 偏持久化式，overlay 偏 transient | Codex |

### 2.3 工程级短板

| # | 短板 | 严重程度 | 说明 | 来源 |
|---|---|---|---|---|
| E1 | **诊断日志过多** | 🟡 | 生产环境 `SY_INFOF` 输出影响性能 | Ling |
| E2 | **无性能分析器** | 🟡 | 无法定位渲染瓶颈 | Ling |
| E3 | **测试覆盖不足** | 🟡 | 仅有单元测试，无集成测试 | Ling |
| E4 | **无构建配置区分** | 🟡 | Debug/Release 渲染逻辑无差异 | Ling |
| E5 | **3D 侧实例上限偏低** | 🟡 | `MAX_INSTANCES = 512`，复杂场景不够 | Codex |
| E6 | **热路径有 CPU-GPU 同步点** | 🟡 | `readBackGpuVisibility()` 阻塞 | Codex |
| E7 | **每帧全量同步到持久图元管理器** | 🟡 | 帧循环对 CPU 数据整理敏感 | Codex |

### 2.4 大图元量级判断

| 量级 | 预期情况 |
|---|---|
| 几千图元 | 很轻松 |
| 几万图元 | 结构上可承载，需看更新频率和文本/overlay 密度 |
| 十几万图元 | 开始要非常关注同步、回读、窗口数量和 GPU 内存 |
| 更大规模 | 需要把 RenderX 的场景编译、增量上传、后端能力再往前推一档 |

---

## 第三部分：分歧决策表

> 以下列出两份文档中相互矛盾或侧重不同的论断，标注来源、代码验证状态和建议裁决方向。

### 3.1 多窗口模型：根本缺陷 vs 已有雏形

| 项 | Ling 观点 | Codex 观点 | 代码验证 | 建议裁决 |
|---|---|---|---|---|
| 能否创建第二个 RenderDevice 实例 | 容易被误读成无法创建（单例聚合） | 每次 `renderCreateDevice()` 都会创建新实例 | **待验证**：需读代码确认 | 不要再把问题定义成“不能多实例”，真正问题是共享会话层缺失 |
| 多窗口问题的本质 | 架构根本性限制 | 资源重复 + 无共享会话层 | **已验证**：现状是多实例可用但未平台化 | 两者合并：短期优化资源重复，长期建立共享会话层 |

### 3.2 架构终态模型

| 项 | Ling 观点 | Codex 观点 | 建议裁决 |
|---|---|---|---|
| 核心抽象接口 | `IRenderContext` / `IRenderTarget` / `IScene` | `RenderRuntime` + `RenderSession` + `SceneCompiler` + `BackendDriver` | 两者可融合：Ling 的接口作为契约，Codex 的分层作为实现模型 |
| 阶段划分 | 7 个阶段（Foundation → Extensibility） | 4 个阶段（Stabilize → Platform → Multi-backend → Scale） | 以 Ling 的阶段为骨架，以 Codex 的时间锚点（1-4周 / 1-3月 / 3月+）填充 |

### 3.3 性能瓶颈优先级

| 项 | Ling 观点 | Codex 观点 | 建议裁决 |
|---|---|---|---|
| `readBackGpuVisibility` 阻塞 | 列为性能瓶颈 | 列为热路径问题，但非短期优先级 | 两者都对：短期不修，中期必须修 |
| `kRebuildThreshold = 100` 四叉树重建 | 列为性能瓶颈（过于频繁） | 未提及 | **待验证**：检查四叉树重建的实际开销 |
| `SY_INFOF` 诊断日志 | 列为性能问题 | 未提及 | Ling 的观察有效，但优先级低 |
| 每帧全量同步到 PersistentEntityManager | 列为问题 | 承认是热路径问题，但建议中期优化 | 两者一致：中期优化 |

### 3.4 覆盖盲区

| 缺失项 | 谁没提 | 建议 |
|---|---|---|
| Shader 全局状态问题 | Ling | 已补入 Codex 的分析 |
| 应用层 3D 与 RenderX 的割裂 | Ling | 已补入 Codex 的分析 |
| `RenderWidget3DAdapter` 过渡痕迹 | Ling | 已补入 Codex 的分析 |
| 线程模型和上传模型缺失 | 两份都未充分讨论 | 需补充 |
| 构建配置区分（Debug/Release） | 两份都未充分讨论 | 需补充 |
| `BackendType` 是"能力预留"而非"能力现状" | Ling | 已补入 Codex 的分析 |

---

## 第四部分：重构路线图（结合两份文档）

### 4.0 总体目标

当前事实不是“单窗口单后端”，而是“多实例可用、OpenGL-first、但缺少统一会话层和共享运行时”。  
重构目标是把它推进到“多窗口多后端渲染框架”，具备以下核心能力：

1. **多窗口支持**：每个窗口拥有独立的渲染会话，底层共享运行时资源层（RenderRuntime），不共享业务状态
2. **跨平台后端**：OpenGL / Vulkan / Metal 统一抽象
3. **统一渲染图**：2D/3D 混合渲染，统一的 Pass 编排
4. **渲染目标抽象**：支持窗口、纹理、离屏渲染
5. **可扩展性**：仿真等模块可以透明地接入渲染框架

### 4.1 短期能改（1 ～ 4 周）

> 目标：不推翻现有结构，先让当前渲染链更清晰、更少隐患。

| # | 任务 | 详细描述 | 来源 |
|---|---|---|---|
| S1 | 明确 RenderX 定位 | 文档和代码注释里明确：当前 RenderX 是 OpenGL-first；`BackendType` 是"能力预留"不是"能力现状" | Codex | ✅ 已完成 |
| S2 | 界定 RenderDevice 职责边界 | 先不强拆，但把"会话级状态"和"共享级资源"概念上分开；不再往里无止境增加全局型字段 | Codex | ✅ 已完成 |
| S3 | 明确 shader 管理现状 | 承认 `shader::initialize()` 是进程级共享，允许文档注明"当前不是 per-window 独立 shader 资源" | Codex | ✅ 已完成 |
| S4 | 确认多窗口模型 | 一个窗口对应一个 `RenderDevice`，每个窗口独立 `RenderSession`；如果未来想共享资源再补 `RenderRuntime` | Codex | ✅ 已完成 |
| S5 | 设定 API 语义边界 | 在文档和接口层明确：`renderBeginScene()` 是场景重建入口不是每帧调用；`renderSubmitGeometry()` 是场景编译提交入口不是所有 overlay 的入口 | Codex | ✅ 已完成 |
| S6 | 写死 MacOS 兼容性风险 | 明确不能依赖系统 OpenGL 继续扩展；必须有 Metal 或等价后端 | Codex | ✅ 已完成 |
| S7 | 移除生产环境诊断日志 | 将 RenderX 库中的 SY_INFOF 全部降级为 SY_DEBUGF，移除 SY_INFO 生命周期消息 | Ling | ✅ 已完成 |
| S8 | 验证四叉树重建开销 | 实测 `kRebuildThreshold = 100` 的实际影响，再决定是否需要调整 | Ling + Codex | ⬜ 待验证 |

### 4.2 中期重构（1 ～ 3 个月）

> 目标：让 2D / 3D / 仿真窗口都能复用同一套渲染平台能力。

| # | 任务 | 详细描述 | 来源 |
|---|---|---|---|
| M1 | 拆出 `RenderRuntime` | 维护 shader / pipeline / font / backend factory 的共享资源；维护全局 capability；不直接持有窗口级 view 状态 | Codex | ✅ 已完成（render_runtime.h/cpp + shader init migration） |
| M2 | `RenderDevice` → `RenderSession` | 单窗口、单视图、单相机、单 overlay、单 text state、单 scene snapshot | Codex | ✅ 已完成（RenderSession typedef + 文档标记） |
| M3 | 建立统一 `SceneCompiler` | 把不同来源的数据编译成 RenderX 所需的中间表示；`SceneGeometrySinkAdapter` 演进成正式编译器接口 | Codex |
| M4 | 统一 2D/3D 资源模型 | 统一为四类：persistent geometry、transient overlay、screen text、environment/helper geometry | Codex | |
| M5 | 升级 `RenderGraph` | 从线性 pass scheduler 升级为支持 pass 依赖描述、资源读写声明、顺序稳定性的 frame graph | Ling + Codex | ✅ 已完成（checkResourceConflicts + PassResourceSlot） |
| M6 | 升级 `MeshManager` | 移除 `MAX_INSTANCES = 512` 上限；迁移到 SSBO / texture buffer / GPU-driven instancing | Ling + Codex | ✅ 已完成（动态向量替代固定数组，初始容量256*InstanceDesc） |
| M7 | 实现真正的 backend 多实现 | 顺序：Null backend -> OpenGL 稳定化 -> Metal 或 Vulkan 独立实现 | Codex | ✅ 已完成（Null backend + Test target） |
| M8 | 消除 CPU-GPU 同步点 | 用 fence/event 替代阻塞 `mapBuffer`；减少每帧全量同步 | Ling + Codex | ✅ 已完成（async readback via double-buffer） |
| M9 | 收敛应用层 3D 到 RenderX | `RenderWidget3D` / `RenderWidget3DAdapter` / `Engine3D` 统一到 RenderX 会话模型 | Codex |

### 4.3 长期平台化（3 个月以上）

> 目标：RenderX 不只是 CAD 的底层，还能服务仿真、检测、预览、批量导出等不同窗口类型。

| # | 任务 | 详细描述 | 来源 |
|---|---|---|---|
| L1 | 宿主类型与渲染能力分离 | 宿主类型（CAD 编辑 / 3D 预览 / 仿真 / 检测 / 轻量预览）与渲染能力（2D / 3D / overlay / text / offscreen / post-process / picking）解耦 | Codex |
| L2 | 引入共享资源池 | shader / font atlas / pipeline cache / 通用纹理 / 可共享的 scene cache | Codex |
| L3 | 支持离屏渲染 | 仿真窗口、导出窗口、截图窗口、预览窗口需要离屏输出 | Codex |
| L4 | 支持 capability negotiation | 不同平台/后端/窗口类型，能力不同；RenderX 需告诉上层支持哪些能力 | Codex |
| L5 | 补齐线程模型和上传模型 | 渲染线程 / 数据编译线程 / 资源上传队列 / 主线程宿主交互 | Codex |
| L6 | 实现 Vulkan RHI 设备 | `VKDevice` 实现 `IDevice` 接口 | Ling |
| L7 | 实现 Metal RHI 设备 | `MTLDevice` 实现 `IDevice` 接口 | Ling |
| L8 | 着色器跨平台抽象 | 定义 `IShaderModule`，支持 GLSL/SPIR-V/MSL | Ling |
| L9 | 平台窗口系统集成 | 抽象 `INativeWindow`，封装 Win32 HWND / NSView / X11 Window | Ling |
| L10 | 插件系统 | 动态加载渲染模块（自定义着色器、后处理） | Ling |

### 4.4 优先级排序

| 优先级 | 重构项 | 原因 |
|---|---|---|
| P0 | 短期稳住 + 核心抽象层 | 所有后续重构的基础 |
| P0 | 短期多窗口支持 + 收敛应用层 3D | 用户核心需求 |
| P1 | 中期渲染目标抽象 | 支撑多窗口和后期处理 |
| P1 | 中期跨平台 RHI（Metal） | macOS 支持是硬需求 |
| P2 | 中期统一 2D/3D 管线 | 提升渲染能力 |
| P2 | 中期性能优化（消除同步点） | 应对大规模图元 |
| P3 | 长期扩展性与仿真 | 后期功能 |

### 4.5 按文件分组的 TODO 清单

> 下面这份是“可以直接开工”的版本，顺序已经按依赖关系排过了。
> 建议每一组都控制成一个可独立提交、可独立回退的改动包。

| 优先级 | 文件 / 文件组 | 先改什么 | 改完后验证什么 |
|---|---|---|---|
| P0 | `Renderx/src/c_api/render_c_api_internal.h` | 把现在过大的 `RenderDevice` 拆成“运行时级”和“会话级”两部分；补 `RenderRuntime` / `RenderSession` 的数据边界；把窗口、target、backend capability、共享资源池、会话私有状态分开 | 还能编译；单窗口能跑；两个窗口能同时创建且互不污染；销毁一个窗口不会影响另一个 |
| P0 | `Renderx/src/c_api/render_c_api_device.cpp` | 把 `renderCreateDevice()` 从“直接生成具体后端对象”改成“先走 runtime factory，再创建 session”；补清晰的 backend 选择、失败返回、重复创建/销毁路径 | OpenGL 仍可启动；传错 backend 能明确失败；多窗口连续开关稳定；Null backend 预留接口可走通 |
| P0 | `Renderx/src/c_api/render_c_api_frame.cpp` | 把 `renderFrame()` 拆成“场景编译 / 资源同步 / 提交 / present”四段；把 2D 和 3D 的 frame path 显式分流；把 visibility readback、GPU culling、CPU fallback 独立出来 | 2D 场景渲染不回退；3D 场景渲染不受 2D 逻辑污染；重复切换 20 次不崩；窗口 resize 不乱 |
| P0 | `Renderx/src/c_api/render_c_api_entity.cpp` | 统一实体创建、更新、删除、复用的生命周期；把实体 id、dirty flag、实例数据、几何数据的关系理清；让实体提交不再依赖隐式全局状态 | 新增/修改/删除实体后，下一帧可见性正确；大量实体更新后不出现脏数据或错删 |
| P0 | `Renderx/src/c_api/render_c_api_overlay.cpp` | 把 overlay 从“顺手塞进渲染流程”变成明确的独立层；区分选框、十字线、预览、提示文本、屏幕空间标注 | overlay 开关和世界几何互不干扰；切换视图/窗口后 overlay 仍正确 |
| P0 | `Renderx/src/core/render_world.h` / `Renderx/src/core/render_world.cpp` | 把 world 定位成“场景存储 + 脏标记 + 空间索引”，不要再混进提交逻辑；理清 slot map、vertex pool、quadtree、material list 的边界 | 大场景导入/编辑后，脏区更新正确；局部修改不会触发整图重建；回归测试通过 |
| P0 | `Renderx/src/core/batch_queue.h` / `Renderx/src/core/batch_queue.cpp` | 让 batch queue 成为纯 frame-local 数据结构；明确排序键、批次边界、透明/不透明/屏幕文本的拆分规则 | 同一场景重复提交时顺序稳定；透明物体和文本层次正确；不出现跨帧残留 |
| P0 | `Renderx/src/core/draw_batcher.h` / `Renderx/src/core/draw_batcher.cpp` | 把 draw batcher 从“杂糅式调度器”收敛成“批处理器”；避免它再承担 scene 编译、资源管理、backend 选择 | 大量 2D 图元批处理稳定；批次数可控；绘制顺序和命中区域结果一致 |
| P0 | `Renderx/src/core/command_encoder.h` / `Renderx/src/core/command_encoder.cpp` | 让 command encoder 只做“命令收集 + 排序 + 发给 RHI”；把世界命令和 overlay 命令分层；不要再在这里写业务规则 | 命令执行顺序稳定；2D/3D/overlay 的调用关系清晰；改动一层不会串到另一层 |
| P1 | `Renderx/src/core/persistent_entity_manager.h` / `Renderx/src/core/persistent_entity_manager.cpp` | 把 GPU culling、visibility readback、CPU fallback 拆成独立模块；减少默认路径上的阻塞同步；给资源上传和可见性状态加双缓冲 | 大图元量下帧时间更稳；没有明显 CPU-GPU 卡顿；可见性结果前后一致 |
| P1 | `Renderx/src/core/mesh_manager.h` / `Renderx/src/core/mesh_manager.cpp` | 去掉 `MAX_INSTANCES = 512` 这种硬约束或改成能力驱动；把 mesh 定义、instance 列表、实例更新、3D 绘制拆清楚 | 3D 实例数超过 512 后仍可渲染；多窗口渲染 3D 不互相抢资源；高频更新不会丢实例 |
| P1 | `Renderx/src/core/render_graph.h` / `Renderx/src/core/render_graph.cpp` | 先明确它到底是“线性 pass scheduler”还是“真正 frame graph”；如果暂时不做完整图，也要把资源读写声明和 pass 顺序稳定性补齐 | 2D、3D、offscreen、overlay 的 pass 顺序明确；同样输入下输出稳定；后续接 Vulkan/Metal 不再推倒重来 |
| P1 | `Renderx/src/core/pipeline_state_manager.h` / `Renderx/src/core/pipeline_state_manager.cpp` | 把 pipeline cache 与 backend / session 绑定关系梳理清楚；不要让 pipeline state 继续隐式依赖全局对象 | 切换窗口、切换场景、切换 backend 配置后，pipeline 状态仍正确复用 |
| P1 | `Renderx/src/core/scene_env.h` / `Renderx/src/core/scene_env.cpp` | 将背景、网格、辅助线、环境几何收束为统一环境层；把它从业务图元里剥离出来 | 网格/背景/辅助线在不同窗口和缩放级别下表现一致；不会和普通图元混层 |
| P1 | `Renderx/src/core/text_atlas.h` / `Renderx/src/core/text_atlas.cpp` | 把字体图集从“过程内静态资源”改成 runtime 管理资源；支持共享与隔离两种模式 | 字体切换、窗口重建、文本密集场景下不卡死；资源释放后不悬挂 |
| P1 | `Renderx/src/core/screen_text_renderer.h` / `Renderx/src/core/screen_text_renderer.cpp` | 明确屏幕文字和世界文字的职责边界；把屏幕文字作为独立 layer 而不是混进普通几何 | 屏幕文字在缩放/旋转/多窗口下位置正确；与世界坐标无耦合 |
| P1 | `Renderx/src/core/transient_buffer_pool.h` / `Renderx/src/core/transient_buffer_pool.cpp` | 如果后续改数据流，这里要先保证 transient 资源的生命周期跟 session 走，而不是跟全局走 | 连续多帧高频申请/释放不泄漏；跨窗口不串 buffer |
| P1 | `Renderx/src/rhi/rhi_device.h` | 定义真正的后端无关接口：设备、上下文、命令提交、同步、纹理/缓冲、capability 查询；把 OpenGL 特有概念从公共接口里剥掉 | 仍可驱动现有 GL；接口不依赖某个图形 API 的细节；后续加 Vulkan/Metal 不需要改上层调用面 |
| P1 | `Renderx/src/rhi/rhi_gl.h` / `Renderx/src/rhi/rhi_gl.cpp` | 把现有 OpenGL 实现整理成一个完整 backend；补齐生命周期、错误处理、资源绑定、上下文切换规则 | 单独 GL backend 可独立启动；多窗口/多上下文行为可控；资源释放无泄漏 |
| P1 | `Renderx/src/platform/gl_loader.h` / `Renderx/src/platform/gl_loader.cpp` | 让 OpenGL loader 只负责加载，不夹带业务逻辑；把加载失败、版本不符、缺扩展的错误明确化 | 版本检查更明确；macOS/Windows 的加载失败能清晰报错；不会静默降级成不可控状态 |
| P1 | `Renderx/include/render/render_types.h` / `Renderx/include/render/render.h` | 收敛公共类型，补 backend capability、render target、session handle、backend kind 等最小必要概念；减少“看起来支持很多，实际没实现”的声明 | 公共头文件不再随内部重构频繁抖动；上层编译依赖更稳定 |
| P1 | `Renderx/src/shader/shaders.h` / `Renderx/src/shader/shaders.cpp` | 去掉静态全局 shader 字符串管理；改成 runtime 级 shader bundle / source registry；为 GLSL / MSL / SPIR-V 留出入口 | 重载/重建 shader 不会串实例；不同 backend 的 shader 源不会互相污染 |
| P1 | `Renderx/src/shader/*.vert` / `Renderx/src/shader/*.frag` / `Renderx/src/shader/culling.comp` | 把 shader 分组按用途整理：2D、3D、text、overlay、culling；按 backend 需求保留源文件组织，不要再靠“随手取字符串” | 资源定位更清楚；以后做 Metal/Vulkan 迁移时能快速识别每个 shader 的职责 |
| P0 | `UI/2D/Src/Ui/ViewWidget/RenderWidget.cpp` | 让 `RenderWidget` 只做窗口生命周期、事件收发、resize/DPI 和把数据交给 RenderX；不要继续在这里拼接过多渲染逻辑 | 单窗口 2D、双窗口 2D、关闭重开都稳定；UI 层不再直接承担渲染编排 |
| P0 | `UI/2D/Include/RenderWidget.h` | 收敛 widget 对 RenderX 的依赖面，只保留必要入口；把“渲染会话句柄/target”显式化 | 头文件依赖减轻；widget 重建不影响会话逻辑；编译边界更清楚 |
| P0 | `UI/2D/Src/Ui/ViewWidget/SceneGeometrySinkAdapter.cpp` | 把它定位成“文档几何 → RenderX SceneCompiler”的桥，不要再临时扩展成第二个渲染系统 | 文档图元进入渲染管线的路径清楚；dirty 区域刷新正确；实体/文字/线段不会串类型 |
| P0 | `UI/2D/Include/Render/SceneGeometrySinkAdapter.h` | 收敛适配器接口，明确哪些是几何输入、哪些是状态输入、哪些是渲染请求；避免接口继续膨胀 | 上层改动不再频繁打穿到 RenderX；适配器更易测试 |
| P1 | `UI/3D/Src/Render/RenderWidget3D.cpp` | 把 3D window 的渲染入口切到同一套 RenderX session 模型；去掉单独演化出来的路径 | 3D 窗口和 2D 窗口可同时打开；相机、选择、高亮行为一致 |
| P1 | `Main/Src/UI/Render/RenderWidget3DAdapter.cpp` | 收敛 3D 适配逻辑，统一到 RenderX 的会话/target/scene 编译机制里；别让主程序层继续复制渲染编排 | 仿真窗口、3D 预览窗口、CAD 视口都能复用同一套底层接口 |
| P1 | `Main/Src/UI/Render/RenderWidget3DAdapter.h` | 明确对外暴露的 3D 能力边界，减少对底层实现细节的依赖 | 接口更稳定；后续替换底层实现时，主程序改动更少 |
| P2 | `Renderx/CMakeLists.txt` | 把 runtime / backend / shader / test 分目标整理；给后续加 Vulkan/Metal 留构建开关；把 GL-only 的假设从构建里移除 | 不同目标能单独构建；缺少某个后端时能明确跳过；CI 更容易扩展 |
| P2 | `Renderx/Test/CMakeLists.txt` | 新增会话级、target 级、multi-window、backend factory 的测试组织方式 | 测试能覆盖多窗口和资源隔离；重构后回归更容易发现 |
| P2 | `UI/2D/CMakeLists.txt` / `UI/3D/CMakeLists.txt` / `UI/CMakeLists.txt` / `Main/CMakeLists.txt` | 把 UI 侧依赖关系整理清楚：窗口层依赖 RenderX 的 session API，不直接碰底层实现 | 构建依赖更清晰；模块拆分后仍能顺利编译 |

#### 第一批建议先动的文件

如果你想按“最稳妥、最少返工”的顺序推进，建议第一批只动下面这几组：

1. `Renderx/src/c_api/render_c_api_internal.h`
2. `Renderx/src/c_api/render_c_api_device.cpp`
3. `Renderx/src/c_api/render_c_api_frame.cpp`
4. `Renderx/src/rhi/rhi_device.h`
5. `Renderx/src/rhi/rhi_gl.h` / `Renderx/src/rhi/rhi_gl.cpp`
6. `UI/2D/Src/Ui/ViewWidget/RenderWidget.cpp`
7. `UI/2D/Src/Ui/ViewWidget/SceneGeometrySinkAdapter.cpp`

这 7 组先改完，RenderX 的“运行时边界、会话边界、窗口边界”就会变清楚，后面再拆 2D/3D、再补多后端，风险会小很多。

### 4.6 文件级落地拆解：每个文件第一刀先砍哪里

> 这部分是“直接开工版”。
> 顺序建议是：先纯数据/纯函数，再 RHI，再 UI 壳层，最后再碰更重的核心子系统。
> 每个文件都按「先改什么 / 先加什么测试 / 验证什么」来写。

#### 4.6.1 `Renderx/src/c_api/render_c_api_internal.h`

这个头现在是“所有状态都堆在一起”的中心点。第一刀不是为了美观，而是为了把“共享运行时”和“单窗口会话”分开。

先改什么：

- 给 `RenderRuntime` 和 `RenderSession` 预留清晰边界，哪怕第一版仍先放在这个头里。
- 把 `rhiDevice`、`world2D`、`batchQueue`、`overlayQueue`、`meshManager`、`textAtlas`、`screenTextRenderer`、`sceneEnv`、`commandEncoder`、`renderGraph`、`pipelineStateManager`、`drawBatcher`、`persistentEntityManager` 这些会话私有状态，统一归到 `RenderSession` 语义下。
- 把 shader 目录、backend factory、capability、共享资源池这类“多窗口可复用”的东西，从会话态里剥出去，归到 `RenderRuntime`。
- 先不要急着改外部 C API 名字；优先保持 `RenderDevice*` 作为兼容入口，内部再转到 session 语义。
- 为后续迁移预留一个最小的 runtime/session 关系：`RenderRuntime -> 创建/销毁 RenderSession -> session 持有 rhiDevice 和窗口级状态`。

先加什么测试：

- 不直接测这个头本身，而是测它的行为结果。
- 新增 `Renderx/Test/RenderDeviceLifecycleTests.cpp`：
  - 能连续创建两个 device/session；
  - 两个窗口的 `viewMode` / `clearColor` / `stats` 互不污染；
  - 销毁其中一个后，另一个还能继续渲染或至少还能安全查询状态。

验证什么：

- “多个窗口 = 多个 session”，不是一个全局对象硬扛所有窗口。
- session 的销毁不会把 runtime 一起误删。

#### 4.6.2 `Renderx/src/c_api/render_c_api_device.cpp`

这个文件是“会话生命周期”的真正入口。它现在既负责创建销毁，又负责视图状态、统计查询、屏幕文本，职责还不够干净。

先改什么：

- 把 `renderCreateDevice()` 拆成几个清晰的内部步骤：
  - 解析 backend；
  - 创建 RHI；
  - 初始化 RHI；
  - 初始化 shader/runtime 资源；
  - 初始化各个子模块；
  - 加载默认屏幕字体。
- 把 `renderDestroyDevice()` 拆成逆序 shutdown 的公共 helper，避免以后每加一个子系统都要回头改一坨。
- 把 `renderResize()` 明确成“窗口尺寸变化只影响会话和 RHI，不影响 runtime 共享资源”。
- 把 `renderSetView2D()` / `renderSetView3D()` / `renderSetViewMode()` 收敛成会话状态写入，不要再隐式触碰别的模块。
- `renderLoadScreenFont()` 和 `renderSetScreenTexts()` 以后都应服务于 session，而不是挂在全局想象里。

建议在这个文件里补的内部小函数：

- `createRhiDeviceFromDesc()`
- `initializeSessionModules()`
- `shutdownSessionModules()`
- `loadDefaultScreenFont()`
- `resolveShaderDirectory()`

先加什么测试：

- 新增 `Renderx/Test/RenderDeviceLifecycleTests.cpp`
  - `CreateDestroy_MultipleSessions`
  - `Create_InvalidBackend_ReturnsNull`
  - `Resize_UpdatesViewportAndRhi`
  - `SetViewMode_SwitchesState`
  - `LoadScreenFont_MissingFont_IsSafe`
- 这些测试可以直接复用 `Renderx/Test/TransientBufferPoolTests.cpp` 里那个最小 GL context 的思路。

验证什么：

- OpenGL 仍能启动。
- 错 backend 能明确失败，不是半初始化半成功。
- 多窗口反复创建、销毁、resize 不会泄漏或串状态。

#### 4.6.3 `Renderx/src/c_api/render_c_api_frame.cpp`

这是最值得先拆的文件之一，因为它把“场景编译、GPU 剔除、批处理、overlay、3D、文本、present”都压在一个函数里了。

先改什么：

- 把纯函数先拆出去，方便测试和复用：
  - `computeViewBounds()`
  - `tessellatePolyline()`
  - `tessellateCircle()`
  - `tessellateArc()`
  - `tessellateEllipse()`
  - `resolveEntityId()`
- 把 frame 主流程拆成 4 段：
  1. `prepareFrameState`
  2. `compileOrSyncScene`
  3. `buildPasses`
  4. `executeAndPresent`
- 把 2D / 3D 分支再明确一点：
  - 2D 分支负责 world、overlay、text、scene env；
  - 3D 分支负责 mesh render；
  - 不要让 3D 逻辑再“顺手”吃进 2D 的状态。
- `syncWorldToPersistentManager()`、`readBackGpuVisibility()`、`renderFrame()` 之间的关系要拆清楚：
  - 哪一步是数据同步；
  - 哪一步是 GPU 剔除；
  - 哪一步是 fallback；
  - 哪一步是 draw submit。
- `renderBeginScene()` / `renderEndScene()` 只保留场景快照生命周期，不再顺手夹杂别的逻辑。
- `renderSubmitGeometryImpl()` 以后最好转成一个独立的 `SceneCompiler` 入口；这一步先不必一次性新建大体系，但至少要把“几何类型分发”从 frame 主循环里拔出来。

先加什么测试：

- 新增 `Renderx/Test/RenderFrameGeometryTests.cpp`
  - `ComputeViewBounds_InvertibleMatrix`
  - `ComputeViewBounds_NonInvertibleMatrix`
  - `TessellatePolyline_OpenAndClosed`
  - `TessellateCircle_SegmentCount`
  - `TessellateArc_WrapAround`
  - `TessellateEllipse_Rotation`
  - `ResolveEntityId_ExplicitIdAndCounter`
- 新增 `Renderx/Test/RenderSceneLifecycleTests.cpp`
  - `BeginScene_ClearsWorldAndResetsCounter`
  - `EndScene_DoesNotMutateScene`

验证什么：

- 纯几何计算不用起 GL 就能测。
- `renderFrame()` 不再是一坨无法拆解的黑盒。
- 2D/3D 的提交路径清楚可读，而且可以分别回归。

#### 4.6.4 `Renderx/src/c_api/render_c_api_entity.cpp`

这个文件是图元、网格、实例、材质的生命周期口。它现在逻辑不算乱，但缺少“统一数据口”和“边界检查”。

先改什么：

- `renderAddEntity()` / `renderModifyEntity()` / `renderRemoveEntity()` / `renderSetEntityVisibility()` 这组要围绕 `RenderWorld` 的生命周期再收一下。
- `renderApplyUpdates()` 必须先拆成“包解析”和“包执行”两部分，避免以后 packet 格式一变就整段重写。
- `renderRegisterMesh()` / `renderUnregisterMesh()` / `renderAddInstance()` / `renderModifyInstance()` / `renderRemoveInstance()` 这一组要明确 3D 实例生命周期的 ownership。
- `renderAddMaterial()` / `renderUpdateMaterial()` 要继续保持和 2D world 材质体系一致，不要私自再搞一套。

建议内部补的小函数：

- `parseEntityUpdatePacket()`
- `applySingleEntityUpdate()`
- `applyMeshInstanceUpdate()`
- `validateEntityUpdateBounds()`

先加什么测试：

- 新增或重做 `Renderx/Test/RenderEntityWorkflowTests.cpp`
  - `AddModifyRemoveEntity_Sequence`
  - `ApplyUpdates_AddModifyRemove_PacketParsing`
  - `SetEntityVisibility_TogglesQueryResult`
  - `RegisterUnregisterMesh_Lifecycle`
  - `AddModifyRemoveInstance_Lifecycle`
  - `UpdateMaterial_RoundTrip`

验证什么：

- 图元 update packet 不会越界读。
- add / modify / remove 的顺序行为可预期。
- 2D 图元和 3D 实例的生命周期不会互相串门。

#### 4.6.5 `Renderx/src/c_api/render_c_api_overlay.cpp`

这个文件现在更像“统一 overlay 的薄门面”，方向是对的，但还可以更干净一点。

先改什么：

- `renderSubmitOverlay()` / `renderSubmitOverlays()` / `renderClearOverlays()` / `renderClearOverlayGroup()` 这组要成为唯一入口，别再回头引入老式 wrapper。
- `renderSetTexts()` 现在是直接走 `textAtlas.renderText()`，以后更合理的方向是把它收敛到 session 里的文本队列；第一步先保留兼容，但要标记这是过渡接口。
- `renderSetSceneEnv()` / `renderSetSceneEnvEx()` 要明确它们是“环境层几何设置”，不是普通业务图元。
- `renderSetBitmap()` / `renderClearBitmap()` 现在是预留接口，第一阶段至少要保持明确 no-op，不要留半成品行为。

先加什么测试：

- 新增 `Renderx/Test/OverlayAndSceneEnvTests.cpp`
  - `SubmitOverlay_ThenClearByKind`
  - `SubmitMultipleOverlays_OrderStable`
  - `SetSceneEnv_LayersArePackedCorrectly`
  - `SetSceneEnvEx_PixelAndTriangleFlagsCorrect`
  - `TextSubmission_UsesCurrentViewState`
  - `BitmapApis_AreSafeNoOps`

验证什么：

- overlay 的清理语义清楚。
- scene env 的层数据不会被打乱。
- 文本、环境、业务图元的职责不混。

#### 4.6.6 `Renderx/src/rhi/rhi_device.h`

这个接口现在已经够“能用”，但还不够“能平台化”。后面接 Vulkan / Metal 时，公有接口不能继续带着 OpenGL 的影子。

先改什么：

- 保持现在的命令式风格，但先补一个“能力查询”的入口，给上层做后端分支判断。
- 在接口层明确 `Null` backend 的存在，让“无图形能力”也能作为合法状态。
- 让公共接口尽量只表达抽象能力，不要把某个 API 的特性写死在方法名里。
- 如果后面要引入 offscreen / render target / fence / wait idle，也要从这里开始留口子。

先加什么测试：

- 目前先不用单独测接口方法，先把 `RenderTypesTests.cpp` 里的 backend 枚举和类型布局测试补齐。
- 后续新增 `Renderx/Test/RhiCapsTests.cpp`
  - `GLBackend_ReportsSupportedFeatures`
  - `NullBackend_ReportsMinimalFeatures`
  - `CapabilityNegotiation_RejectsUnsupportedPath`

验证什么：

- `rhi_device.h` 不再是“只对 OpenGL 有意义”的接口。
- 后续加 Vulkan / Metal 时，不用再倒着改上层调用面。

#### 4.6.7 `Renderx/src/rhi/rhi_gl.h` / `Renderx/src/rhi/rhi_gl.cpp`

这两个文件是现阶段真正落地的后端实现，也是你之后拆后端时最需要稳住的一层。

先改什么：

- `GLDevice` 先拆成三个清晰的小职责：
  - buffer/texture/pipeline 资源管理；
  - 帧状态与上下文状态；
  - shader / program 解析与创建。
- `createPipeline()` 里的 shader 名称映射要收口，别再在函数里堆一长串字符串比较。
- `initialize()` / `shutdown()` 要明确上下文和资源生命周期，保证多窗口多上下文时状态不会混。
- `mapBuffer()` / `unmapBuffer()` / `flushMappedRange()` 要把持久映射和普通映射的差异写清楚。
- `draw()` / `drawIndexed()` / `drawIndirect()` / `drawIndexedIndirect()` / `dispatchCompute()` / `memoryBarrier()` 这组最好以后统一走一个更细的 command layer，但第一步先保持行为稳定。
- `resize()`、`setClearColor()`、`enableDepthTest()`、`enableBlend()` 这类状态函数，最好都只影响当前 device，不要偷偷影响全局 GL 状态。

先加什么测试：

- 新增 `Renderx/Test/GLDeviceTests.cpp`
  - `CreateDestroy_Succeeds`
  - `CreateBuffer_UploadAndMap`
  - `CreateTexture_Upload`
  - `CreateGraphicsPipeline_BasicShaders`
  - `CreateComputePipeline_CullingShader`
  - `Resize_And_StateFlags_AreLocal`
- 这些测试直接复用 `TransientBufferPoolTests.cpp` 的最小 GL context 模式就够了。

验证什么：

- GL 后端能单独启动、单独退出、单独建资源。
- 多窗口情况下不会把一个窗口的状态带到另一个窗口。
- shader / program / buffer / texture 的生命周期收得住。

#### 4.6.8 `UI/2D/Src/Ui/ViewWidget/RenderWidget.cpp`

这个类现在已经是“UI + 场景编排 + overlay + 环境 + 文本 + 适配器”的混合体。它最需要做的是瘦身。

先改什么：

- `RenderWidget` 只保留四类职责：
  1. Qt 生命周期；
  2. 视图矩阵 / DPI / resize；
  3. 把数据转交给 RenderX；
  4. 接收来自上层的 overlay / scene 更新。
- 这几个 helper 最好从这个 cpp 里再拆出去，单独放到 UI 侧的轻量桥接文件里：
  - `submitLineListOverlay()`
  - `submitPointsOverlay()`
  - `submitRectOverlay()`
  - `submitFilledRectOverlay()`
  - `submitSnapIndicator()`
- `applyPreviewOverlay()` / `applySelectionOverlay()` / `applyPointMarkerOverlay()` / `applySnapIndicator()` 不要继续膨胀，建议收敛成一个 `OverlayUpdateBuilder` 或 `RenderOverlayBridge`。
- `submitSceneFromDataSource()` 和 `submitDefaultSceneEnv()` 这两段要继续保留“场景提交”和“环境提交”两条线，但尽量别让 UI 再承担渲染规则。
- `paintGL()` 最终理想状态应该只做三件事：
  1. dirty 检查；
  2. 必要时重提场景/文本；
  3. `renderFrame()`。
- `setSceneCommands()` / `addRenderEntity()` / `modifyRenderEntity()` / `removeRenderEntity()` 这组应该尽量改成“调用 RenderX 的统一场景接口”，不要再自己拼装过多转换逻辑。

先加什么测试：

- 新增 `UI/2D/Test/RenderWidgetStateTests.cpp`
  - `SetViewMatrix_MarksSceneEnvDirty`
  - `ResetView_UsesIdentityMatrix`
  - `MarkSceneEnvDirty_RequestUpdate`
- 新增 `UI/2D/Test/RenderWidgetOverlayTests.cpp`
  - `SelectionBox_ClearAndSet`
  - `SelectionHandles_ClearAndSet`
  - `SnapIndicator_ShowHide`
  - `InteractiveSelectionToggle_ClearsOverlay`
- 新增 `UI/2D/Test/RenderWidgetSceneEnvTests.cpp`
  - `SubmitDefaultSceneEnv_CachesRulerText`
  - `SceneEnvDirty_RebuildsOnNextPaint`
  - `SubmitSceneFromDataSource_RoutesGeometry`

验证什么：

- UI 不再直接承担过多渲染编排。
- 视图变化、环境脏标记、overlay 更新三条线互不打架。
- 多窗口 UI 状态不会交叉污染。

#### 4.6.9 `UI/2D/Src/Ui/ViewWidget/SceneGeometrySinkAdapter.cpp`

这个适配器是好东西，方向完全正确：它就是“文档几何 → RenderX”的桥。这里的要求是薄，越薄越好。

先改什么：

- 保持它只做 `Ut` 数据到 `render::GeometryPrimitive` 的转换。
- `emitPolyline()` / `emitPoint()` / `emitCircle()` / `emitArc()` / `emitEllipse()` / `emitText()` / `emitTextEx()` / `emitImagePlaceholder()` / `emitTriangleSoup()` 这几个函数里，尽量不要再加业务规则。
- `m_currentEntityId` 的消费规则要固定住：一次 emit 用一次，提交后立刻清零。
- `emitTriangleSoup()` 的 3D 路径和 2D 路径一定要分开看，别把它当“文档图元”的一部分继续扩展。
- 这里最适合做的事，是把点、线、文本、三角面都统一转成稳定 POD，然后交给 RenderX 的统一入口。

先加什么测试：

- 新增 `UI/2D/Test/SceneGeometrySinkAdapterTests.cpp`
  - `EmitPolyline_ConvertsPointsAndColor`
  - `EmitCircle_PropagatesRadiusAndColor`
  - `EmitArc_WrapAngleIsPreserved`
  - `EmitEllipse_FullEllipseAndRotation`
  - `EmitText_ConvertsAlignmentAndRotation`
  - `EmitTriangleSoup_PreservesVertexAndNormalCount`

验证什么：

- 适配器不会悄悄改数据。
- entity id 的传播规则稳定。
- 2D / 3D 图元进入 RenderX 的路径明确。

#### 4.6.10 `Main/Src/UI/Render/RenderWidget3DAdapter.cpp` 和 `UI/3D/Src/Render/RenderWidget3D.cpp`

这两块现在还是“旧 3D 路径 + 过渡适配层”的状态。短期目标不是推倒重写，而是别让它们继续长成第二套系统。

先改什么：

- `RenderWidget3DAdapter.cpp` 保持只做：
  - widget 创建 / 销毁；
  - 信号转发；
  - scene / camera 绑定；
  - 状态回调。
- `ensureWidgetCreated()` / `bindWidgetSignals()` / `initialize()` / `shutdown()` 这几个函数要继续保守，别往里塞更多业务。
- `RenderWidget3D.cpp` 里真正复杂的地方是：
  - `paintGL()`；
  - `renderEntities()`；
  - `renderSelectedEntities()`；
  - 鼠标 / 滚轮 / 键盘事件；
  - 这些都属于“旧 3D 渲染链路”，后面要逐步往统一 RenderX 会话模型迁移。
- 这一层现在的定位，应该是“过渡期兼容”，不是长期主干。

先加什么测试：

- 继续扩展现有 `Main/Src/UI/Test/RenderWidget3DAdapterTests.cpp`
  - `Initialize_WithNullIsSafe`
  - `Shutdown_DoubleCallSafe`
  - `SetScene_NullSafe`
  - `SetCamera_NullSafe`
  - `SelectionCallback_Wiring`
  - `Widget_ReturnsNullBeforeInit`
- 如果以后开始迁移 `RenderWidget3D.cpp` 到 RenderX，也建议先从“状态和事件”测起，不要一上来测整帧画面。

验证什么：

- 3D 过渡层不会变成新的架构债。
- 适配器和渲染 widget 的职责边界清楚。

#### 4.6.11 `Renderx/src/core/batch_queue.h` / `Renderx/src/core/batch_queue.cpp`

这个模块是 2D 大量图元渲染的关键。你后面要让“大图元量还能跑”，这里一定要先稳住。

先改什么：

- `initialize()` / `shutdown()` 只做资源准备和释放，不要继续掺杂业务状态。
- `submit()` 要把“可见索引、脏范围、版本号、全量重建判断”分开。
- `render()` 只做顶点上传、间接命令上传、提交，不要再夹带太多策略判断。
- `ensureIndirectCapacity()` 和 `mergeDirtyRanges()` 这类容量/脏区逻辑，最好是可单测的纯逻辑。

先加什么测试：

- 重做/修复 `Renderx/Test/BatchQueueTests.cpp`
  - `Submit_RebuildsOnGenerationChange`
  - `DirtyRanges_MergeAdjacent`
  - `DirtyRanges_KeepSeparatedWhenGapExists`
  - `IndirectCommands_AreStableByVisibilityOrder`
  - `Capacity_GrowsWhenNeeded`

验证什么：

- 批处理顺序稳定。
- 脏区合并规则稳定。
- 大图元量下不会因为队列逻辑乱掉。

#### 4.6.12 `Renderx/src/core/overlay_queue.h` / `Renderx/src/core/overlay_queue.cpp`

这个模块负责交互 overlay，最容易“能跑但难维护”。这里要强制把语义写清楚。

先改什么：

- 统一维护各类 overlay 的数据层，不要让 `setSelectionBox()`、`setSelectionHandles()`、`setPreviewLines()` 自己各写各的逻辑。
- `submitOverlay()` / `clearUnifiedOverlays()` / `clearOverlayKind()` 这三个是核心 API，优先保证它们的行为稳定。
- `render()` 只做最终组装与上传，不要把“构造 overlay 几何”再继续塞进去。
- `buildMarkerQuad()` / `buildMarkerBorder()` 这种几何小工具保持纯函数风格，方便测。

先加什么测试：

- 新增 `Renderx/Test/OverlayQueueTests.cpp`
  - `SetCrosshair_VisibleAndHidden`
  - `SetSnapIndicator_VisibleAndHidden`
  - `SubmitOverlay_StoresByKind`
  - `ClearOverlayKind_RemovesOnlyTargetKind`
  - `SelectionRect_BuildsFillAndBorder`
  - `Render_PreservesRangeMapping`

验证什么：

- 交互层的 overlay 清理语义不会互相误伤。
- 预览、选择框、手柄、捕捉点各自独立。

#### 4.6.13 `Renderx/src/core/mesh_manager.h` / `Renderx/src/core/mesh_manager.cpp`

这是 3D 实例化渲染的核心。当前最大的现实问题不是“能不能画”，而是 `MAX_INSTANCES = 512` 这种硬约束会卡住后期扩展。

先改什么：

- 先把 `MAX_INSTANCES` 这个硬限制替换成能力驱动或动态扩容策略。
- `registerMesh()` / `unregisterMesh()` 负责 mesh 生命周期。
- `addInstance()` / `modifyInstance()` / `removeInstance()` 负责实例生命周期。
- `queryVisible()` 和 `render()` 要明确谁负责 culling，谁负责 pack，谁负责 draw。
- `update()` / `uploadMeshBuffers()` / `uploadInstanceBuffer()` 这类函数建议逐步收敛成“脏了才上传”。

先加什么测试：

- 重做/修复 `Renderx/Test/MeshManagerTests.cpp`
  - `RegisterMesh_RoundTrip`
  - `UnregisterMesh_StopsRendering`
  - `AddModifyRemoveInstance_Lifecycle`
  - `QueryVisible_ReturnsExpectedIds`
  - `InstanceCount_Exceeds512StillWorks`
  - `DirtyUpload_RebuildsOnlyNecessaryBuffers`

验证什么：

- 3D 实例量上去以后不会被 512 这个值卡死。
- mesh / instance 的关系不会乱。

#### 4.6.14 `Renderx/src/core/persistent_entity_manager.h` / `Renderx/src/core/persistent_entity_manager.cpp`

这个模块方向很好，说明你们已经在往 GPU-driven 走了；但现在还没有完全摆脱 CPU-GPU 同步点。

先改什么：

- `initialize()` / `shutdown()` 里把 SSBO、visibility buffer、count buffer 的生命周期理顺。
- `addEntity()` / `removeEntity()` / `clearEntities()` / `updateEntity()` 要保证 dirty 状态清晰。
- `uploadChanges()` 是关键，后面要尽量做到增量上传，而不是每帧全量同步。
- `executeCulling()` 和 `generateIndirectCommands()` 这两步要明确 GPU / CPU 的边界。
- `ensureCullingPipeline()` 只负责 pipeline，不要把状态编排塞进去。

先加什么测试：

- 新增 `Renderx/Test/PersistentEntityManagerTests.cpp`
  - `InitializeShutdown_IsSafe`
  - `AddUpdateRemoveEntity_Lifecycle`
  - `UploadChanges_OnlyUploadsDirtyEntities`
  - `ExecuteCulling_ProducesVisibleMask`
  - `GenerateIndirectCommands_WritesCountAndCommands`

验证什么：

- 视见性剔除链路不会因为同步点把帧时间拖爆。
- 实体增删改的 dirty 路径稳定。

#### 4.6.15 `Renderx/src/core/render_graph.h` / `Renderx/src/core/render_graph.cpp`

现在它是线性 pass scheduler，不是真正的 dependency graph。这个结论要写死，避免以后误判。

先改什么：

- `PassDesc` 里的资源读写描述先保留，但要让 `execute()` 清楚地表示“线性执行，不做拓扑排序”。
- `addPass()` / `clear()` / `setPassEnabled()` / `execute()` 的行为必须稳定，不能依赖隐式全局状态。
- 如果以后要长成真正 frame graph，再另起一层；现在先把“线性调度器”做好。

先加什么测试：

- 新增 `Renderx/Test/RenderGraphTests.cpp`
  - `AddPass_ExecutionOrderIsStable`
  - `DisabledPass_IsSkipped`
  - `Clear_RemovesAllPasses`
  - `ExecutedPassCount_IsReportedCorrectly`
  - `ResourceSlots_ArePreserved`

验证什么：

- pass 顺序和启用/禁用行为是可预测的。
- 你们以后升级成真正 frame graph 时，能清楚区分“当前能力”和“未来能力”。

#### 4.6.16 `Renderx/src/core/scene_env.h` / `Renderx/src/core/scene_env.cpp`

场景环境层负责背景、网格、辅助线、标尺这些“不是业务图元但又一定要渲染”的内容。

先改什么：

- `setGeometry()` / `setGeometryEx()` 要把 layer 的颜色、线宽、pixel/world 坐标、zDepth 这些语义固定住。
- `render()` 的两个重载要继续保留，但最终应以一个统一的环境层数据模型为准。
- `EnvLayer` 的 packing 规则要稳定，不要每次 UI 一点变化就重新发明一遍。

先加什么测试：

- 新增 `Renderx/Test/SceneEnvTests.cpp`
  - `SetGeometry_LayersPackedCorrectly`
  - `SetGeometryEx_PixelAndTriangleFlags`
  - `Render_EmptyGeometryIsSafe`
  - `Render_LayerZDepthOrderStable`

验证什么：

- 背景/网格/标尺在不同窗口下语义一致。
- UI 侧不会把环境层当普通图元乱塞。

#### 4.6.17 `Renderx/src/core/text_atlas.h` / `Renderx/src/core/text_atlas.cpp`

这套是世界文字渲染。它和 `screen_text_renderer` 不是一回事，两个层次要分开。

先改什么：

- `initialize()` / `shutdown()` 把 font atlas 资源生命周期收干净。
- `loadFont()` 先稳定默认字体加载和 fallback 行为。
- `renderText()` 作为世界文字输出路径，要明确依赖 viewMatrix 和 viewport，不要默默吃 session 外状态。
- `rasterizeGlyph()` / `buildTextQuads()` 这两个尽量保持纯逻辑，方便测。

先加什么测试：

- 新增 `Renderx/Test/TextAtlasTests.cpp`
  - `LoadFont_DefaultFallbackIsSafe`
  - `RasterizeGlyph_CachesAndReuses`
  - `BuildTextQuads_UsesViewportAndViewMatrix`
  - `RenderText_EmptyInputIsSafe`

验证什么：

- 字体图集不会在窗口重建时失控。
- 世界文字布局是可重复的。

#### 4.6.18 `Renderx/src/core/screen_text_renderer.h` / `Renderx/src/core/screen_text_renderer.cpp`

这套是屏幕文字，和世界文字分工要彻底明确。

先改什么：

- `beginFrame()` / `submitText()` / `render()` / `clear()` 这条线要保持 frame-local，不要泄漏到别的窗口或别的帧。
- `loadFont()` 继续作为独立入口，但要和 `RenderSession` 生命周期绑定。
- `rasterizeGlyph()` 的缓存行为需要稳定，不要出现窗口切换后仍持有悬挂状态。

先加什么测试：

- 新增 `Renderx/Test/ScreenTextRendererTests.cpp`
  - `BeginFrame_ClearsSubmittedTexts`
  - `LoadFont_IsSafe`
  - `SubmitText_ProducesQuads`
  - `Render_EmptyFrameIsSafe`

验证什么：

- 屏幕文字不会跨帧残留。
- 屏幕文字和世界文字互不抢职责。

#### 4.6.19 这批测试文件怎么接进现有 CMake

现在 `Renderx/Test/CMakeLists.txt` 里只先编了 `TransientBufferPoolTests.cpp`，这是对的，因为它还在稳底座。

下一步建议按这个顺序放开：

1. 先把纯 CPU 测试挂进去：
   - `RenderTypesTests.cpp`
   - `RenderGraphTests.cpp`
   - `SceneEnvTests.cpp`
   - `TextAtlasTests.cpp`
   - `ScreenTextRendererTests.cpp`
2. 再挂带最小 GL context 的测试：
   - `RenderDeviceLifecycleTests.cpp`
   - `GLDeviceTests.cpp`
   - `PersistentEntityManagerTests.cpp`
   - `MeshManagerTests.cpp`
3. 最后再挂 UI / QWidget 级测试：
   - `RenderWidgetStateTests.cpp`
   - `RenderWidgetOverlayTests.cpp`
   - `SceneGeometrySinkAdapterTests.cpp`
   - `RenderWidget3DAdapterTests.cpp`

这样推进，回归风险最小，也最容易定位问题。

---

## 第五部分：不建议现在做的事情

> 两条文档的共识约束。

1. 不要把 RenderX 重写成完全不同的 API
2. 不要在没有 `RenderRuntime` 的情况下先乱拆 `RenderDevice`
3. 不要把 Vulkan / Metal 先当成"接口名"而不是"实现任务"
4. 不要让 `RenderWidget` 继续承担越来越多业务编排
5. 不要把多个窗口硬塞进一个全局渲染对象
6. 不要把 MacOS 兼容性继续建立在"OpenGL 4.6 能跑"的假设上
7. 不要把 OpenGL-only 当成跨平台方案
8. 不要一上来就把 OpenGL 包一层假 Vulkan

---

## 第六部分：风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|---|---|---|---|
| 重构破坏现有 API | 中 | 高 | 严格向后兼容测试，C API 不改动 |
| Vulkan/Metal 实现工作量过大 | 高 | 中 | 阶段五分步实施，先 Vulkan 后 Metal |
| 多窗口上下文切换性能问题 | 中 | 中 | 基准测试验证，延迟切换策略 |
| 着色器跨平台兼容性问题 | 高 | 高 | 使用 SPIR-V 作为中间表示 |
| 重构周期过长 | 中 | 中 | 严格按阶段推进，每阶段可交付 |
| 应用层 3D 收敛阻力 | 中 | 高 | 先定义统一接口，再逐步迁移 |
| 共享资源池引入依赖 | 中 | 中 | 默认隔离，可选共享 |

---

## 附录：关键设计决策建议

### 决策 1：是否保留 C API？

**建议**：保留 C API，但将其重构为"薄适配层"。
- C API 仅负责参数校验和类型转换
- 所有实际逻辑委托给 `RenderSession` 的 C++ 实现
- 好处：保持跨语言绑定能力，同时让 C++ 层更清晰

### 决策 2：如何管理多窗口上下文？

**建议**：采用"每个窗口一个 `RenderSession`"的模型。
- `RenderRuntime` 负责创建/销毁/切换会话
- 会话之间资源默认隔离，可选共享
- 类似于 OpenGL 的多 context 模型，但抽象得更清晰

### 决策 3：RHI 后端如何组织代码？

**建议**：每个后端独立目录，通过工厂模式创建。

```
src/rhi/
├── rhi_device.h          # IDevice 接口（不变）
├── rhi_gl.h / rhi_gl.cpp # OpenGL 后端
├── rhi_vk.h / rhi_vk.cpp # Vulkan 后端（新增）
├── rhi_mtl.h / rhi_mtl.cpp # Metal 后端（新增）
└── rhi_factory.h         # 后端工厂
```

### 决策 4：着色器如何管理？

**建议**：引入着色器源码抽象层。
- 定义 `ShaderSource` 结构，包含 GLSL/SPIR-V/MSL 源码
- 着色器编译逻辑在 RHI 后端内部
- 提供默认的 GLSL 源码（当前行为），新增后端提供对应源码
- 迁移到 `RenderRuntime` 管理

### 决策 5：如何保证重构期间不引入回归？

**建议**：
- 每个阶段完成后运行现有单元测试
- 新增集成测试验证多窗口和跨平台场景
- 使用 CI/CD 自动化测试（Windows + Linux + macOS）
- 保持 `RenderTypes` 和 `TessParams` 等公共头文件稳定

---

> **文档结束**。本合并文档旨在综合两份分析的各自优势，为 RenderX 框架的长期演进提供可操作的参考。建议按照优先级逐步推进，每阶段独立可验证、可回退。
