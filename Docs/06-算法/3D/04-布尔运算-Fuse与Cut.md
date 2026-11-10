# 布尔运算：Fuse 与 Cut

> 状态说明（2026-11-10）　状态：现行　维护：架构组

Fuse（并）与 Cut（差）是 3D 工作台 Draw → Boolean 子菜单下的两个实体布尔命令。二者共用同一入口函数 `replaceTwoWithBoolean`，只在 `GmcBooleanOp` 上不同：Fuse 传 `Union`，Cut 传 `Difference`，并且 Cut 的语义是“第一个选中减第二个选中”。

## 1. 菜单入口与命令注册

| 项目 | 内容 |
| --- | --- |
| 菜单路径 | `Draw` → `Boolean` → `Fuse` / `Cut` |
| 子菜单定义 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L198-L201)（`id` 为 `model.boolean`） |
| Fuse 菜单项 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L199)，命令 id `model.boolean_fuse`，快捷键 `Ctrl+Shift+U` |
| Cut 菜单项 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L200)，命令 id `model.boolean_cut`，快捷键 `Ctrl+Shift+X` |
| 菜单动作枚举 | `MenuActionId3D::Model_BooleanFuse` / `Model_BooleanCut`（[MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L89-L90)） |
| 操作 ID | `OperationId3D::Model_BooleanFuse` / `Model_BooleanCut`（[OperationId.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Command/OperationId.h#L201-L202)） |
| 命令目录条目 | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L284-L299) |
| 操作实现注册 | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L616-L639) |
| 启用条件 | `Cmd::RequiresMultiSelection`（选中 2 个及以上才亮） |
| 可勾选 | 否 |
| 暴露位置 | 菜单、工具栏、快捷键、右键菜单（`kMenuToolbarShortcutCtx`） |

`RequiresMultiSelection` 的定义是 `CmdCond::MultiSelection`（[CommandEnableRule.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Command/CommandEnableRule.h#L67)），判定规则为选择数量 `>= 2`，选择数为 1 与 0 时命令不可用（`SatisfyConds_RequiresMultiSelection`，[CommandKernelTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Test/CommandKernelTests.cpp#L68-L85)）。命令目录在选择规则旁注明：布尔至少需要两个图元，用与 2D `edit.boolean_*` 相同的判定，避免“只选一个也亮着、点了必然失败”（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L282-L283)）。

## 2. 调用链路

1. 菜单/快捷键/工具栏 → `CommandCatalog3D::operationForCommandId`（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L674-L682)）把命令 id 解析为 `OperationId3D::Model_BooleanFuse` 或 `Model_BooleanCut`。
2. `OperationBus3D::run`（[OperationBus3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationBus3D.cpp#L102-L146)）校验 id 与上下文后调用注册的 `LambdaOperation3D`。
3. Fuse 的 lambda 调用 `replaceTwoWithBoolean(ctx, GmcBooleanOp::Union, "Fused", "Boolean Fuse")`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L616-L626)）；Cut 的 lambda 调用 `replaceTwoWithBoolean(ctx, GmcBooleanOp::Difference, "Cut", "Boolean Cut")`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L628-L639)）。
4. `replaceTwoWithBoolean`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L370-L484)）先调 `getTwoSelectedBRep`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L174-L211)）取两个操作数。
5. 取 B-Rep：`getWorldBRepForEntity`（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L253-L269)）→ `clone` → `alignShapeToMesh`（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L63-L86)）。
6. 几何运算：`GmcBoolean::fuse` / `GmcBoolean::cut`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L480-L490)）→ `operateWithOptions`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L330-L472)）。
7. 结果落地：`captureBRepSnapshots3D` → `SceneManager3D::extractEntities` → `detachBRep` → `tessellateToEntity` → `addEntity` → `attachBRep` → `pushReplaceUndoWithBRep`（L422-L463）。

## 3. 输入与参数

### 输入实体

- 选择集必须恰好 2 个实体（`selected.size() != 2` 直接失败，[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L185-L190)）。
- 两个实体都必须有 B-Rep：`getWorldBRepForEntity` 返回空即失败，日志文本 `boolean requires two B-Rep solids (Create Solid or import STEP)`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L203-L208)）。
- 选择顺序有意义：`selected[0]` 是 A，`selected[1]` 是 B。Fuse 为 `A ∪ B`；Cut 为 `A - B`，即第一个选中减第二个选中（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L192-L193)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L401-L402)）。

### 参数

本命令没有 `params` 读取，也没有对话框；调用 `GmcBoolean::fuse/cut` 时容差传 `{}`、选项走默认构造：

| 参数 | 默认值 | 单位/含义 | 权威定义 |
| --- | --- | --- | --- |
| `GmcTolerance.linear` | `1e-7` | mm，初始 fuzzy | [GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L22-L28) |
| `GmcTolerance.angular` | `1e-9` | rad；布尔链路未使用 | 同上 |
| `GmcBooleanOptions.unifySameDomain` | `true` | 同域合并 | [GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L194-L211) |
| `GmcBooleanOptions.checkResult` | `true` | 跑 `BRepCheck_Analyzer` | 同上 |
| `GmcBooleanOptions.verifyVolume` | `true` | 体积健全性检查 | 同上 |
| `GmcBooleanOptions.volumeTolerance` | `0.0` | `<= 0` 时回落到 `1e-6 * max(1, Va, Vb)` | 同上 |
| `GmcBooleanOptions.maxRetries` | `3` | 不含首轮的重试次数 | 同上 |
| `GmcBooleanOptions.fuzzyGrowth` | `10.0` | 每级放大倍数 | 同上 |
| `GmcBooleanOptions.fuzzyMax` | `1.0` | mm，fuzzy 上限 | 同上 |

## 4. 算法原理与分步流程

### 数据结构

- 输入与输出都是 `GeoModelCore::TopoShape`（封装 `TopoDS_Shape`，双精度，mm）。
- 输出状态装在 `GmcBooleanResult`（`success`、`errorMessage[256]`、`diagnostics`，[GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L214-L220)）；`diagnostics` 记录 `retryCount`、`fuzzyUsed`、`facesBefore`、`facesAfterUnify`、`analyzerPassed`、`unified`、`volumeA/B/Result`、`invalidFaceCount`（[GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L171-L192)）。
- UI 侧只保留结果网格与结果 B-Rep，诊断信息不落库、不显示。

### 坐标与精度

- B-Rep 以世界坐标参与运算。`getWorldBRepForEntity` 先 `clone` 再按“网格包围盒中心 − B-Rep 包围盒中心”做平移对齐（阈值 `1e-6`），只平移、不旋转、不缩放（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L63-L86)）。
- 输入先 `BRepBuilderAPI_Copy` 复制（只复制几何、不复制网格），保证重试起点一致且不改写入参（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L65-L74)）。
- 体积用 `BRepGProp::VolumeProperties` 取绝对值（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L76-L85)）。

### 分步流程

1. 校验选择集恰好 2 个，取 A、B 的 world B-Rep 克隆；任一为空则失败。
2. 内核 `ensureInitialized`，复制 A、B 并计算 `volumeA`、`volumeB`。
3. 计算重试参数：`maxAttempts = max(1, maxRetries + 1) = 4`；`initialFuzzy = max(1e-7, 1e-7) = 1e-7`；`fuzzyMax = 1.0`；`fuzzyGrowth = 10.0`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L358-L363)）。
4. 每一轮用相同配置构建布尔对象：`SetFuzzyValue(fuzzy)`、`SetNonDestructive(true)`、`SetRunParallel(true)`、`SetGlue(BOPAlgo_GlueOff)`（`configureBoolean`，[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L169-L175)）。
5. Fuse 用 `BRepAlgoAPI_Fuse(a, b)`，Cut 用 `BRepAlgoAPI_Cut(a, b)`；`HasErrors()` 或 `!IsDone()` 时记 `fuse failed` / `cut failed`，放大 fuzzy 后进入下一轮（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L195-L230)）。
6. 成功构建后先取 `raw`；`SimplifyResult(true, true)` 得到对象内同域简化结果 `simplified`（`buildOutcome`，[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L177-L193)）。
7. 比较简化前后面数：`faceCount(simplified) < faceCount(raw)` 认为合并生效；否则回退到 `raw`，并再用 `ShapeUpgrade_UnifySameDomain`（线性容差 `max(1e-3, 对角线 * 1e-6)`、角容差 `1e-3`）合并（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L284-L310)、[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L379-L400)）。
8. `BRepCheck_Analyzer` 严格校验；失败时记 `result invalid: <shape check 文本> (failed subshapes: N)`，放大 fuzzy 重试（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L402-L416)、[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L155-L166)）。
9. 体积健全性检查（仅当 `verifyVolume` 且 `Va > 0 && Vb > 0`）：`eps = volumeTolerance > 0 ? volumeTolerance : 1e-6 * max(1, Va, Vb)`；Union 要求 `V ∈ [max(Va,Vb) − eps, Va + Vb + eps]`，Difference 要求 `V <= Va + eps`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L312-L327)、[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L418-L441)）。
10. 通过后返回新 `TopoShape`；任何一轮失败都把错误累积到 `lastError`，最终文本为 `boolean failed after 4 attempts: <lastError>`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L447-L454)）。

fuzzy 的实际序列为 `1e-7 → 1e-6 → 1e-5 → 1e-4`（每次 `min(fuzzy * 10, 1.0)`），共 4 次尝试。

### 输出形状语义

- 返回的是全新形状，`unify`/简化后拓扑索引整体失效，不能沿用旧的 `faceId`/边索引；`GmcBoolean.h` 的注释明确要求“unify 后面/边索引不可跨调用缓存”（[GmcBoolean.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcBoolean.h#L30-L38)）。
- 布尔结果按单实体处理：UI 只创建一个网格实体承载整个结果。

## 5. 输出形态与撤销

1. 结果三角化：`tessellateToEntity(*result, resultName)`，Fuse 名 `Fused`，Cut 名 `Cut`（默认 `GmcMeshParams`：0.1 mm、0.5 rad、并行）。
2. 两个操作数先一次性 `extractEntities(operands, 2, removedRaw)`，再 `detachBRep(removedAId)`、`detachBRep(removedBId)`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L422-L434)）。
3. 结果 `addEntity` 后 `attachBRep(entityId, result)`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L450-L453)）。
4. 结果 B-Rep 再次快照，随后 `pushReplaceUndoWithBRep(ctx, removed, added, removedBRep, addedBRep, undoLabel)`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L455-L463)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L87-L116)）。
5. 撤销命令是 `BRepReplaceEntitiesCommand3D`（[BRepReplaceEntitiesCommand3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Edit/BRepReplaceEntitiesCommand3D.h#L65-L94)）：
   - `execute`：先按 `removedBRep` 的 id `detachBRep`，调内层 `Eg::ReplaceEntitiesCommand3D::execute`（结果入场），再 `restoreBRepSnapshots3D(addedBRep)`（[BRepReplaceEntitiesCommand3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Edit/BRepReplaceEntitiesCommand3D.cpp#L112-L140)）。
   - `undo`：先按 `addedBRep` 的 id `detachBRep`，内层命令把结果摘出、操作数放回，再 `restoreBRepSnapshots3D(removedBRep)`（[BRepReplaceEntitiesCommand3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Edit/BRepReplaceEntitiesCommand3D.cpp#L142-L179)）。
   - 网格搬运完全委托 `Eg::ReplaceEntitiesCommand3D`（[SceneUndoCommands3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Include/Engine3D/Edit/SceneUndoCommands3D.h#L116-L158)）；B-Rep 快照与网格实体共用同一 `shared_ptr`，不产生第二份真源。
6. 成功后 `clearSelection`、`markSceneDirty`、`fitAll`、`updateStatusBar`、`documentManager->setModified(true)`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L465-L477)）。
7. 结果三角化失败时就地回滚：`addEntities(removedRaw, removedCount)` 把操作数放回场景，`restoreBRepSnapshots3D(removedBRep)` 挂回 B-Rep，函数返回 `false`，**不弹窗**（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L436-L448)）。

## 6. 限制与边界行为

- 选择数量：启用条件是 `>= 2`，但实现要求 `== 2`。选中 3 个及以上时菜单可点，执行会失败并弹 `Boolean` 提示框：`Select exactly two B-Rep solids (parametric or STEP).\nCut uses: first selected minus second selected.`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L379-L390)）。
- 缺 B-Rep：网格导入的实体没有 B-Rep，同样落到上面的提示框；日志另记 `boolean requires two B-Rep solids (Create Solid or import STEP)`。
- 空操作数：`null operand`（`GmcErrorCode::NullShape`），经 `Boolean Error` 弹窗显示 `Boolean operation failed:\nnull operand`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L341-L345)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L408-L420)）。
- 失败报错文本（原样）：`fuse failed`、`cut failed`、`empty result shape`、`result invalid: ... (failed subshapes: N)`、`volume out of bounds (Va=..., Vb=..., V=...)`、`boolean failed after 4 attempts: ...`、`exception: ...`、`boolean internal error`。
- 失败重试会放大 fuzzy：薄壁、近共面模型可能在 `1e-6`～`1e-4` mm 的 fuzzy 下被“粘合”，结果拓扑与几何与无 fuzzy 时不同；`diagnostics.fuzzyUsed` 记录实际生效值，但 UI 不展示。
- 体积不可测（`Va` 或 `Vb` 为 0，例如退化实体或非实体）时跳过体积检查，只依赖 `BRepCheck`（`DegenerateSolidBooleanCurrentBehaviorRecorded` 记录了该现状）。
- 不相交的操作数：Fuse 的包围盒判据允许 `V = Va + Vb`，Cut 允许 `V = Va`，两者都能通过体积检查并得到合法结果（Fuse 为两个不相连实体合并的复合形状，Cut 原样返回 A 的几何）。
- 同域合并被关闭或 `SimplifyResult` 未降面时，结果会保留共面分割出的多余面；`unifySameDomain = false` 时面数不降。
- 返回空形状：`buildBoolean` 在 `outcome.raw.IsNull()` 时返回 `empty result shape`（[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L276-L280)）；体积为 0 的非空结果在 Cut 判据 `V <= Va + eps` 下仍会通过几何校验，后续三角化得不到有效网格，UI 侧因 `tessellateToEntity` 返回空而静默回滚。
- 三角化失败：无弹窗，静默回滚（日志 `boolean result tessellation failed`）。
- B-Rep 对齐限制：只按包围盒中心平移，带旋转/缩放的网格实体会得到位置错误的世界 B-Rep，从而得出错误的布尔结果。
- 单次只处理两个实体，不支持链式多体布尔；需要多次执行。
- 性能：结果先三角化一次用于显示，`attachBRep` 又为拾取缓存再三角化一次并重建 BVH（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L158-L188)）。

## 7. 源码与测试

关键源码：

- 菜单/注册：[base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L198-L201)、[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L284-L299)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L616-L639)。
- UI 编排：[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L370-L484)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L174-L211)。
- 几何算法：[GmcBooleanOps.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcBooleanOps.cpp#L330-L472)、[GmcBoolean.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcBoolean.h#L40-L47)。
- 撤销：[BRepReplaceEntitiesCommand3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Edit/BRepReplaceEntitiesCommand3D.h#L32-L94)、[BRepReplaceEntitiesCommand3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Edit/BRepReplaceEntitiesCommand3D.cpp#L112-L179)。

自动化测试：

- `GmcBooleanTest.FuseTwoBoxes`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L93-L103)）、`GmcBooleanTest.CutBox`（同文件 L104-L114）、`GmcBooleanTest.CommonBox`（L115-L125）。
- `GmcBooleanCredibilityTest.CoplanarFuseReducesFacesAfterUnify`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L1057-L1077)）、`CoplanarFuseWithoutUnifyKeepsSplitFaces`（L1078-L1096）、`ThinWallCutVolumeConserved`（L1097-L1115）、`DegenerateSolidBooleanCurrentBehaviorRecorded`（L1116-L1140）、`VolumeToleranceDrivesVerification`（L1141-L1173）、`InvalidFaceCountReportedAroundUnify`（L1174-L1200）、`NullOperandStatusIsConsistent`（L1201-L1213）。
- `BRepUndoRestoreTests.booleanUndoRestoresBRep`（[BRepUndoRestoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/BRepUndoRestoreTests.cpp#L225-L271)）：撤销后操作数回场且 B-Rep 可继续参与第二次布尔。
- `BRepUndoRestoreTests.booleanUndoThenStepExport`（同文件 L273-L323）：撤销后导出 STEP 得到两个实体。
- `BRepUndoRestoreTests.redoDetachesResultAndKeepsBRep`（同文件 L360-L389）：重做时结果离场、操作数 B-Rep 再次被摘除。
- 未找到针对 `model.boolean_fuse` / `model.boolean_cut` 菜单入口与弹窗分支的自动化测试。

## 8. 旧文档差异

本批写作过程中，[00-索引与算法对照表.md](file:///c:/Users/xx/Documents/Cpp/CAD/Docs/06-算法/3D/00-索引与算法对照表.md) 的“跨算法通用约定”曾有两处与代码不一致，现已按代码修正：

- 原写“几何结果生成后先 `attachBRep(entityId, shape)`，再入场景”；代码顺序相反，`addShapeToScene` 与 `replaceTwoWithBoolean` 都是先 `addEntity`、后 `attachBRep`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L146-L151)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L450-L453)）。
- 原写“涉及 B-Rep 的操作前调用 `pruneStaleEntries`”；`getTwoSelectedBRep`/`getOneSelectedBRep` 均未调用，`pruneStaleEntries` 目前只在 STEP 导出与未挂菜单的 `saveStepDocument` 路径出现（[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1143)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L493)）。
