# 网格套料（Nesting from Mesh）

> 状态说明（2026-11-10）　状态：现行　维护：架构组

本文描述 3D 工作台下 `Algorithm → Nesting from Mesh` 的实际实现：把场景中的网格实体各取 XY 包围盒当作 2D 零件，交给 `Nesting` 库排样，再把排样位移以平移方式回写到网格顶点。全文以源码为准（读取时间 2026-11-08）。

## 1. 菜单入口与命令注册

| 项 | 值 | 证据 |
|---|---|---|
| 菜单 | `Algorithm → Nesting from Mesh`（父菜单 id `algorithm`，`workbenches` 含 `3D`） | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L210-L225) |
| 菜单项 id | `algo.nesting_from_mesh`（`label` 为 `Nesting from Mesh`，无省略号） | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L225-L225) |
| command id | `algo.nesting_from_mesh`（与菜单项 id 同名） | 同上 |
| `OperationId3D` | `Algo_NestingFromMesh` | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L238-L246) |
| `MenuActionId3D` | `Algo_NestingFromMesh` | 同上 |
| 目录标签 / 图标 | `Nesting from Mesh...` / `group.svg` | 同上 |
| 注册位置 | `CommandCatalog3D.cpp` 静态目录条目，整段被 `#ifdef ENABLE_NESTING` 包裹 | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L238-L247) |
| 启用条件 | `Cmd::Always`，`kMenuOnly`（只出现在菜单，不进工具栏） | 同上 |

`Cmd::Always` 只表示菜单项永远可点，真正的门槛在处理器与任务 handler：

- `AlgorithmOperations3D` 的 lambda 先调用 `sceneHasMeshes`，场景无网格时直接返回失败，消息 `No mesh entities in scene`（[AlgorithmOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperations3D.cpp#L59-L80)）。
- `NestingFromMesh3DTaskHandler::canRun` 要求场景非空（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L140-L144)）。
- 对话框宿主 `collectNestingFromMesh` 再次检查 `scene->isEmpty()`，失败消息同名（[AlgorithmTaskUiHost3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskUiHost3D.cpp#L26-L40)）。

因此“有没有选中实体”不是启用条件：默认会对全场景网格套料（见 §3、§6）。

## 2. 调用链路

逐跳说明（同一跳内给出的函数名均可直接搜索）：

1. 菜单点击 → `base.json` 菜单项 `algo.nesting_from_mesh` → `CommandCatalog3D` 目录 → 操作总线 `OperationBus3D`。
2. `AlgorithmOperations3D::registerAll` 注册 `Algo_NestingFromMesh` lambda（[AlgorithmOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperations3D.cpp#L57-L80)）→ `resolveAlgorithmRunner` → `AlgorithmRunner3D::runForOperation`（[AlgorithmRunner3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmRunner3D.cpp#L71-L89)）。
3. `algorithmTaskForOperation3D` 把操作映射为 `AlgorithmTaskId::NestingFromMesh3D`（[AlgorithmOperationMapping3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperationMapping3D.cpp#L13-L14)）。
4. `AlgorithmRunner3D::runTask` 构造 `AlgorithmTaskContext` 后调用 `AlgorithmApplicationService::runSync`（[AlgorithmRunner3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmRunner3D.cpp#L91-L112)）。
5. `runSync` 在 UI 线程同步执行 handler，并用 `UiInteractionGate::GateLock` 锁界面；不是异步任务（[AlgorithmApplicationService.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/AlgorithmApplicationService.cpp#L144-L201)）。
6. `NestingFromMesh3DTaskHandler::doPrepare`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L147-L223)）：
   - `AlgorithmTaskUiHost3D::collectNestingFromMesh` 弹 `NestingFromMesh3DDialog`（[AlgorithmTaskUiHost3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskUiHost3D.cpp#L42-L52)）；
   - `gatherMeshes`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L34-L88)）；
   - `buildBBoxPart` 生成 `NestingPartInput`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L90-L123)）；
   - 记录变换前顶点/法线快照（L209-L220）。
7. `doCompute` 调用 `Eg::NestingEngine::runNestingAlgorithm`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L225-L261)）→ [NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L259-L587)。
8. 引擎通过 C API `Nesting_CreateJob` / `Nesting_SetOptions` / `Nesting_AddSheet` / `Nesting_AddPart` / `Nesting_Run` / `Nesting_GetReport` / `Nesting_GetPlacement` 驱动库（接口定义见 [NestingApi.h](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Include/Nesting/NestingApi.h#L218-L268)）。
9. `doApply` 按 `placement.offsetX/offsetY` 平移网格并在主线程提交撤销（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L263-L371)）。

## 3. 输入与参数

### 3.1 零件输入形态

`buildBBoxPart` 读取 `mesh->getBBox()` 的 `minPt/maxPt`，只用 X/Y：

- `refPoint = center = ((minX+maxX)/2, (minY+maxY)/2)`；
- `contours = {{(minX,minY)-ref, (maxX,minY)-ref, (maxX,maxY)-ref, (minX,maxY)-ref}}`，即**局部坐标的 4 点包围盒矩形**；
- `area = (maxX-minX)*(maxY-minY)`，`quantity = 1`；
- Z 不参与，网格高度不影响排样。

**这不是真实投影轮廓**：凹形零件按外接矩形占位，排样结果偏保守（不会重叠，但浪费料）。代码注释也只称其为 bbox 零件（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L112-L121)）。

`NestingPartInput` 的权威定义在 `UI/Common/Include/UI/Algorithm/INestingJobRunner.h`：`verts`、`contours`、`nestLocalVerts`、`center`、`refPoint`、`area`、`quantity`（[INestingJobRunner.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Algorithm/INestingJobRunner.h#L78-L97)）。

### 3.2 参数与默认值

`NestingParameters` 定义于 [INestingJobRunner.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Algorithm/INestingJobRunner.h#L15-L61)，3D 对话框只覆盖其中 6 个字段（[NestingFromMesh3DDialog.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/NestingFromMesh3DDialog.cpp#L17-L29)）。

| 参数 | 默认值 | 单位/取值 | 3D 对话框 | 说明 |
|---|---|---|---|---|
| `sheetWidth` | 1000.0 | mm | 10–5000，默认 1000 | 板宽 |
| `sheetHeight` | 600.0 | mm | 10–5000，默认 600 | 板高 |
| `marginX` / `marginY` | 5.0 / 5.0 | mm | 0–500，默认 5 | 四边对称使用 |
| `partSpacing` | 0.5 | mm | 0–50，默认 0.5 | 零件净间距 |
| `sheetSpacing` | 2.0 | mm | 不在对话框 | 多板沿 X 摆开的板间距 |
| `tolerance` | 0.01 | mm | 不在对话框 | 容差 |
| `enableRotation` | `true` | bool | 硬编码 `false` | 3D 侧禁用旋转 |
| `allowFlip` | `false` | bool | 硬编码 `false` | 镜像/翻转 |
| `autoSheetSize` | `true` | bool | 未修改，保持 `true` | 自动板料尺寸 |
| `targetFillRatio` | 0.8 | (0,1] | 不在对话框 | 仅自动尺寸时生效 |
| `iterations` | 50 | 次 | 不在对话框 | 直接作为 `opt.generations` |
| `strategy` | 2 | 0/1/2 | 不在对话框 | 2 = QUALITY |
| `maxSheets` | 0 | 张 | 不在对话框 | 0 = 不限 |
| `timeLimitMs` | 0 | ms | 不在对话框 | 0 = 不限时 |
| `sortStrategy` | 0 | 0–3 | 不在对话框 | 排序规则 |
| `nestSelectedOnly` | `false` | bool | 复选框，**默认未勾选** | 见 §6 |
| `angleMode` / `maxRotationCount` | 0 / 4 | — | 不在对话框 | 旋转角度预设 |
| `partQuantity` | 1 | 件 | 不在对话框 | 3D 侧恒为 1 |
| `enableHoleNesting` | `false` | bool | 不在对话框 | 孔内嵌套 |
| `enableCommonLine` | `false` | bool | 不在对话框 | 共边 |

对话框数值控件统一 `setDecimals(2)`、后缀 ` mm`（[NestingFromMesh3DDialog.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/NestingFromMesh3DDialog.cpp#L41-L59)）；`Nest selected meshes only` 复选框只创建、未设置初始勾选状态（[NestingFromMesh3DDialog.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/NestingFromMesh3DDialog.cpp#L63-L64)）。

## 4. 算法原理与分步流程

### 4.1 数据结构与坐标系

- 输入零件：局部坐标多边形（相对包围盒中心），因此 `placement.offsetX/offsetY` 是“目标参考点世界坐标”。
- 输出放置：`NestingPartPlacement { partId, instanceIndex, sheetOrdinal, offsetX, offsetY, rotationDeg, flipped }`（[INestingJobRunner.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Algorithm/INestingJobRunner.h#L99-L108)）。
- 坐标约定：毫米、Z 轴向上；套料只改 X/Y，Z 不变；`sheetOrdinal` 仅用于多板时沿 X 平移。

### 4.2 引擎侧分步

1. **创建作业**：`NestingEngine::runNestingAlgorithm` 先做空输入与重入检查（“already running”），再 `Nesting_CreateJob`（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L259-L304)）。
2. **选项映射**：`NestingOptions` 填入 `partSpacing`、`sheetSpacing`、`tolerance`、`allowMirror`、`strategy`、`timeLimitMs`、`threadCount`（线程池时 0）、`maxSheets`、`autoSizeSheet`、`targetFillRatio`、`generations`、共边/孔嵌套/纹理等；旋转角由 `fillRotationAngles` 生成，`enableRotation=false` 时只保留 `0°`（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L317-L344)、[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L153-L185)）。
3. **建板**：`sheet.quantity = 0`（不限张数，实际由 `maxSheets` 约束）；四边 margin 由 `marginX/marginY` 对称赋值（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L352-L370)）。
4. **加零件**：逐件 `Nesting_AddPart`，`contours` 为空时回退到 `verts`（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L376-L376) 起）。
5. **自动板料尺寸**（3D 默认开启）：库按“全部实例净面积 / 安全填充率”推导可用面积，安全填充率取 `min(targetFillRatio, 0.60)`；保持用户给定宽高比缩放；四边 margin 与 `2×sheetSpacing` 加回毛尺寸；用户给的宽高作为**下界**；自动模式下目标函数强制为 `NESTING_OBJ_COMPACT`（[NestingApi.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Api/NestingApi.cpp#L495-L570)、[NestingApi.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Api/NestingApi.cpp#L630-L649)）。
6. **NFP 精确放置**：单板放置器 `SheetPlacer` 先求 `InnerFitRect/InnerFitPolygon`（零件参考点合法区域），再对每个已放置件求 `OuterNFP` 并求并集作为禁止区，合法平移区 = 板材可用区 \ 禁止区；在候选点中按目标评分选位（[Placer.h](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Placement/Placer.h#L5-L28)、[NoFitPolygon.h](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Core/NoFitPolygon.h#L11-L19)）。
7. **质量迭代**：策略分档与预算在 `Solver` 中决定——`FAST` 单次贪心（population=1、generations=0）；`BALANCED` 默认 16×12；`QUALITY` 默认 32×40 加 24 步退火；先用一次贪心实测耗时做成本模型，预算不足时自动降档（[Solver.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Engine/Solver.cpp#L554-L590)、[Solver.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Engine/Solver.cpp#L722-L757)）。
8. **结果回收**：`Nesting_GetReport` 填写 `placedCount/unplacedCount/totalUsableArea/totalPartArea`，`utilization` 优先取后端 `packedUtilization`；自动尺寸时回填 `autoSheetWidth/Height`（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L510-L538)）。
9. **多板摆开**：`sheetPitch = 实际板宽 + max(0, sheetSpacing)`；`offsetX = tx + sheetOrdinal * sheetPitch`，`offsetY = ty`（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L542-L568)）。

### 4.3 精度与单位

- 全链路公开参数为毫米。库内部使用整数定点坐标（C API 会拒绝超出定点范围的坐标，见测试 `CoordinateOutsideFixedPointRangeIsRejected`），`NestingApi.cpp` 用 `Scale::ToMm` 把内部坐标换回毫米（[NestingApi.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Api/NestingApi.cpp#L511-L570)）。
- `tolerance` 默认 0.01 mm，用于几何判定；`partSpacing` 与 `sheetSpacing` 也是毫米。
- 旋转角以度表示；3D 侧固定 0°，放置结果里的 `rotationDeg` 不参与回写。

## 5. 输出形态与撤销

**回写方式**：`doApply` 遍历 `placements`，`dx = offsetX - ref.x`、`dy = offsetY - ref.y`，调用 `mesh->translate(Vec3f(dx, dy, 0))`，随后 `updateEntityBounds`、`markEntitiesGeometryChanged`、`markDataChanged`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L299-L325)）。平移是**就地改写顶点**。

**撤销**：不是 `ReplaceEntitiesCommand3D`，而是：

- `doPrepare` 保存每个受影响实体的 `vertices` / `normals` 快照；
- `doApply` 收集变换后数据，展平为 POD 数组，调用 `SceneEditService3D::commitTransformUndo(...)`，矩阵参数传单位阵 `identity`，再 `notifySceneModified()`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L327-L361)）。

因此撤销能恢复顶点与法线，但**不更新 B-Rep**：处理器全程没有 `attachBRep` 或任何 B-Rep 重建调用。若该实体同时持有 `TopoShape`，撤销/回写后网格与 B-Rep 位置会不一致，后续布尔、分割、STEP 导出会用到旧 B-Rep 坐标。

**结果反馈**：

- 成功消息为 `Nesting completed: <placed>/<total> placed, utilization=<..>%, elapsed=<..>ms`，`stats` 写入 `totalParts/placedParts/unplacedParts/utilization`（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L570-L577)、[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L363-L370)）。
- 一个零件都没放下时返回 `success = true` 且不发任何位移，消息 `No parts could be placed. Try enlarging the sheet or reducing spacing.`（[AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L291-L297)）。
- 没有预览层、没有文件导出；产物就是场景中平移后的网格。

## 6. 限制与边界行为

1. **包围盒近似**：零件外形用外接矩形，孔洞、凹口、曲面投影全部丢失。结果不会互相重叠，但材料利用率按矩形计算，与真实轮廓无关。
2. **旋转/翻转被禁用但没有 UI**：对话框没有旋转或翻转复选框，`getParameters` 直接写死 `enableRotation=false`、`allowFlip=false`；参数结构里也没有写入这两个控件（[NestingFromMesh3DDialog.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/NestingFromMesh3DDialog.cpp#L17-L29)、[NestingFromMesh3DDialog.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/NestingFromMesh3DDialog.h#L38-L43)）。
3. **旋转结果被静默忽略**：即使其它调用方把 `enableRotation` 打开，`doApply` 仍只做平移，不使用 `placement.rotationDeg` 与 `placement.flipped`。此时布局按旋转后的占位计算，而网格不转，存在真实重叠风险。
4. **自动板料尺寸始终开启**：对话框不写 `autoSheetSize`，保持 `true`。用户看到的 1000×600 只是下界，实际可被放大；界面没有任何提示。
5. **`maxSheets` 不限**：3D 侧不设置该字段（默认 0），理论上可多板；但自动尺寸已按“全部放下”推导，正常情况下 `sheetOrdinal` 恒为 0。
6. **`selectedOnly` 未勾选时的默认行为**：`nestSelectedOnly=false` → `gatherMeshes` 直接遍历全场景（含非选中实体）。即使勾选但确实没有选中，代码也会**静默回退**到全场景（L57-L77），不会报错。
7. **无选中不阻断**：唯一的前置失败是“场景无任何网格”。
8. **空结果不算失败**：`placements.empty()` 走 `success=true` 分支，用户只看到提示文本。
9. **取消即丢弃**：`doApply` 见到 `m_cancelled` 直接返回 `userCancelled`，不写顶点；引擎侧取消时也销毁 job 并丢弃部分布局（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L485-L508)）。
10. **失败文本**：`No parts to nest`、`Nesting engine is already running`、`Nesting_CreateJob failed`、`Nesting_Run` 的具体错误由 `describeFailure` 透传（[NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L266-L315)）。
11. **同步阻塞**：`runSync` 在 UI 线程执行，界面被 `UiInteractionGate` 锁住，进度通过 `reportProgress` 回调上报；长作业期间不能操作场景。

## 7. 源码与测试

### 7.1 关键源码

| 文件 | 作用 | 关键行 |
|---|---|---|
| [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L210-L225) | 菜单项定义 | L210-L225 |
| [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L238-L247) | 目录注册、`Cmd::Always` | L238-L247 |
| [AlgorithmOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmOperations3D.cpp#L57-L80) | 操作 lambda | L57-L80 |
| [AlgorithmRunner3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/AlgorithmRunner3D.cpp#L71-L112) | 分发与 `runSync` | L71-L112 |
| [AlgorithmTaskRegistration3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskRegistration3D.cpp#L132-L393) | 任务 handler 全部逻辑 | L132-L393 |
| [AlgorithmTaskUiHost3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Algorithm/AlgorithmTaskUiHost3D.cpp#L26-L60) | 弹窗收集参数 | L26-L60 |
| [NestingFromMesh3DDialog.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/UI/Dlg/NestingFromMesh3DDialog.cpp#L9-L71) | 对话框与参数 | L9-L71 |
| [INestingJobRunner.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Algorithm/INestingJobRunner.h#L15-L108) | 参数/输入/输出结构 | L15-L108 |
| [NestingEngine.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Algorithm/NestingEngine.cpp#L259-L587) | 引擎封装 | L259-L587 |
| [NestingApi.h](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Include/Nesting/NestingApi.h#L218-L268) | C API | L218-L268 |
| [NestingApi.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Api/NestingApi.cpp#L495-L660) | 自动尺寸、校验、求解入口 | L495-L660 |
| [Solver.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Engine/Solver.cpp#L554-L757) | 策略预算与 GA/退火 | L554-L757 |
| [Placer.h](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Placement/Placer.h#L5-L28) | NFP 放置器说明 | L5-L28 |
| [NoFitPolygon.h](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Src/Core/NoFitPolygon.h#L11-L19) | NFP 定义 | L11-L19 |

### 7.2 测试

3D 侧**没有**针对 `Nesting from Mesh` 的测试：在 `UI/3D/Test` 全目录搜索 `nest` 无任何命中，`UI/Common/Test` 中也没有 `NestingEngine`/`runNestingAlgorithm` 的测试。核心库测试位于 `Nesting/Nesting/Test`：

- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L446-L446)：`ApiNesting.PlacesAllRectanglesOnOneSheet`；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L636-L636)：`ApiNesting.HonoursRotationRestriction`；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L760-L760)：`ApiNesting.SpillsOntoMultipleSheets`；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L813-L813)：`ApiNesting.MaxSheetsCapLeavesPartsUnplaced`；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L1126-L1126)：`ApiProgress.CallbackIsInvokedWithMonotonicPercent`、`ApiProgress.FinalizeStageReportsIntermediateProgress`（L1412）；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L1180-L1180)：`ApiCancel.CallbackReturningZeroStopsTheRun`、`ApiCancel.CancelledRunKeepsPartialLayoutReadableButReportsCancelled`（L1224）；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L1482-L1482)：`ApiAutoSheet.KeepsRequestedAspectRatio`；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L1594-L1594)：`ApiDeterminism.SameSeedGivesSameLayout`；
- [ApiTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/ApiTests.cpp#L2260-L2260)：`ApiArbitraryShapes.ConcaveShapesNeverOverlap`；
- [NfpTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/NfpTests.cpp#L35-L35)：`NfpOuter.InteriorMeansOverlapBoundaryMeansTouching`、`NfpOuter.ConcaveShapeStillProducesValidRegion`（L51）；
- [SolverTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Nesting/Nesting/Test/SolverTests.cpp#L40-L40)：`SolverInvariant.ConcavePartsNeverOverlap`、`SolverInvariant.QualityStrategyIsNotWorseThanFast`（L158）、`SolverExactFit.SingleRowIsFilledCompletely`（L330）。

`UI/Common/Test/NestingPreparerTests.cpp` 只覆盖 2D 的 `NestingPreparer`，不覆盖 3D handler，本文不展开。

## 8. 旧文档差异

以下几处与索引/旧说明或代码注释不一致，已按源码纠正：

1. **菜单 tooltip 与实现不一致**：`base.json` 的 tooltip 写 `Project mesh outlines and nest them as 2D parts`，实现并不投影轮廓，只用 XY 包围盒矩形（§3.1）。索引 [00-索引与算法对照表.md](file:///c:/Users/xx/Documents/Cpp/CAD/Docs/06-算法/3D/00-索引与算法对照表.md#L51-L51) 的“2D 矩形套料 + 3D 回写”描述与源码一致，无需修改。
2. **“旋转/翻转复选框未勾选”**：对话框根本没有这两个控件，而是代码写死 `enableRotation=false`、`allowFlip=false`。
3. **`allowFlip` 默认值**：`NestingParameters::allowFlip` 默认为 `false`，不存在“默认 true、3D 覆盖为 false”的情况。
4. **`selectedOnly` 默认勾选**：实际未勾选（复选框创建后没有 `setChecked`），默认套料全场景；勾选但无选中还会静默回退到全场景。
5. **“自动板料尺寸被关闭”**：没有关闭，`autoSheetSize` 保持 `true`，用户尺寸只是下界。
6. **撤销实现**：不是 `ReplaceEntitiesCommand3D`，而是 `commitTransformUndo` + 单位阵，并手工补 `GeometryChanged` 脏标记。
7. **多板偏移**：除板宽外还累加 `sheetSpacing`（默认 2.0 mm），该值不在对话框内。
