# 基本体创建：Cylinder（圆柱体）

> 状态说明（2026-11-10）　状态：现行　维护：架构组

Cylinder 是 3D 工作台 Draw 菜单下的参数化实体命令。它以半径和高度构造一个底面位于 z=0、轴向 +Z 的封闭圆柱，随后三角化入场景并挂回 B-Rep。

## 1. 菜单入口与命令注册

| 项目 | 内容 |
| --- | --- |
| 菜单路径 | `Draw` → `Cylinder` |
| 菜单定义 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L196)（父菜单 `id` 为 `draw`，见 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L167-L171)） |
| 菜单项/命令 id | `model.make_cylinder` |
| 菜单动作枚举 | `UI3D::MenuActionId3D::Model_MakeCylinder`（[MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L88)） |
| 操作 ID | `OperationId3D::Model_MakeCylinder`（[OperationId.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Command/OperationId.h#L200)） |
| 命令目录条目 | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L274-L281) |
| 操作实现注册 | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L601-L614) |
| 启用条件 | `Cmd::Always` |
| 可勾选 | 否 |
| 暴露位置 | 菜单、工具栏、快捷键、右键菜单（`kMenuToolbarShortcutCtx`，定义于 [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L9-L10)） |
| 工具栏按钮 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L335)（`id` 与 `commandId` 均为 `model.make_cylinder`） |

命令目录条目显示名 `Cylinder`，图标 `:/ui/common/Icons/Model3D/primitive_cylinder.svg`。操作实现由 `ModelOperations3D::registerAll` 注册，调用点见 [OperationSetup3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationSetup3D.cpp#L62-L64)。

## 2. 调用链路

1. 菜单/工具栏/快捷键 → `CommandCatalog3D::operationForCommandId`（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L674-L682)）把 `model.make_cylinder` 解析为 `OperationId3D::Model_MakeCylinder`。
2. `OperationBus3D::run`（[OperationBus3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationBus3D.cpp#L102-L146)）完成 id 与上下文校验后调用已注册的 `LambdaOperation3D`。
3. lambda 读取半径与高度，调用 `BRepBuilder::makeCylinder`（[BRepBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/BRepBuilder.cpp#L43-L48)）→ `TopoShape::makeCylinder` → `TopoShapeImpl::makeCylinder`（[TopoShapeImpl.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Private/TopoShapeImpl.h#L77-L81)）→ `BRepPrimAPI_MakeCylinder(radius, height)`。
4. `addShapeToScene`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L118-L172)）：`ensureInitialized`（L129）→ `tessellateToEntity`（L130）。
5. `BRepModelService3D::tessellateToEntity`（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L144-L156)）→ `GmcMeshBridge3D::tessellateToEntity`（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L51-L58)）→ `GmcMesh::tessellate`。
6. `GmcMeshBridge3D::toAprMeshEntity`（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L48)）生成网格；`addEntity`（L148）→ `attachBRep`（L149）。
7. `pushReplaceUndo`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L56-L79)）→ `clearSelection` → `markSceneDirty` → `fitAll` → `updateStatusBar` → `setModified(true)`（L151-L165）。

## 3. 输入与参数

不读取选择集，参数为半径与高度：

| 参数 | 类型 | 默认值 | 单位 | 权威定义 |
| --- | --- | --- | --- | --- |
| `radius` | double | `5.0` | mm | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L603) |
| `height` | double | `10.0` | mm | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L604) |

- 菜单触发使用默认值：Draw → Cylinder 不带 `params`，因此每次生成半径 5 mm、高 10 mm 的圆柱，没有尺寸对话框。
- 仓库内没有向 `Model_MakeCylinder` 显式传入 `radius`/`height` 的调用点；`CommandCatalog3D` 与 `base.json` 都不携带参数。外部调用方可写 `params` 覆盖，`toDouble()` 对非数值返回 `0.0`。
- `params` 结构为 `QVariantMap`，与 `id`、`source`、`traceId` 一起构成 `OperationRequest3D`（[OperationId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Operation/OperationId3D.h#L26-L37)）。

## 4. 算法原理与分步流程

### B-Rep 结构与坐标约定

- `BRepPrimAPI_MakeCylinder(radius, height)` 生成 `TopAbs_SOLID`：底面圆心在原点、位于 z=0 平面，轴向 +Z，顶面位于 z=height，整体占据半径 `radius`、高度 `height` 的圆柱区域。此语义与 OCCT 一致，UI 侧不做平移或旋转。
- 边界由一张柱面与两张圆形端面组成（共三张 `TopAbs_FACE`），柱面带一条接缝边。
- 体积为 `π * radius² * height`；半径 5 mm、高 10 mm 时约 785.4 mm³。

### B-Rep 与网格关系

- B-Rep 由 `TopoShape` 持有，`double` 精度；显示网格为 `AprMeshEntity`，位置与法线均为 `float`，逐三角形展开。
- 两者按实体 id 绑定：`BRepModelService3D::attachBRep` 写入，`hasBRep`/`getBRep` 读取，`detachBRep` 移除。
- `GmcMesh` 输出逐三角形 `faceIds`（柱面/顶面/底面），但 `toAprMeshEntity` 只复制位置与法线，UI 侧没有面回指。

### 三角化参数

默认 `GmcMeshParams`（[GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L97-L107)）：`linearDeflection = 0.1 mm`、`relative = false`、`angularDeflection = 0.5 rad`、`parallel = true`。柱面按弦高 0.1 mm 离散成多个四边形条带（再拆为三角形），端面按同样偏差离散成扇形三角面；法线按同面 60° 聚类，柱面法线连续、端面法线沿 ±Z。

### 分步流程

1. 解析 `radius`（默认 5.0）与 `height`（默认 10.0）。
2. `BRepPrimAPI_MakeCylinder` 构造圆柱实体。
3. `addShapeToScene` 校验上下文、`shape` 非空、`shape->isNull()` 为假。
4. `ensureInitialized` 后按 0.1 mm 线性偏差三角化柱面与端面。
5. `toAprMeshEntity` 组装网格实体（名称 `Cylinder`）。
6. `addEntity` 入场景，`attachBRep` 挂回 B-Rep。
7. `pushReplaceUndo(ctx, {}, { added }, "Make Cylinder")` 记录新增。
8. 清空选择、`markSceneDirty`、`fitAll`、刷新状态栏、`setModified(true)`。

## 5. 输出形态与撤销

- 场景结果：一个 `AprMeshEntity`，实体名 `Cylinder`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L608)），默认材质。
- B-Rep：圆柱实体以同一实体 id 存入 `BRepModelService3D`，布尔与分割命令可直接使用。
- 撤销：`pushReplaceUndo` 使用 `Eg::ReplaceEntitiesCommand3D`（[SceneUndoCommands3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Include/Engine3D/Edit/SceneUndoCommands3D.h#L116-L158)）：
  - 撤销把新增实体从场景摘出，命令持有该指针；重做把同一指针放回场景，id 不变。
  - 命令不保存 B-Rep 快照，撤销期间 B-Rep 映射仍在服务中；若撤销状态下执行 `pruneStaleEntries`（例如导出 STEP），映射被清理，重做后的圆柱不再带 B-Rep。
- 无撤销栈时只释放 `removed`（此处为空），实体保留但不可撤销。

## 6. 限制与边界行为

- 无选择约束：`Cmd::Always`，执行前不读取选择集。
- 固定尺寸：菜单路径只有半径 5 mm、高 10 mm 一种结果。
- 半径或高度非正：UI 与 `BRepBuilder` 均无校验，`BRepPrimAPI_MakeCylinder` 拒绝非正参数，`BRepBuilder::makeCylinder` 未捕获异常。
- 三角化失败：弹 `Model Error` 与 `Failed to tessellate shape for display.`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L132-L143)），不产生实体、不入撤销栈。
- 面拾取限制：UI 网格无 `faceIds`，无法区分柱面、顶面、底面；按面拾取只能依赖三角形法线。
- 精度：柱面在 0.1 mm 弦高下会出现可见的折线轮廓；该偏差只影响显示，不影响 B-Rep 与布尔结果。
- 高度极小或半径极小时，端面三角形面积趋零，`BVH3D` 射线求交会跳过零面积三角形（[BVH3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/SpatialIndex/BVH3D.cpp#L207-L211)）。

## 7. 源码与测试

关键源码：

- 菜单与注册：`base.json`、[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L274-L281)、[MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L88)。
- 操作实现：[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L601-L614)、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L118-L172)。
- 几何：[BRepBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/BRepBuilder.cpp#L43-L48)、[TopoShapeImpl.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Private/TopoShapeImpl.h#L77-L81)。
- 三角化与挂接：[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L144-L156)、[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L58)。

自动化测试：

- `TopoShapeTest.MakeCylinder`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L45-L52)）：断言体积等于 `π r² h`。
- `GmcMeshFaceIdTest.CylinderFaceIdCoversAllFaces`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L808-L827)）：验证三角面覆盖柱面、顶面、底面三个 `faceId`。
- `GmcMeshFaceIdTest.CylinderNormalsPointOutward`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L843-L857)）：验证法线朝外。
- 未找到针对 `model.make_cylinder` 菜单入口、三角化入场景与撤销/重做的自动化测试；`BRepUndoRestoreTests` 未包含基本体新增用例。
