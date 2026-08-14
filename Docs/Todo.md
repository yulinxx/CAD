# Todo

## SceneEnvGeometryDesc — 让 Renderx 直通消费 SceneEnvGeometry

> **状态：✅ 已完成（2026-08-12）** — 方案已完成 ABI 边界复核修订与全链路落地（Renderx 类型 → C API → 核心实现 → UI2D 调用点迁移 → UI/Common 死代码清理 → 文档同步）。
> 最终变更摘要见 `Docs/03-渲染主链/渲染管线.md`「场景环境直通提交改造（2026-08-12）」；下文保留原始方案与修订过程供追溯。

### 现状（问题）

`RenderWidget` 消费 `Eg::SceneEnvGeometry` 时，手动拆散为平行数组后传给 C API `renderSetSceneEnvEx`。

**调用方实际有两处**（都在 `UI/2D/Src/Ui/ViewWidget/RenderWidget.cpp`），不是一处：

| 调用点 | 位置 | 顶点 z 处理 |
|--------|------|------------|
| `submitSceneFromDataSource()` | ~951 行 | `usePixelCoords ? layer.zDepth : 5.0f` |
| `submitDefaultSceneEnv()` | ~1054 行 | `layer.zDepth`（直接用层深度） |

两处各自重复约 50 行拆包胶水，且对 world 层 z 的处理不一致。

**问题**：

1. **纯胶水代码（×2）**：两处拆包逻辑（各 ~50 行）完全是格式转换，无业务逻辑。`Eg::SceneEnvLayer`（Engine 侧）与 `render::EnvLayer`（Renderx 侧）字段几乎一样但字段名不同，被迫手动映射。
2. **两套几乎相同的结构体**：Engine 侧 `SceneEnvLayer`（`Engine2D/Include/Engine2D/Environment/SceneEnvLayer.h:17-25`）和 Renderx 侧 `EnvLayer`（`Renderx/src/core/scene_env.h:101-110`）通过 RenderWidget 手动转换。
3. **`layerColors[]` 是"写了但未读"的死数据**：`renderSetSceneEnvEx` 把层颜色数组拆进 `EnvLayer.color[4]` 后，**shader 从不读取**——`scene_2d.frag` 只采样逐顶点 `vColor`，渲染实际颜色完全由每顶点 RGB 决定（从 `layer.color` 展开写入）。
4. **颜色编码散落各处**：`Eg::Color` → `uint32_t`（打包）→ `float[4]`（拆包）→ 逐顶点 RGB，链路太长且层色分支实际无效。

**额外事实（影响方案决策）**：

- **工程里还有第三套同名 `SceneEnvGeometry`**：`UI/Common/Include/Render/RenderTypes.h:299` 定义了独立的 `Render::SceneEnvGeometry`（挂在 `RenderFrameUpdate::sceneEnvGeometry`），但 `RenderWidget::applyFrameUpdate()` 只消费 `hasViewMatrix`/`hasSceneCommands`，**从未消费 `hasSceneEnvGeometry`**——是死代码。建议本次顺带处理（接线或删除），避免三套结构重名。
- **Python 绑定不存在**：`PyBindCore`/`Python/`/`PythonHost` 对 `renderSetSceneEnv*` 零引用（已扫描确认），影响面较原评估低。

### 为什么现在没做

- 改动需要 Engine 侧（`SceneEnvGeometry`）、Renderx 侧（`SceneEnv`）、C API 层（`render.h`）、UI 侧（`RenderWidget`）四层联动。
- C API 属于对外公开符号，新增需配合兼容性评估（本次采用"只新增、不删除"策略规避）。
- 建议作为独立 PR，配合 ABI 兼容性评估一起做。

### 应该怎么做

> **执行状态：以下步骤已全部完成。** 落地明细见"最终变更摘要"。

#### 方案：`SceneEnvGeometryDesc`（纯 Renderx POD）+ 标尺文字继续走 `renderSetScreenTexts`

**三条设计原则**（本次修订的核心）：

1. **描述符必须是纯 Renderx POD**：`render_types.h` 不得 include/引用任何 `Eg::*` 类型（尤其不能出现 `Eg::RulerTextItem`——它继承自含 `std::string` 的 `TextItem`，会破坏 Renderx 的零依赖 C ABI 边界）。
2. **文字与几何解耦**：标尺文字不塞进几何描述符，继续走既有 `renderSetScreenTexts(const ScreenTextItem*, count)` 通道——`ScreenTextItem`（`render_types.h:602`）已是纯 POD（`const char* text` + float 坐标/颜色/字号）。
3. **一次转换归位**：`Vec2f` → `VertexP3C3` 的逐顶点转换（含 z、不存在的层色填充）下沉到 Renderx 内部完成，UI 侧不再做任何格式适配。

```cpp
// render_types.h —— 纯 Renderx POD，零 Engine 依赖

/// 场景环境单层描述（直接映射 Engine 侧 SceneEnvLayer 字段）
struct EnvLayerDesc
{
    const float* vertices;      // 顶点坐标对（x0,y0, x1,y1, ...），直接透传 Engine 的 Vec2f/float2 数据
    uint32_t     vertexCount;   // 顶点数（坐标对个数，非分量数）
    float        color[4];      // 整层颜色 RGBA（Renderx 内部填充到该层每个顶点 RGB）
    float        lineWidth;     // 线宽（像素）
    uint8_t      usePixelCoords;// 是否像素坐标
    uint8_t      asTriangles;   // 是否三角形拓扑
    float        zDepth;        // 深度（层间排序）
};

/// 场景环境几何直通描述符
struct SceneEnvGeometryDesc
{
    const EnvLayerDesc* layers;     // 层数组
    uint32_t            layerCount; // 层数
};
// 标尺文字不在此描述符内 —— 继续走 renderSetScreenTexts(ScreenTextItem*, count)
```

> 说明：`vertexCount=0` 的层跳过；`vertices` 可直接 `reinterpret_cast` 自 `std::vector<Ut::Vec2f>` 的 `.data()`（`Ut::Vec2f` 为 `{float x, y}` POD），调用期间 `envGeo` 存活即可（Renderx 在调用内同步拷贝，不持有悬垂指针）。

#### 改动步骤

1. **`render_types.h`**：新增 `EnvLayerDesc` + `SceneEnvGeometryDesc`（纯 POD，无 Engine 依赖）。✅
2. **`render.h`**：新增 `renderSetSceneEnvDirect(RenderDevice*, const SceneEnvGeometryDesc*)`。✅
3. **`SceneEnv`**：新增 `setGeometryDirect(const SceneEnvGeometryDesc*)`——内部完成 `Vec2f→VertexP3C3` 组装（填充逐顶点 `layer.color RGB` 与 `zDepth`）、firstVertex/vertexCount 换算、z 排序所需的 `EnvLayer` 构建。✅
4. **`RenderWidget`**：`submitDefaultSceneEnv()` 与 `submitSceneFromDataSource()` **两处**统一改为填充 `SceneEnvGeometryDesc` 并调用新 API；删除平行数组组装胶水；**统一 z 语义**（world 层不再特判 `5.0f`，统一为层 `zDepth`）。✅
5. **标尺文字**：维持现状，两处都继续走 `renderSetScreenTexts`（已确认 `submitSceneFromDataSource` 无缺口）。✅
6. **废弃路径**：保留 `renderSetSceneEnvEx` 供外部兼容，标记 `deprecated`。✅
7. **清理死代码**：已删除 `UI/Common/RenderTypes.h` 的 `sceneEnvGeometry` / `hasSceneEnvGeometry`。✅
8. **文档更新**：`Docs/03-渲染主链/渲染管线.md` 已补「场景环境直通提交改造（2026-08-12）」记录。✅

#### 影响面评估
> 实际影响面：Renderx + UI2D + UICommon 三处改动，编译验证 SanYiRender / UI2D / SanYiCAD 均通过；`renderSetSceneEnvEx` 曾因漏写分号触发 C2144，已修复。

| 影响范围 | 风险 | 说明 |
|---------|------|------|
| C API 新增函数 | 低 | 只新增不删除，现有 `renderSetSceneEnvEx` 保留并标记 deprecated |
| RenderWidget 两处调用 | 中 | 两处拆包统一为一个填充路径，删除胶水的同时需修复 z 语义差异 |
| Renderx `SceneEnv` | 中 | `setGeometryDirect` 承担顶点格式转换，需覆盖三角/线/像素坐标/深度排序全分支 |
| Python 绑定 | 低 | 已确认工程中 `_sanyi_core`/`Python`/`PythonHost` 对 `renderSetSceneEnv*` 零引用（原"中"调低） |
| 标尺文字 | 低 | 继续走 `renderSetScreenTexts`，职责解耦，无 ABI 风险 |
| ABI 兼容性 | 低 | 新增函数不影响现有符号；`render_types.h` 零新增 Engine 依赖 |

#### 参考文件

- Engine 侧结构：`Engine2D/Include/Engine2D/Environment/SceneEnvLayer.h`
- Renderx 侧结构：`Renderx/src/core/scene_env.h:101-110`
- 当前拆包逻辑（两处）：`UI/2D/Src/Ui/ViewWidget/RenderWidget.cpp:901-960`（submitSceneFromDataSource）与 `1000-1087`（submitDefaultSceneEnv）
- 当前 C API：`Renderx/include/render/render.h:396`（renderSetSceneEnvEx）
- 当前实现：`Renderx/src/core/scene_env.cpp:99-137`（setGeometryEx）
- 文字通道：`Renderx/include/render/render.h:316` + `render_types.h:602`（ScreenTextItem）