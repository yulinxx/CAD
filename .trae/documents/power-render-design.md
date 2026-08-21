# PowerRender — 下一代渲染 DLL 实现计划

> **文档状态（2026-08-02）**：设计方案，不是当前生产实现清单。
>
> 当前代码已使用 `Renderx` 相关路径，但本文对 `Render/NextGen`、`RenderX`、`RenderEngine` 等模块的比较和目标架构，不能直接解释当前运行时调用链。后续若决定采用 PowerRender，应另行记录迁移范围、兼容期和验收标准。

## Context（背景）

工程内同时存在 4 套并行渲染模块：

- `Render/NextGen/` — POD 契约 + C-API 干净，但只有单 GL46 VBO + 间接绘制，无空间索引/文字/Mesh
- `RenderX/` — VertexArena + 间接绘制，但无 QuadTree、无 Mesh 共享、无文字
- `Renderx/` — SlotMap + QuadTree + TextAtlas + MeshManager 全部具备，但 RHI 抽象厚、与业务耦合重
- `RenderEngine/` — Device/World/View 三层最清晰，但 QuadTree 简单、无间接绘制、无 Mesh/Text

任何一套都无法同时满足 **"百万级 2D 图元 + 按 ID 动态更新 + QuadTree 剔除 + 文字 + Mesh 共享"**。新建独立的 `PowerRender` DLL，把四家最优组件拼起来。

**用户强调**：
- 充分利用图元 ID 作为动态渲染的数据更新 → 用 `PREntityId + generation` 做稳定键
- 完整 CAD 版（不是最小骨架）
- 集成 QuadTree 视锥剔除 + 文字渲染（Text Atlas） + Mesh 共享/实例化
- **不集成**进主 CMakeLists（独立项目）
- **不包含** Overlay 类（preview/control/handles/box）—— 用户未选

## 目录结构

```
C:\Users\xx\Documents\Cpp\CAD\PowerRender\
├── CMakeLists.txt
├── README.md
├── PLAN.md                          ← 本文件
├── include\power_render\
│   ├── power_export.h               # PR_API dllexport 宏
│   ├── power_types.h                # POD：PREntityId/PRVertex/PREntityUpdate/...
│   └── power_render.h               # C API：prCreateDevice/World/View/...
├── src\
│   ├── core\
│   │   ├── slot_map.h               # port from Renderx
│   │   ├── arena.h                  # port from Renderx（小对象池）
│   │   ├── vertex_arena.h           # port from RenderX
│   │   ├── vertex_arena.cpp
│   │   ├── quad_tree.h              # port from RenderEngine
│   │   ├── quad_tree.cpp
│   │   └── power_log.h              # PR_LOG / PR_ASSERT
│   ├── text\
│   │   ├── text_atlas.h
│   │   ├── text_atlas.cpp           # port from Renderx，修复 renderText stub
│   │   └── stb_truetype_impl.cpp    # #define STB_TRUETYPE_IMPLEMENTATION
│   ├── mesh\
│   │   ├── mesh_manager.h           # port from Renderx + hash dedup
│   │   └── mesh_manager.cpp
│   ├── world\
│   │   ├── power_world.h            # 核心：图元 SlotMap + VertexArena + QuadTree
│   │   └── power_world.cpp
│   ├── view\
│   │   ├── power_view.h
│   │   └── power_view.cpp
│   ├── backend\
│   │   ├── power_backend.h          # IPowerBackend 接口
│   │   ├── gl46_backend.h           # OpenGL 4.6 实现
│   │   └── gl46_backend.cpp         # 持久映射 VBO + glMultiDrawArraysIndirect
│   ├── c_api\
│   │   ├── power_c_api.cpp          # C 入口
│   │   └── power_device.cpp         # PRDevice 实现
│   └── internal.h
└── demo\
    ├── CMakeLists.txt
    └── main.cpp                     # 1M 图元 + 文字 + 共享 Mesh
```

## 关键设计

### 1. POD 契约（`power_types.h`） — 零依赖、ABI 稳定
- `PREntityId = uint64_t`：外部业务 ID，**内部直接当 SlotMap Key**（`gen=high32 / sparse=low32`）
- `PRGeneration` 防止 ABA 重复
- `PRDirtyFlags { TRANSFORM | VERTICES | COLOR }` 位掩码
- `PREntityUpdate { op, id, gen, vertices*, count, prim, lineW, pointS, bbox, dirty }`
- `PRRenderStats` 包含 `frameTimeMs / cpuTimeMs / gpuTimeMs`
- `PRTextItem { text, x, y, fontSize, color[4], zOrder }`
- `PRMeshDesc { positions, normals, indices, count, bbox }`

### 2. C API（`power_render.h`） — 三层 + Mesh/Text
```
PRDevice ── GPU 上下文 + 窗口
PRWorld  ── 纯数据容器（SlotMap + VertexArena + QuadTree + TextAtlas + MeshManager）
PRView   ── 相机 + 视口，绑 1 个 World + 1 个 Device；多 View 可共享 1 个 World
```
- 图元按 ID 直接寻址：`prWorldModifyEntity(world, id, verts, n)` → 自动定位 VBO 稳定 offset
- 批量：`prWorldApplyUpdates(world, updates[], n)` → 一次 packet 提交
- 文字：`prWorldLoadFontDefault` + `prWorldSetTexts`
- Mesh 共享：`prWorldRegisterMesh` 用 hash(pos,nor,idx) 自动去重；`prWorldAddMeshInstance` 1000 个实例共享 1 个 VBO/IBO
- 帧：`prRenderFrame(view)` = Begin + RenderView + End + Present

### 3. SlotMap（`core/slot_map.h`）— 直接 port from Renderx
- 64-bit Key（高 32 = generation，低 32 = sparse index）
- 内部 `m_dense / m_dense_keys / m_sparse / m_freeList`
- `swap-with-last` 删除，`dense_data()` 暴露给遍历

### 4. VertexArena（`core/vertex_arena.h`）— 直接 port from RenderX
- bump + freelist，按 vertex 数分配稳定 VBO offset
- 改 vertex count → 旧 slot 进 freelist，分配新 offset
- `reserveHighWater` 指数扩容

### 5. QuadTree（`core/quad_tree.h`）— 直接 port from RenderEngine
- `kMaxEntities=64, kMaxDepth=8`
- `build(bboxes, count)`：根节点按所有 bbox 中心 + 1% padding
- `queryVisible(frustum[4], outVisible)`：栈式遍历，AABB 交叉测试
- World 在 `dirty` 时 `rebuild()`

### 6. PowerWorld（`world/power_world.cpp`）— 核心
```cpp
namespace pr {
class PowerWorld {
    pr::core::SlotMap<uint64_t, Entity> m_entities;   // key = (gen<<32 | sparse)
    pr::core::VertexArena               m_arena;      // 每个 entity 稳定 VBO offset
    pr::core::QuadTree                  m_quad;
    std::vector<PRDrawCmd>              m_drawCmds;   // 按 prim type 分桶
    std::unique_ptr<text::TextAtlas>    m_text;
    std::unique_ptr<mesh::MeshManager>  m_mesh;
public:
    void addEntity(PREntityId id, const PRVertex* v, uint32_t n,
                   PRPrimitiveType t, float lw, float ps);
    void modifyEntity(PREntityId id, const PRVertex* v, uint32_t n);
    void removeEntity(PREntityId id);
    void applyUpdates(const PREntityUpdate* u, uint32_t n);
    void setTexts(const PRTextItem* items, uint32_t n);
    PRMeshHandle registerMesh(const PRMeshDesc* d);    // hash 去重
    uint32_t     addMeshInstance(PRMeshHandle h, const float m[16]);
    Snapshot     buildSnapshot(const PowerView& v, float vw, float vh);
};
}
```
**关键点**：`modifyEntity` 通过 ID 找到 EntityEntry → 通过 `m_arena.allocate` 给新顶点申请稳定 offset → 旧 offset 进 freelist → 写脏标记。`buildSnapshot` 触发 `m_quad.rebuild()`（若 dirty），`m_quad.queryVisible()` 返回可见 EntityId 集合，再按 (prim type, material) 排序构造 `DrawCmd[]`。

### 7. PowerView（`view/power_view.cpp`）
- 持有 `PowerDevice&` + `PowerWorld&`
- 2D/3D 相机矩阵、视口、背景色
- `render()` = `world.buildSnapshot` → `backend.flush(snap)` → `backend.draw`

### 8. GL46Backend（`backend/gl46_backend.cpp`）— port from Render/NextGen + RenderX
- 单一大型 VBO（初始 1M 顶点）+ 持久映射（`glMapNamedBufferRange` + `MAP_PERSISTENT`）
- 增量上传：`glBufferSubData(offset, size, data)`，仅上传脏 entity
- 间接绘制：每帧构造 `DrawCmd[]` → `glNamedBufferSubData(INDIRECT)` → `glMultiDrawArraysIndirect`
- 文字/Mesh 独立 VBO + Program
- 7 个 prim type 共用一个 Program，按 `glLineWidth` / `glPointSize` 状态切换
- **跨平台注意事项（从 Renderx 修复中吸取的教训）**：
  - `glLineWidth` 须钳制到 `GL_LINE_WIDTH_RANGE` 范围；macOS CoreProfile 下该范围为 `[1, 1]`
  - 不应启用 `GL_LINE_SMOOTH`；与 MSAA 叠加会导致特定缩放级别下线段片段被覆盖率丢弃
  - 细分阶段应以 double 精度减去相机中心后再转 float（camera-relative），避免大坐标精度丢失

### 9. TextAtlas（`text/text_atlas.cpp`）— port from Renderx，**修复 stub**
- 2048×2048 RGBA8 纹理
- stb_truetype 烘焙 glyph → UV + 屏幕 quad
- LRU cache：`(codepoint, pixelHeight) → GlyphInfo`
- `buildQuads(items, n, view, outVerts)` 生成三角形列表
- 修复 Renderx 旧版 `renderText` 是空函数的问题

### 10. MeshManager（`mesh/mesh_manager.cpp`）— port from Renderx + 加 hash dedup
- 内部：`SlotMap<uint64_t, MeshEntry>` + 共享 `m_positions / m_indices` 全局池
- **新增**：`std::unordered_multimap<HashKey, PRMeshHandle>` 做几何去重
- `addInstance(h, m[16])` → 用 `m_freeInstances` 复用 ID
- 每帧 `queryVisible(view, proj)`：实例 AABB → NDC 包围盒测试，margin 0.05
- 渲染：按 meshId 排序，分组 `drawIndexed` 调用

## 关键参考文件

| 复用来源 | 文件 | 行数 |
|---|---|---:|
| SlotMap | [Renderx/src/core/slot_map.h](file:///C:/Users/xx/Documents/Cpp/CAD/Renderx/src/core/slot_map.h) | 153 |
| VertexArena | [RenderX/src/internal/vertex_arena.h](file:///C:/Users/xx/Documents/Cpp/CAD/RenderX/src/internal/vertex_arena.h) | 110 |
| QuadTree | [RenderEngine/src/core/quad_tree.h](file:///C:/Users/xx/Documents/Cpp/CAD/RenderEngine/src/core/quad_tree.h) | 166 |
| TextAtlas 头 | [Renderx/src/core/text_atlas.h](file:///C:/Users/xx/Documents/Cpp/CAD/Renderx/src/core/text_atlas.h) | 69 |
| TextAtlas 实 | [Renderx/src/core/text_atlas.cpp](file:///C:/Users/xx/Documents/Cpp/CAD/Renderx/src/core/text_atlas.cpp) | 209 |
| MeshManager 头 | [Renderx/src/core/mesh_manager.h](file:///C:/Users/xx/Documents/Cpp/CAD/Renderx/src/core/mesh_manager.h) | 75 |
| MeshManager 实 | [Renderx/src/core/mesh_manager.cpp](file:///C:/Users/xx/Documents/Cpp/CAD/Renderx/src/core/mesh_manager.cpp) | 458 |
| GL46 后端头 | [Render/NextGen/Src/GL46/Backend.h](file:///C:/Users/xx/Documents/Cpp/CAD/Render/NextGen/Src/GL46/Backend.h) | 176 |
| GL46 后端实 | [Render/NextGen/Src/GL46/Backend.cpp](file:///C:/Users/xx/Documents/Cpp/CAD/Render/NextGen/Src/GL46/Backend.cpp) | 718 |
| POD 契约 | [Render/NextGen/Include/RenderNext/Types.h](file:///C:/Users/xx/Documents/Cpp/CAD/Render/NextGen/Include/RenderNext/Types.h) | 192 |
| C-API 风格 | [Render/NextGen/Include/RenderNext/CAPI.h](file:///C:/Users/xx/Documents/Cpp/CAD/Render/NextGen/Include/RenderNext/CAPI.h) | 220 |
| Buffer 封装 | [Render/NextGen/Src/GL46/Buffer.h](file:///C:/Users/xx/Documents/Cpp/CAD/Render/NextGen/Src/GL46/Buffer.h) | 142 |
| C-API 调度样例 | [RenderX/src/c_api.cpp](file:///C:/Users/xx/Documents/Cpp/CAD/RenderX/src/c_api.cpp) | — |
| C++ RHI 类型 | [RenderEngine/src/rhi/rhi_types.h](file:///C:/Users/xx/Documents/Cpp/CAD/RenderEngine/src/rhi/rhi_types.h) | 84 |
| 独立 CMake 样例 | [RenderX/CMakeLists.txt](file:///C:/Users/xx/Documents/Cpp/CAD/RenderX/CMakeLists.txt) | 105 |

## 实施步骤

按依赖顺序逐层创建，每层独立可编译：

1. **L0 头文件** — 创建 `include/power_render/{power_export.h, power_types.h, power_render.h}`
2. **L1 容器** — 创建 `src/core/{slot_map.h, arena.h, vertex_arena.h, quad_tree.h, power_log.h}`
3. **L2 数据结构** — 实现 `src/core/{vertex_arena.cpp, quad_tree.cpp}`
4. **L3 资源** — 创建 `src/text/{text_atlas.h, text_atlas.cpp, stb_truetype_impl.cpp}` + `src/mesh/{mesh_manager.h, mesh_manager.cpp}`
5. **L4 领域类** — 创建 `src/world/{power_world.h, power_world.cpp}` + `src/view/{power_view.h, power_view.cpp}` + `src/backend/{power_backend.h, gl46_backend.h, gl46_backend.cpp}`
6. **L5 C 桥接** — 创建 `src/internal.h` + `src/c_api/{power_c_api.cpp, power_device.cpp}`
7. **L6 验证** — 创建 `CMakeLists.txt` + `demo/{CMakeLists.txt, main.cpp}`，独立构建测试

## 验证

### 编译验证
```bash
cd C:\Users\xx\Documents\Cpp\CAD\PowerRender
mkdir build && cd build
cmake -DPR_BUILD_DEMO=ON ..
cmake --build . --config Release
```
预期：`PowerRender.dll` + `demo.exe`，无 LNK2019 / C1083 错误。

### 功能冒烟（`demo/main.cpp`）
- 创建 1024×768 OpenGL 窗口（GLFW）
- `prWorldAddEntity` × 100,000 条 `PR_PRIM_LINES`（每条 2 顶点）
- `prWorldSetTexts` × 1 项
- `prWorldRegisterMesh(立方体)` + `prWorldAddMeshInstance` × 1000
- 每帧：随机改 100 个 entity 顶点（验证 dynamic update by ID）→ `prRenderFrame(view)`
- 检查 `prGetStats().frameTimeMs < 8ms`（GTX 1060 基线）

### 单元测试（可选 / 后续 PR）
- `tests/test_slot_map.cpp`：generation 去重、ABA、swap-erase
- `tests/test_vertex_arena.cpp`：alloc/dealloc 合并、bump 扩容
- `tests/test_quad_tree.cpp`：边界交叉、深度上限、可见性

## 估计行数

| 类别 | 行数 |
|---|---:|
| 头文件 (3 个) | 360 |
| core/ 容器 (5 个) | 540 |
| text/ (3 个) | 350 |
| mesh/ (2 个) | 400 |
| world/ (2 个) | 570 |
| view/ (2 个) | 190 |
| backend/ (3 个) | 630 |
| c_api/ (2 个) | 450 |
| demo/CMake/build (3 个) | 290 |
| **合计** | **~3800** |

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| GLEW 找不到 | `find_package(GLEW QUIET)` 找不到则 `PR_NO_GLEW` 用 wglGetProcAddress 退化 |
| OpenGL 4.6 不可用（macOS 4.1） | IPowerBackend 抽象，Vulkan/Metal 后续；2D path 最低 3.3 |
| macOS 线宽钳制为 1px | macOS CoreProfile 下 `GL_LINE_WIDTH_RANGE=[1,1]`；粗线需 geometry shader 或 triangle strip 模拟 |
| GL_LINE_SMOOTH + MSAA 冲突 | 不启用 `GL_LINE_SMOOTH`，MSAA 已提供足够抗锯齿（Renderx 已验证） |
| 大坐标精度丢失 | 细分阶段 double 精度减去相机中心后再转 float（Renderx 已实现） |
| QuadTree 重建慢 | V1 接受全量重建；V2 增量（dirty bbox 复用） |
| stb_truetype 单 TU 膨胀 | `#define STB_TRUETYPE_IMPLEMENTATION` 仅在 `stb_truetype_impl.cpp` |
| PowerWorld 组件多 | text/mesh 用 `unique_ptr` 默认 nullptr，2D 场景不强制使用 |

## 决策摘要

1. **`PREntityId` 直接当 SlotMap Key** — 业务 ID 64-bit，内部高 32 = gen，低 32 = sparse idx
2. **VertexArena 给每个 entity 稳定 VBO offset** — 改顶点仅 `glBufferSubData(offset, count)`
3. **Snapshot 模式** — World 与 Backend 通过 POD `Snapshot` 解耦，多线程友好
4. **Backend 接口最小化** — 仅 `init/shutdown/beginFrame/flushWorld/endFrame/stats`
5. **C 头零依赖** — `power_types.h` 只 include `<cstdint>`，与 Render/NextGen 同等级
6. **Hash dedup 共享 Mesh** — CAD 大量重复几何（孔型/板料）只存 1 份
