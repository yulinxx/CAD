# 基本体创建：Sphere（球体）

> 状态说明（2026-11-10）　状态：现行　维护：架构组

Sphere 是 3D 工作台 Draw 菜单下的参数化实体命令。它用单一半径构造一个以世界原点为球心的封闭球实体，B-Rep 与显示网格同源，随后进入场景并挂回 B-Rep。

## 1. 菜单入口与命令注册

| 项目 | 内容 |
| --- | --- |
| 菜单路径 | `Draw` → `Sphere` |
| 菜单定义 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L195)（父菜单 `id` 为 `draw`，见 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L167-L171)） |
| 菜单项/命令 id | `model.make_sphere` |
| 菜单动作枚举 | `UI3D::MenuActionId3D::Model_MakeSphere`（[MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L87)） |
| 操作 ID | `OperationId3D::Model_MakeSphere`（[OperationId.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Command/OperationId.h#L199)） |
| 命令目录条目 | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L266-L273) |
| 操作实现注册 | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L588-L599) |
| 启用条件 | `Cmd::Always` |
| 可勾选 | 否 |
| 暴露位置 | 菜单、工具栏、快捷键、右键菜单（`kMenuToolbarShortcutCtx`） |

`CommandCatalog3D` 把 `model.make_sphere` 映射为 `OperationId3D::Model_MakeSphere`，显示名 `Sphere`，图标 `:/ui/common/Icons/Model3D/primitive_sphere.svg`。实现注册由 `ModelOperations3D::registerAll` 完成，调用点在 [OperationSetup3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationSetup3D.cpp#L62-L64)。

## 2. 调用链路

1. 菜单/工具栏/快捷键 → `CommandCatalog3D::operationForCommandId`（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L674-L682)）查表得到 `OperationId3D::Model_MakeSphere`。
2. `OperationBus3D::run`（[OperationBus3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationBus3D.cpp#L102-L146)）校验请求 id 与上下文后，交给注册在 `OperationBus3D` 的 `LambdaOperation3D`。
3. lambda 读取半径并调用 `BRepBuilder::makeSphere`（[BRepBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/BRepBuilder.cpp#L36-L41)）→ `TopoShape::makeSphere` → `TopoShapeImpl::makeSphere`（[TopoShapeImpl.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Private/TopoShapeImpl.h#L71-L75)）→ `BRepPrimAPI_MakeSphere(radius)`。
4. `addShapeToScene`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L118-L172)）：`ensureInitialized`（L129）→ `tessellateToEntity`（L130）。
5. `BRepModelService3D::tessellateToEntity`（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L144-L156)）→ `GmcMeshBridge3D::tessellateToEntity`（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L51-L58)）→ `GmcMesh::tessellate`。
6. `GmcMeshBridge3D::toAprMeshEntity`（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L48)）生成网格实体；`addEntity`（L148）→ `attachBRep`（L149，[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L190-L202)）。
7. `pushReplaceUndo`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L56-L79)）→ `clearSelection` → `markSceneDirty` → `fitAll` → `updateStatusBar` → `setModified(true)`（L151-L165）。

失败分支：`BRepBuilder::makeSphere` 返回空形状时 `addShapeToScene` 的上下文/形状检查会拒绝并返回 `false`，操作返回 `OperationResult3D{success=false}`，日志仅打 `SY_WARN`。

## 3. 输入与参数

本命令不读取选择集，唯一参数是半径：

| 参数 | 类型 | 默认值 | 单位 | 权威定义 |
| --- | --- | --- | --- | --- |
| `radius` | double | `5.0` | mm | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L590) |

- 菜单触发使用默认值：Draw → Sphere 不带 `params`，因此创建的球为半径 5 mm、直径 10 mm，没有半径输入对话框。
- 仓库内没有向 `Model_MakeSphere` 显式传 `radius` 的调用点；`CommandCatalog3D` 与 `base.json` 均不携带参数。外部调用方可以写 `params["radius"]` 覆盖默认值，`toDouble()` 对非数值返回 `0.0`。
- 代码注释与日志确认半径含义为球半径而非直径：`SY_DEBUGF("... MakeSphere r=%.3f", radius)`（L591）。

## 4. 算法原理与分步流程

### 数据结构与 B-Rep/网格关系

- `TopoShapeImpl::makeSphere` 内部持有 `BRepPrimAPI_MakeSphere` 的结果：一个 `TopAbs_SOLID`，其边界由一张球面 `TopAbs_FACE`、一条接缝边（seam edge）与两个极点顶点构成。
- 显示网格为 `AprMeshEntity`：三角形顶点位置与顶点法线都是 `float` 三元组，逐三角形展开；B-Rep 用 `double`，两者精度不同。
- B-Rep 与网格用实体 id 关联：`BRepModelService3D::attachBRep` 写入 `(id, shape)`，`hasBRep`/`getBRep` 读取，`detachBRep` 移除。

### 坐标系与尺寸约定

- 世界坐标系为右手系、Z 轴向上、长度单位 mm。
- `BRepPrimAPI_MakeSphere(radius)` 的球心在世界原点，无平移、无旋转，与 OCCT 语义一致。
- 实体体积为 `4/3 * π * radius³`；半径 5 mm 时约 523.6 mm³（`TopoShape::getVolume()` 用 OCCT `GProp_GProps` 计算）。

### 三角化

使用默认 `GmcMeshParams`（[GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L97-L107)）：`linearDeflection = 0.1 mm`、`relative = false`、`angularDeflection = 0.5 rad`、`parallel = true`。曲面按弦高误差 0.1 mm 自适应细分；顶点法线在同面内按 60° 夹角聚类平均，球面为光滑面，法线基本一致。

`GmcMesh` 同时给出逐三角形 `faceIds`，但 `toAprMeshEntity` 只复制位置与法线，`faceIds` 在 UI 侧丢失。

### 分步流程

1. 解析 `radius`，缺省 `5.0`。
2. `BRepPrimAPI_MakeSphere` 构造球实体，球心原点。
3. `addShapeToScene` 校验上下文、`shape` 与 `shape->isNull()`。
4. 内核初始化后按 0.1 mm 线性偏差三角化球面，得到闭合三角形集合。
5. 组装 `AprMeshEntity`（名称 `Sphere`），`addEntity` 入场景。
6. `attachBRep` 把球 B-Rep 挂到实体 id。
7. `pushReplaceUndo(ctx, {}, { added }, "Make Sphere")` 记录新增。
8. 清空选择、标记场景脏、`fitAll`、刷新状态栏、`setModified(true)`。

## 5. 输出形态与撤销

- 场景结果：单个 `AprMeshEntity`，实体名 `Sphere`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L593)），默认材质。
- B-Rep：球实体以同一实体 id 存入 `BRepModelService3D`，可被布尔与分割命令直接读取。
- 撤销：`pushReplaceUndo` 使用 `Eg::ReplaceEntitiesCommand3D`（[SceneUndoCommands3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Include/Engine3D/Edit/SceneUndoCommands3D.h#L116-L158)），撤销时把实体从场景摘出并由命令持有，重做时放回同一指针。
  - 该命令不保存 B-Rep 快照；撤销期间 `BRepModelService3D` 里的映射仍在，重做后 id 不变因此继续可用。
  - 若在撤销状态执行 `pruneStaleEntries`（例如导出 STEP，[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1143)），映射被清理，重做后的球不再带 B-Rep。
- 无 `undoRedoManager` 时 `pushReplaceUndo` 直接返回，实体保留但不可撤销。

## 6. 限制与边界行为

- 无选择约束：`Cmd::Always`，任何选择状态都可执行，且执行前不读取选择集。
- 固定尺寸：菜单路径只有半径 5 mm 一种结果，没有尺寸对话框。
- 半径非正：UI 与 `BRepBuilder` 都不校验，`BRepPrimAPI_MakeSphere` 对 `radius <= 0` 直接拒绝构造，`BRepBuilder::makeSphere` 未捕获异常。
- 三角化失败：弹 `Model Error` 提示 `Failed to tessellate shape for display.`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L132-L143)），不产生实体。
- 面级拾取不可用：网格没有 `faceIds`，UI 只能按三角形法线判断朝向；球面只有一个 B-Rep 面，此限制不影响后续布尔/分割，但影响“按面选择”。
- 精度：0.1 mm 线性偏差对半径 5 mm 的球会明显可见多边形轮廓；该偏差是显示参数，不改变 B-Rep。
- 退化构造：半径极小时球面仍能构造，但三角化难以为零面积三角形提供法线，`BVH3D` 会跳过零面积三角形（[BVH3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/SpatialIndex/BVH3D.cpp#L207-L211)），可能只剩少量可射线命中的三角面。

## 7. 源码与测试

关键源码：

- 菜单与注册：`base.json`、[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L266-L273)、[MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L87)、[OperationId.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Command/OperationId.h#L199)。
- 操作实现：[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L588-L599)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L118-L172)。
- 几何：[BRepBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/BRepBuilder.cpp#L36-L41)、[TopoShapeImpl.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Private/TopoShapeImpl.h#L71-L75)。
- 三角化：[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L144-L156)、[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L58)。

自动化测试：

- `TopoShapeTest.MakeSphere`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L37-L44)）：断言体积等于 `4/3 π r³`。
- 未找到针对 Sphere 三角化、网格法线或 `model.make_sphere` 菜单入口的自动化测试；`GmcMeshTest` 只覆盖 Box（`TessellateBox`）。
- 未找到针对“Sphere 新增后撤销/重做”的自动化测试；`BRepUndoRestoreTests` 的撤销用例均针对布尔与分割。
