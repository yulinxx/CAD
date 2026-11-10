# 三维浮雕雕刻（Mesh Relief Engraving）

> 状态说明（2026-11-10）　状态：现行　维护：架构组

本文描述 3D 工作台下 `Algorithm → Mesh Relief Engraving...` 的实际实现：把网格按 Z 分层切片，逐层生成等高轮廓与扫描线填充刀路，在视口叠加预览，并可导出 G-code / JSON。该功能**不经过** `AlgorithmTask` 框架，由服务直接开对话框（见 §1、§2）。全文以源码为准（读取时间 2026-11-08）。

## 1. 菜单入口与命令注册

| 项 | 值 | 证据 |
|---|---|---|
| 菜单 | `Algorithm → Mesh Relief Engraving...`（父菜单 id `algorithm`，`workbenches` 为 `["3D"]`） | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L224-L224) |
| 菜单项 id | `algo.relief_engraving_mesh` | 同上 |
| command id | `algo.relief_engraving_mesh`（同名） | 同上 |
| 快捷键 | `Ctrl+Shift+E` | 同上 |
| tooltip | `Generate a relief engraving toolpath from a 3D mesh height field` | 同上 |
| `OperationId3D` | `Algo_ReliefEngraving`（枚举 `MENU_3D_ALGO_BASE + 1`） | [MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L83-L84) |
| 目录标签 | `3D Relief Engraving...`（与菜单标签不同） | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L248-L255) |
| 目录图标 | `:/ui/common/Icons/Actions/cut.svg` | 同上 |
| 目录标志 | `kMenuShortcut` + `Cmd::RequiresSelection` | 同上 |
| 处理器注册 | `AlgorithmOperations3D` 内 `#ifdef ENABLE_ENGRAVING` 包裹的 `LambdaOperation3D` | [AlgorithmOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperations3D.cpp#L83-L94) |

要点：

- 目录条目**不在** `ENABLE_NESTING` 之类的条件块内（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L246-L256)）；真正被 `ENABLE_ENGRAVING` 条件编译的是操作处理器的注册（L83、L94）。`ENABLE_ENGRAVING` 由 `UI/3D/CMakeLists.txt` 无条件定义（[CMakeLists.txt](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/CMakeLists.txt#L174-L174)），因此正常构建中菜单可用。
- `Cmd::RequiresSelection` 让菜单项在有选中时可用；但服务自身的取网格逻辑在“无选中”时会回退到全场景（§2、§6）。
- 该功能**没有**走 `AlgorithmTask` 框架：`AlgorithmTaskId` 中虽定义了 `ReliefEngraving3D`（[AlgorithmTaskId.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Algorithm/AlgorithmTaskId.h#L28-L28)），`AlgorithmOperationMapping3D` 也给出 `Algo_ReliefEngraving → ReliefEngraving3D` 的映射（[AlgorithmOperationMapping3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperationMapping3D.cpp#L16-L17)），但 `AlgorithmTaskRegistration3D::registerAll` 只注册 `NestingFromMesh3D`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L397-L403)）。该映射是死配置，若真按任务分发会因找不到 handler 而失败。

## 2. 调用链路

### 2.1 打开对话框与准备网格

1. 菜单点击 → `algo.relief_engraving_mesh` → `OperationBus3D`。
2. `AlgorithmOperations3D` 的 lambda 直接调用 `ReliefEngravingService3D::instance().showDialog(ctx)`，返回 `success = true`、`phase = Completed`、消息 `3D relief engraving workflow opened`（[AlgorithmOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperations3D.cpp#L84-L93)）。
3. `ReliefEngravingService3D::gatherTargetMeshes`：先收集当前选中且 `isValid()` 的 `AprMeshEntity`；`onlySelected` 为假且无选中时，用 `sceneManager->forEachEntity` 遍历全场景（[ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L29-L68)）。
4. `showDialog` 若网格为空，弹 `Please import or select a mesh model first.`，标题 `3D Relief Engraving`；否则设置 `useSelectedMeshOnly`（有选中即 `true`），调用 `m_processor.configure(meshes, params, &error)`（[ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L70-L92)）。
5. `ReliefEngravingProcessor3D::configure`：`EngravingMeshBridge3D::fromMeshEntities` 合并三角网格；`applyBoundingBoxToParams` 求 bbox；仅当参数看起来是默认 `[0,1]` 时才用 bbox 覆盖 XY/Z 范围；随后 `syncVolumeProcessor()` 与 `computeLayerHeights()`；层高为空则 `Unable to compute layer heights`（[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L73-L168)）。
6. `syncVolumeProcessor` 把 `toEngravingParams()` 结果、裁剪框、加工模式送入 `VolumeProcessing`；`StockRemoval` 模式下若 `m_stockMesh` 为空，则用 `makeBboxStockMesh` 生成“零件 bbox 外扩 2 mm、Z 向下 5 mm”的六面体毛坯（[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L270-L307)）。
7. 服务创建（或复用）`ReliefEngravingDialog3D`，构造参数为 `dynamic_cast<UI3D::IToolpathOverlaySink3D*>(m_renderWidget)` 与 `&m_processor`，`syncFromProcessor()` 后 `show()/raise()/activateWindow()`（[ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L94-L124)）。

### 2.2 参数变更、分层预览与生成

1. 对话框改动任意参数 → `collectParams()` 从控件读出一份新 `ReliefEngravingParams3D`（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L126-L180)）。
2. `onPreviewLayer` → `processor.previewLayer(layerIdx, &error)`（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L818-L841)）。
3. `previewLayer` 先 `syncVolumeProcessor()`，再 `generateTaggedLayerToolpath(z, layerIndex)`，`z` 取 `job.layerHeights[layerIndex]`（[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L209-L224)）。
4. `onGenerate` → `processor.generateFull(&error)`（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L860-L892)）。
5. `generateFull` → `syncVolumeProcessor()` → `VolumeProcessing::generateTaggedToolpath()`，把 `paths/kinds/layerIndices` 搬进 `job`；失败时透传网格预检错误（[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L226-L266)）。
6. 生成成功后刷新统计、`emit sigJobGenerated`、再预览当前层，并弹 `ReliefProcessWorkflow3D::promptNextStep`（[ReliefProcessWorkflow3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefProcessWorkflow3D.cpp#L36-L59)）。

### 2.3 导出

`onExport` 以扩展名判定格式：`.nc`/`.gcode` → `ReliefToolpathExporter3D::exportSimpleGcode`，否则 `exportJson`（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L894-L926)）。服务层 `exportToolpath` 逻辑相同（[ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L127-L162)），且先检查 `job().valid && !toolpaths.empty()`（[ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L164-L167)）。

## 3. 输入与参数

### 3.1 输入形态

- 源数据：`std::vector<Eg::AprMeshEntity*>`，经 `EngravingMeshBridge3D::fromMeshEntities` 合并为 `Engraving::TriangleMesh`（三角形列表）。
- 目标收集：有选中 → 只用选中；无选中 → 全场景有效网格（§2.1 步骤 3）。
- 毛坯：`ReliefEngravingParams3D::stockMeshId` 只被记录，**从未按 ID 解析**；`setStockMeshEntity` 有实现但全仓库无调用点（§6）。

### 3.2 参数与默认值

权威定义：[ReliefEngravingParams3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Relief/ReliefEngravingParams3D.h#L30-L76)。

| 参数 | 默认值 | 单位/范围 | 对话框控件 |
|---|---|---|---|
| `baseZ` | 0.0 | mm，-10000–10000 | `Base Z` 默认 0，步长 0.01 |
| `maxZ` | 1.0 | mm，同上 | `Top Z` 默认 1，步长 0.01 |
| `minX` / `maxX` | 0.0 / 1.0 | mm，同上 | 默认 0/1，步长 0.1 |
| `minY` / `maxY` | 0.0 / 1.0 | mm，同上 | 默认 0/1，步长 0.1 |
| `layerHeight` | 0.05 | mm，0.001–10 | 默认 0.05，步长 0.01 |
| `laserPower` | 80.0 | %，0–100 | `Laser power`，显示 ` %` |
| `scanSpeed` | 800.0 | mm/min，1–10000 | 步长 10 |
| `spotSize` | 0.1 | mm，0.01–5 | 步长 0.01 |
| `overlapRatio` | 0.5 | 0–0.95 | 步长 0.05，小数 2 位 |
| `rasterAngle` | 0.0 | deg，-180–180 | 显示 ` deg` |
| `numLayers` | 0 | 只读派生 | 对话框写入后被引擎丢弃（§6） |
| `processMode` | `ContourAndFill` | `ContourOnly`/`FillOnly`/`ContourAndFill` | 组合框 3 项 |
| `machiningMode` | `SolidCut` | `SolidCut`/`StockRemoval` | 组合框 2 项 |
| `stockMeshId` | 0 | 0–999999 | 0 显示 `Use bbox as stock` |
| `materialPreset` | `wood` | `wood`/`metal`/`acrylic`/`leather`/`glass` | 默认 Wood |
| `bottomUp` | `false` | false=自上而下 | 组合框索引 0 |
| `useSelectedMeshOnly` | `false` | bool | 复选框，按选中状态回填 |
| `showContourPreview` / `showFillPreview` | `true` / `true` | bool | 预览过滤 |
| `showTravelMoves` | `false` | bool | 预览过滤 |
| `enableLeadInOut` | `false` | bool | `Enable lead-in / lead-out` 未勾选 |
| `leadInLength` / `leadOutLength` | 1.0 / 0.5 | mm，0–100 | 未勾选时禁用 |
| `leadInRampAngle` | 0.0 | deg，0–80 | 未勾选时禁用 |
| `enablePathOrdering` | `false` | bool | `Optimize path order` 未勾选 |
| `passesPerLayer` | 1 | 1–100 | `Passes per layer` |
| `adaptiveLayerHeight` | `false` | bool | `Adaptive layer height` 未勾选 |
| `minLayerHeight` | 0.01 | mm，0.001–10 | 未勾选时禁用 |
| `adaptiveCuspHeight` | 0.01 | mm，0.001–10 | 未勾选时禁用 |

对话框控件创建位置见 [ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L300-L475)；`syncFromProcessor` 逐项回填 `job.params`（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L228-L266)）。

### 3.3 参数映射与材料预设

`toEngravingParams`（[ReliefEngravingParams3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingParams3D.cpp#L9-L53)）：

- 直传：功率、速度、光斑、层高、重叠率、栅格角、`bottomUp`、lead-in/out、`enablePathOrdering`、道次、自适应三参数；
- `numLayers` **强制置 0**（L20），层数永远由引擎按 `layerHeight` 重算；
- `useRaster = processMode != ContourOnly`（L22）；
- 三种模式下的 `includeContour/includeFill` 组合（L24-L39）。

材料预设 `applyMaterialPreset` 只改 `layerHeight/laserPower/scanSpeed/overlapRatio`，数值来自 `DefaultStrategy::adjustParams`（[ReliefEngravingParams3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingParams3D.cpp#L55-L75)、[LaserStrategy.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/LaserStrategy.cpp#L39-L54)）：

| 预设 | 功率 | 速度 | 层高 |
|---|---|---|---|
| `wood` / 木材 | `min(80, P)` | `S × 0.8` | 不变 |
| `metal` / 金属 | `min(100, P × 1.2)` | `S × 0.6` | `L × 0.5` |
| `acrylic` / 亚克力 | `min(90, P)` | `S × 1.1` | 不变 |
| `leather` / `glass` | 无分支，保持原值 | 保持原值 | 保持原值 |

## 4. 算法原理与分步流程

### 4.1 数据结构、单位与坐标

- 长度单位 mm，速度 mm/min（导出 `F`），角度 deg，功率 %；内部 `ToolpathPoint` 记录 `x/y/z/power/speed`。
- 坐标：Z 轴向上，层切平面为 `Z = const`；XY 裁剪框与 Z 范围来自 bbox（除非用户显式改过）。
- `TaggedToolpathList` 在保存 `paths` 的同时保存等长的 `kinds`（Contour/Fill/Travel）与 `layerIndices`。

### 4.2 编号步骤

1. **建网格与范围**：合并 bbox；`applyBoundingBoxToParams` 给出 `[minX,maxX]×[minY,maxY]×[baseZ,maxZ]`；只有当入参仍是默认 `[0,1]` 时才覆盖，否则保留用户值（[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L117-L155)）。
2. **网格预检**：构造临时 `AprMeshEntity` 并 `markDirty()` 触发 BVH，调用 `Geo3DAnalysis::isWatertight/isManifold/isSelfIntersecting`；只写 `m_lastValidation.errorMessage`，**不阻断生成**（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L331-L379)）。
3. **层高序列**：`computeLayerHeights` 先取 Z 范围（`StockRemoval` 且有毛坯时用毛坯 Z 范围）；`numLayers > 0` 时按层数均分；否则：
   - 自适应开启 → `computeAdaptiveLayerHeights`（按 `cusp = 层厚 × |n_z|` 反推允许层厚 `cusp/|n_z|`，以 `layerHeight` 为上限、`minLayerHeight` 为下限），失败或为空则回退均匀；
   - 均匀 → `ceil(zRange / layerHeight)` 层；
   - 每层切片 Z 取 `slabStart + max(0.05×层厚, 1e-5)`，即“层底面略内缩”，避免落在退化边界（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L484-L561)、[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L75-L145)）。
4. **逐层切片**：`MeshSlicer::sliceMeshAtZ(mesh, z)` 得线段集合；`connectSegmentsToContours(segments, 1e-4f, false)` 焊接成环（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L677-L678)）。
5. **轮廓刀路**：`includeContour` 时每条环按顶点生成刀路，末尾补回首点闭合并套用 `simplifyPath` + `applyLeadInOut`；点数 `< 3` 的环跳过（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L683-L703)）。
6. **填充刀路**：`includeFill` 且轮廓非空时 `fillContours(contours, z)`；`useRaster=true` 走扫描线，否则走轮廓平行（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L646-L656)）。
7. **线距**：`calculateLineSpacing` 返回 `spotSize × (1 - overlapRatio)`，默认 `0.1 × 0.5 = 0.05 mm`（[LaserStrategy.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/LaserStrategy.cpp#L10-L13)）。
8. **填充实现**：扫描线在旋转 `rasterAngle` 后的坐标中按固定线距生成平行线，与轮廓求交得到线段，再旋转回原坐标；轮廓平行则用 `contourParallelFillLoops(contours, lineSpacing, 1000.0)`，逐圈内缩，每圈单独闭合（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L607-L644)）。
9. **裁剪**：`VolumeProcessing::fillContoursScanline` 把 `m_minX/m_maxX/m_minY/m_maxY` 传给 `MeshSlicer::fillContoursScanline`；后者取“轮廓 bbox 与传入 bbox 的交集”作为扫描范围，实现局部加工（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L580-L598)、[MeshSlicer.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/MeshSlicer.cpp#L918-L950)）。
10. **简化**：`simplifyPath` 默认容差 0.01 mm，去掉共线点（[VolumeProcessing.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Include/Engraving/VolumeProcessing.h#L154-L154)）。
11. **层内排序**：`enablePathOrdering` 时以 `(0,0,z)` 或上一层末点为锚做最近邻排序（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L723-L726)）。
12. **层间顺序与扫描方向**：`layerIndex = bottomUp ? i : N-1-i`，但 `z` 始终取对应下标的真实层高；奇数层 `rasterAngle + 90°`（偶数层原角度），函数结束恢复原角度（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L885-L901)）。
13. **多道次与空走**：每层按 `passesPerLayer` 重复加工（最后一次复用数据，不拷贝）；每条路径前由 `appendTaggedWithTravel` 插入 `Travel` 空走；空层直接跳过（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L903-L934)）。
14. **引入/引出**：开启后沿路径起始方向反向外延 `leadInLength`，该点 `power = 0`；斜坡角 > 0 时 `z = start.z + leadInLength × tan(ramp)`，形成抬高进入（[LeadInOut.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/LeadInOut.cpp#L74-L133)）。
15. **StockRemoval 语义**：毛坯截面为外环（切除区），零件截面作为孔洞；两者合并后填充；毛坯层切为空时退化为 `makeStockRectangle`（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L735-L792)）。
16. **SolidCut 语义**：网格本身是待去除体，切片轮廓即加工区。

### 4.3 精度约定

- 切片焊接容差 `1e-4` mm；简化容差 0.01 mm；层切内缩 5% 层厚且不小于 `1e-5` mm。
- 层高下限由 `minLayerHeight`（自适应）或 0.001 mm（对话框控件下限）约束。
- 所有计算均为 `float`，导出时坐标保留 3 位小数。

## 5. 输出形态与撤销

**场景侧**：该功能**不修改场景、不创建图元、不进撤销栈**。所有产物都在 `ReliefEngravingProcessor3D::m_job` 与服务的单例中（`m_processor`、`m_dialog` 常驻），关闭对话框不会清空，`hasGeneratedToolpath()` 据此判断。

**预览**：`ReliefToolpathPreview3D::applyLayerPreview` 遍历刀路：

- 按 `layerIndexAt` 与层 Z 过滤到当前层；
- 按 `showContourPreview/showFillPreview/showTravelMoves` 过滤类型（[ReliefToolpathPreview3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathPreview3D.cpp#L119-L128)）；
- 非 `Travel` 路径按 `power > 0` 的连续段拆分，避免把关光连接段画成加工线；首尾关光点并入首末段以保留引入点（[ReliefToolpathPreview3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathPreview3D.cpp#L130-L177)）；
- 颜色按**路径类型**（`applyColor(kind, segment)`），不是按功率；`travel` 标记单独给出（[ReliefToolpathPreview3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathPreview3D.cpp#L190-L197)）；
- 以 POD 数组提交给 `IViewport3DFacade` / `IToolpathOverlaySink3D`，`RenderWidget3D` 持有 `m_toolpathVerts`；`hasToolpathOverlay()` 返回 `!m_toolpathVerts.empty()`（[RenderWidget3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.cpp#L896-L899)）。

**导出**：

- G-code：默认 `.nc`。`ReliefToolpathExporter3D::exportSimpleGcode` 只设置 `safeZ = maxZ + 5`、`programName`、`rapidSpeed = max(scanSpeed×3, 3000) mm/s × 60`；其余用 `GcodeExportOptions` 默认值（Generic 方言、`M3` 开光 / `M5` 关光、PWM 0–1000）（[ReliefToolpathExporter3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathExporter3D.cpp#L504-L560)、[ToolpathExporter.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Include/Engraving/ToolpathExporter.h#L22-L53)）。
- 文本格式：`G21`（mm）、`G90`（绝对）、`G94`（每分钟进给）；快移 `G0 X.. Y.. [Z..]`；切削 `G1 X.. Y.. Z.. F<mm/min> S<pwm>`；结尾关光、抬到 `safeZ`、`M2`（[ToolpathExporter.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/ToolpathExporter.cpp#L130-L190)）。导出前还会插入 `buildGcodeProcessHeader` 注释头（[ReliefToolpathExporter3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathExporter3D.cpp#L548-L551)）。
- JSON：`type = relief_engraving_3d`、`schemaVersion = 2`、`sourceMeshId`、顶层 `layerHeight/laserPower/scanSpeed`、`process`（模式、材料、上下方向、光斑、重叠、栅格角、bbox、stockMeshId、道次、自适应、lead-in/out）、`estimate`（切削/空走秒数与假设快移速度/安全高度）、`layerHeights/layerCount`、`paths`（kind、layerIndex、z、stats、points）、`layers`（每层 kindCounts 与统计），另保留兼容字段 `toolpaths`（[ReliefToolpathExporter3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathExporter3D.cpp#L307-L370)、[ReliefToolpathExporter3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathExporter3D.cpp#L375-L448)）。
- 时间预估：快移速度 `max(scanSpeed×3, 3000)` mm/s，安全高度 `maxZ+5`，切削/空走分别累计（[ReliefToolpathExporter3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathExporter3D.cpp#L464-L502)）。

## 6. 限制与边界行为

1. **毛坯 ID 失效**：`stockMeshId` 仅写入参数与 JSON，处理器从不按 ID 查实体；`setStockMeshEntity` 全仓库无调用点（[ReliefEngravingProcessor3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Relief/ReliefEngravingProcessor3D.h#L56-L56)、[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L197-L207)）。因此 `StockRemoval` 永远使用自动 bbox 毛坯（XY 各外扩 2 mm、Z 向下 5 mm、顶面等于零件 `maxZ`）。对话框里填任何非 0 ID 都无效。
2. **层数控件无效**：`collectParams` 写入 `numLayers`（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L143-L143)），`toEngravingParams` 又强制为 0；层数始终由 `layerHeight` 推导，界面上的 `Layer count` 只是回显（只读）。
3. **开放轮廓被丢弃**：切片连接调用 `connectSegmentsToContours(..., 1e-4f, false)`，第三参数 `allowChordClose=false` 即严格模式，开口链不补弦、直接丢弃（仅计数告警）。因此非水密网格的破洞区域会缺轮廓/缺填充。
4. **日志文本与行为不符**：代码注释与日志写 `open slices will be chord-closed`（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L870-L878)），实际是丢弃；`MeshSlicer` 内部日志仍称 `chord-closed`。见 §8。
5. **非水密/非流形/自交不阻断**：只记警告并继续；只有在最终刀路为空时，才把预检的 `errorMessage` 作为失败原因弹窗（`Mesh is not watertight...` / `Mesh is not manifold...` / 否则 `No toolpath generated for the current parameters`）（[ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L243-L266)）。
6. **无网格直接拦截**：`Please import or select a mesh model first.`（[ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L73-L80)）。
7. **空层静默跳过**：某层切片为空（层高落在模型外、破面）不报错，只在总日志里体现层数（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L903-L908)）。
8. **自适应失败回退**：`computeAdaptiveLayerHeights` 返回空时回退均匀分层并记警告（[VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L511-L524)）。
9. **轮廓平行填充在 UI 不可达**：`useRaster = processMode != ContourOnly`，而 `ContourOnly` 时 `includeFill=false`；`FillOnly`/`ContourAndFill` 都用扫描线。因此 3D 界面永远不会走 `fillContoursContourParallel`。
10. **Z 范围默认值陷阱**：只有 `baseZ≈0 且 maxZ≈1` 才替换为 bbox；用户把 Top Z 设成 1.0 且模型最高点不是 1 时，会保留 1.0，可能裁掉/多切。
11. **预览“叠加不可用”分支**：`hasToolpathOverlay()` 已实现，正常情况下非空返回 true；仅当过滤器把所有路径都隐藏（例如只关掉全部显示开关）导致 `lines` 为空时，才会显示 `viewport toolpath overlay is not implemented yet` 这条误导文案（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L843-L855)）。
12. **无撤销/无场景回写**：该功能不产生 `Undo` 记录，也不修改任何图元；对话框关闭（`closeEvent` 与 `QDialog::finished` 两条路径）会 `ReliefToolpathPreview3D::clear` 并恢复场景不透明度，但**已生成的 `job` 仍保留在服务单例**，重新打开对话框会复用（[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L602-L613)、[ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L981-L992)）。
13. **RequiresSelection 与实际回退**：菜单声明 `RequiresSelection`，但服务在无选中时会加工全场景；两个入口的可用性由总线控制，直接调用服务不受限。
14. **单例状态**：`m_processor` 与 `m_dialog` 由服务长期持有，`m_renderWidget` 每次 `showDialog` 覆盖；没有场景切换时的处理器重置逻辑。

## 7. 源码与测试

### 7.1 关键源码

| 文件 | 作用 | 关键行 |
|---|---|---|
| [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L224-L224) | 菜单项 | L224 |
| [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L248-L255) | 目录注册 | L248-L255 |
| [MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L83-L84) | 操作 ID | L83-L84 |
| [AlgorithmOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperations3D.cpp#L83-L94) | 打开服务 | L83-L94 |
| [ReliefEngravingService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingService3D.cpp#L29-L167) | 收集/开窗/导出 | L29-L167 |
| [ReliefEngravingProcessor3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingProcessor3D.cpp#L73-L307) | 配置/预览/生成 | L73-L307 |
| [ReliefEngravingParams3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Relief/ReliefEngravingParams3D.h#L30-L76) | 参数权威定义 | L30-L76 |
| [ReliefEngravingParams3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefEngravingParams3D.cpp#L9-L75) | 参数映射/材料预设 | L9-L75 |
| [ReliefEngravingJob3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Relief/ReliefEngravingJob3D.h#L21-L48) | 结果结构 | L21-L48 |
| [ReliefEngravingDialog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/ReliefEngravingDialog3D.cpp#L126-L180) | 参数收集 | L126-L180 |
| [ReliefToolpathPreview3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathPreview3D.cpp#L96-L204) | 预览生成 | L96-L204 |
| [ReliefToolpathExporter3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefToolpathExporter3D.cpp#L296-L560) | JSON/G-code | L296-L560 |
| [VolumeProcessing.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Include/Engraving/VolumeProcessing.h#L40-L160) | 引擎接口 | L40-L160 |
| [VolumeProcessing.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/VolumeProcessing.cpp#L331-L942) | 校验/层高/切片/填充/排序 | L331-L942 |
| [MeshSlicer.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/MeshSlicer.cpp#L431-L874) | 切片与连链 | L431-L874 |
| [LaserStrategy.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/LaserStrategy.cpp#L10-L54) | 线距/层数/材料 | L10-L54 |
| [LeadInOut.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/LeadInOut.cpp#L74-L133) | 引入/引出 | L74-L133 |
| [ToolpathExporter.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Include/Engraving/ToolpathExporter.h#L22-L53) | G-code 选项默认值 | L22-L53 |
| [ToolpathExporter.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engraving/Engraving/Src/ToolpathExporter.cpp#L33-L190) | 指令生成 | L33-L190 |
| [ReliefProcessWorkflow3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Relief/ReliefProcessWorkflow3D.cpp#L21-L59) | 提示与导出 | L21-L59 |
| [ReliefToolpath3DTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/ReliefToolpath3DTests.cpp#L108-L469) | 单元测试 | L108-L469 |

### 7.2 测试

[ReliefToolpath3DTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/ReliefToolpath3DTests.cpp#L108-L108) 的 `private slots` 共 8 个用例，均只覆盖“预览/导出/参数转发”，不覆盖切片与 DRC 几何：

- `previewUsesExactKindColorsAndFilters`（L120）：验证按类型着色与 `showContour/showFill/showTravel` 过滤；
- `previewUsesLayerMetadataForRampLeadIn`（L170）：验证层元数据与斜坡引入点；
- `jsonExportWritesSchemaV2`（L210）：JSON `schemaVersion = 2`；
- `jsonExportIncludesProcessAndRampLayerZ`（L282）：`process` 与逐层 Z；
- `gcodeExportWritesLayerAndKindComments`（L357）：G-code 头注释/层与类型注释；
- `estimateMachiningTimeSplitsCutAndTravel`（L396）：切削/空走时间拆分；
- `paramsForwardLeadInOutToEngraving`（L439）、`paramsForwardPathOrderingToEngraving`（L457）、`paramsForwardPassesAndAdaptiveToEngraving`（L469）：`toEngravingParams` 字段透传。

没有针对 `VolumeProcessing` 切片/连通/填充算法的单元测试，也没有针对 `ReliefEngravingService3D`/对话框的 UI 测试；这部分仅靠日志与人工验证。

## 8. 旧文档差异

1. **“开口链补弦闭合”**：`VolumeProcessing.cpp` 的注释与日志（L870-L878）以及 `MeshSlicer` 的内部日志写“open slices will be chord-closed”，实际第三参数传 `false`（`allowChordClose=false`），开口链被**丢弃**而非补弦。这是注释/日志层面的错误描述。
2. **`stockMeshId` 注释**：头文件写“0 表示使用 bbox”，暗示非 0 会使用真实毛坯；实际非 0 也从不解析，`setStockMeshEntity` 无调用点，永远用 bbox 毛坯（外扩 2 mm / Z −5 mm）。
3. **`numLayers`**：结构体与对话框都保留该字段，看起来可控制层数；`toEngravingParams` 强制 0，实际不可控。
4. **“等高轮廓偏置最多 1000 圈”**：`contourParallelFillLoops(..., 1000.0)` 的 `1000.0` 是毫米到内部单位的比例（scale），不是圈数上限；圈数由 `maxSpan / lineSpacing` 决定。且 3D UI 的填充恒为扫描线，这条路径不可达。
5. **“预览按功率着色”**：实际按 `ToolpathKind` 着色；功率只用于把 `power>0` 的连续段切分，`Travel` 段整条绘制。测试 `previewUsesExactKindColorsAndFilters` 确认了这一行为。
6. **“叠加不可用分支是死代码”**：`RenderWidget3D::hasToolpathOverlay` 已实现（返回 `!m_toolpathVerts.empty()`），正常路径不会触发该提示；但当所有显示过滤器都关闭时 `lines` 为空，分支仍可命中并显示“not implemented yet”的过时文案。
7. **“浮雕走 AlgorithmTask 框架”**：`ReliefEngraving3D` 任务 ID 与映射存在，但未注册 handler，功能实际由 `ReliefEngravingService3D::showDialog` 直接驱动；映射是遗留/死配置。
8. **菜单与目录标签不一致**：菜单显示 `Mesh Relief Engraving...`，命令目录写 `3D Relief Engraving...`；两处都属正常（不同用途），但旧文档若只抄其一会与界面不符。
