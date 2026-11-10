# 基本体创建：Box（立方体）

> 状态说明（2026-11-10）　状态：现行　维护：架构组

Box 是 3D 工作台 Draw 菜单下的参数化实体命令。命令只依赖 OpenCASCADE 的 `BRepPrimAPI_MakeBox` 生成 B-Rep，再三角化后以网格实体入场景，同时把 B-Rep 挂到实体上，供后续布尔、分割、导出 STEP 使用。

## 1. 菜单入口与命令注册

| 项目 | 内容 |
| --- | --- |
| 菜单路径 | `Draw` → `Box` |
| 菜单定义 | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L167-L171) 中 `id` 为 `draw` 的 `Draw` 菜单；Box 菜单项在 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L194) |
| 菜单项/命令 id | `model.make_box` |
| 菜单动作枚举 | `UI3D::MenuActionId3D::Model_MakeBox`（[MenuActionId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Menu/MenuActionId3D.h#L86)） |
| 操作 ID | `OperationId3D::Model_MakeBox`（[OperationId.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/Command/OperationId.h#L198)） |
| 命令目录注册 | [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L258-L265) |
| 操作实现注册 | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L572-L586) |
| 启用条件 | `Cmd::Always`（无需选择集） |
| 可勾选 | 否（`checkable = false`） |
| 暴露位置 | 菜单、工具栏、快捷键、右键菜单（`kMenuToolbarShortcutCtx`，定义于 [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L9-L10)） |

命令目录条目把 `model.make_box` 解析为 `OperationId3D::Model_MakeBox`，显示名 `Box`，图标 `:/ui/common/Icons/Model3D/primitive_box.svg`。操作实现由 `ModelOperations3D::registerAll` 注册，调用点位于 [OperationSetup3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationSetup3D.cpp#L62-L64)（`ENABLE_GEOMODELCORE` 宏内）。

## 2. 调用链路

1. 菜单/工具栏/快捷键/右键菜单 → 命令目录查表：`CommandCatalog3D::operationForCommandId`（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L674-L682)）把 `model.make_box` 映射成 `OperationId3D::Model_MakeBox`。
2. 操作总线分发：`OperationBus3D::run`（[OperationBus3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/OperationBus3D.cpp#L102-L146)）先校验 `OperationId3D::None` 与 `OperationContext3D::isValid`，随后调用已注册的 `LambdaOperation3D::execute`。
3. 几何构造：lambda 内调用 `BRepBuilder::makeBox`（[BRepBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/BRepBuilder.cpp#L29-L34)）→ `TopoShape::makeBox` → `TopoShapeImpl::makeBox`（[TopoShapeImpl.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Private/TopoShapeImpl.h#L65-L69)），最终为 `BRepPrimAPI_MakeBox(dx, dy, dz)`。若 `BRepBuilder::makeBox` 返回空形状，操作返回失败文本 `Failed to create box`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L578-L582)）。
4. 入场景：`addShapeToScene`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L118-L172)）先 `ctx.brepService->ensureInitialized()`（L129），再 `tessellateToEntity`（L130）。
5. 三角化：`BRepModelService3D::tessellateToEntity`（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L144-L156)）→ `GmcMeshBridge3D::tessellateToEntity`（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L51-L58)）→ `GmcMesh::tessellate` → `GmcMeshBridge3D::toAprMeshEntity`（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L48)）。
6. 实体与 B-Rep：`sceneManager->addEntity(entity)`（L148），随后 `attachBRep(entityId, shape)`（L149，实现见 [BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L190-L202)）。
7. 收尾：`pushReplaceUndo`（L151，实现见 [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L56-L79)）→ `clearSelection` → `markSceneDirty` → `fitAll`（L153-L155）→ `updateStatusBar`（L159）→ `documentManager->setModified(true)`（L164）。

## 3. 输入与参数

命令不读取选择集，输入只有三项尺寸参数。参数来自 `OperationRequest3D::params`（`QVariantMap`，结构定义见 [OperationId3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Operation/OperationId3D.h#L26-L37)）：

| 参数 | 类型 | 默认值 | 单位 | 代码位置 |
| --- | --- | --- | --- | --- |
| `dx` | double | `10.0` | mm | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L574) |
| `dy` | double | `10.0` | mm | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L575) |
| `dz` | double | `10.0` | mm | [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L576) |

- 菜单触发使用默认值：Draw → Box 直接以 `OperationId3D` 派发请求，没有尺寸对话框，也不写入 `dx/dy/dz`，因此每次创建的都是 10×10×10 mm 的立方体。
- 代码中不存在向 `Model_MakeBox` 显式传入 `dx/dy/dz` 的调用点（全仓检索 `dx` 的其他命中分别属于 2D `Edit_Nudge` 与 3D 平移变换，与本命令无关）。若外部调用方显式写入 `params`，对应键值会被采用；`toDouble()` 对非数值返回 `0.0`，UI 层不做正负或范围校验。
- 权威定义位置：参数键与默认值只存在于上述三行 lambda 中，`CommandCatalog3D` 与 `base.json` 均不携带参数。

## 4. 算法原理与分步流程

### 数据结构与关系

- `TopoShape` 封装 `TopoDS_Shape`；几何为 OCCT 精确 B-Rep（实体 `TopAbs_SOLID`），坐标以 `double` 计算，长度单位为 mm。
- 显示使用 `AprMeshEntity`：位置数组 `std::array<float, 3>` 与法线数组 `std::array<float, 3>`，三角形逐点展开，顶点与法线都为 `float`。因此显示精度低于 B-Rep 精度。
- B-Rep 与网格通过实体 id 绑定：`BRepModelService3D` 内部 `unordered_map<entityId, shape>`（`attachBRep` 写入，`getBRep`/`hasBRep` 读取，`detachBRep` 移除）。B-Rep 不随网格一起被序列化，需在导出 STEP 时重新挂接或由撤销命令恢复。
- `attachBRep` 还会调用 `rebuildPickCache`，用同一组 `GmcMeshParams` 再次三角化并构建拾取用 BVH（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L158-L188)）；因此一次基本体入库的三角化发生两次。

### 坐标系与轴约定

- 世界坐标系为右手系、Z 轴向上的 mm 单位制。
- `BRepPrimAPI_MakeBox(dx, dy, dz)` 的角点在原点，实体占据 `[0, dx] × [0, dy] × [0, dz]`，三条边分别平行于 +X、+Y、+Z。此语义与 OCCT 完全一致，UI 侧不做平移或旋转修正。

### 三角化参数

`tessellateToEntity` 使用默认构造的 `GmcMeshParams`（[GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L97-L107)）：

| 字段 | 默认值 | 含义 |
| --- | --- | --- |
| `linearDeflection` | `0.1` | 线性偏差，单位 mm |
| `relative` | `false` | 绝对偏差模式 |
| `angularDeflection` | `0.5` | 角度偏差，单位 rad（约 28.6°） |
| `parallel` | `true` | 多面并行三角化 |

法线由 `GmcMesh::tessellate` 按同面法线夹角聚类平均（阈值 60°），平面 Box 的六个面各自得到一致法线。

### 分步流程

1. 解析 `dx/dy/dz`，缺省为 `10.0/10.0/10.0`。
2. `BRepPrimAPI_MakeBox` 生成六面实体，面为 `TopAbs_FACE`，边为 `TopAbs_EDGE`。
3. `addShapeToScene` 检查上下文与形状非空；空形状仅打印 `SY_WARN` 日志并返回 `false`，不弹窗。
4. `ensureInitialized` 确保 OCCT 内核可用。
5. `GmcMesh::tessellate` 在 `linearDeflection = 0.1 mm` 下把六个矩形面离散为三角形，平面面片通常每面 2 个三角形；具体三角形数由三角化器决定，测试只断言网格非空（见第 7 节）。
6. `toAprMeshEntity` 组装 `AprMeshEntity`；`GmcMesh` 中的 `faceIds` 在此不复制（[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L48)），故网格与 B-Rep 面之间没有回指关系。
7. `addEntity` 把实体加入 `SceneManager3D`，空间索引与包围盒随之更新。
8. `attachBRep(entityId, shape)` 把 B-Rep 挂到同一 id；`pushReplaceUndo` 记录“新增一个实体”的撤销记录。
9. 清空选择、标记场景脏、视图 `fitAll`，调用 `MainWindow3D::updateStatusBar()` 刷新状态栏（无逐操作文本），文档 `modified` 置位，并打印 `SY_INFOF` 日志记录 `undoLabel` 与实体 id（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L157-L169)）。

## 5. 输出形态与撤销

- 场景结果：一个 `AprMeshEntity`，实体名 `Box`（`QObject::tr("Box")`，[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L580)），默认材质，加入后成为当前唯一新实体。
- B-Rep：以同一个实体 id 保存在 `BRepModelService3D` 中，后续 `getTwoSelectedBRep`、`getOneSelectedBRep` 能直接读取。
- 撤销：`pushReplaceUndo(ctx, {}, { added }, undoLabel)`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L151)），`undoLabel` 由 lambda 传入 `QObject::tr("Make Box")`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L580)），内部使用 `Eg::ReplaceEntitiesCommand3D`（[SceneUndoCommands3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Include/Engine3D/Edit/SceneUndoCommands3D.h#L116-L158)）。
  - 该命令只交换实体归属：撤销时把新增实体从场景摘出并由命令持有，重做时把同一指针放回场景；不做深拷贝。
  - 因为命令不携带 B-Rep 快照，`attachBRep` 建立的映射在撤销期间仍保留在服务中；重做放回的是同一实体指针、id 不变，映射继续有效。
  - 若撤销状态下执行会调用 `pruneStaleEntries` 的操作（例如导出 STEP，[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1143)），该映射会被清理，重做后实体不再带 B-Rep。
- 无撤销栈时：`pushReplaceUndo` 只把 `removed` 交给栈（此处为空），新增实体保留，命令不可撤销（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L61-L68)）。

## 6. 限制与边界行为

- 无选择约束：命令启用条件为 `Cmd::Always`，任何选择状态下都可执行，执行前不读取选择集。
- 无尺寸对话框：菜单路径固定使用 10 mm 默认值；若要得到其它尺寸，只能由外部调用方在 `params` 中传入 `dx/dy/dz`。
- 尺寸非正值：UI 与 `BRepBuilder` 均无校验，`BRepPrimAPI_MakeBox` 对非正尺寸直接拒绝构造；`BRepBuilder::makeBox` 未捕获异常，UI 层也没有降级处理分支。
- 三角化失败：`QMessageBox` 显示 `Model Error` 与 `Failed to tessellate shape for display.`（[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L132-L142)），不产生实体、不入撤销栈。
- 面拾取限制：网格不携带 `faceIds`，UI 侧只能依赖三角形索引与三角形法线，无法把拾取结果回指到 B-Rep 面。
- 精度限制：B-Rep 为双精度，显示网格为单精度且线性偏差 0.1 mm；两者在视觉上一致，数值上不等价。
- 并发/重复执行：命令无幂等保护，连续执行会不断新增实体，实体 id 由场景分配，互不冲突。

## 7. 源码与测试

关键源码：

- 菜单与命令：`base.json`、`CommandCatalog3D.cpp`、`MenuActionId3D.h`、`OperationId.h`。
- 操作实现：[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L572-L586)（注册）、[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L118-L172)（入场景）。
- 几何内核：[BRepBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/BRepBuilder.cpp#L29-L34)、[TopoShapeImpl.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Private/TopoShapeImpl.h#L65-L69)。
- 三角化与适配：[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L144-L156)、[GmcMeshBridge3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Adapter/GmcMeshBridge3D.cpp#L29-L58)。
- 撤销：[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L56-L79)、[SceneUndoCommands3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Include/Engine3D/Edit/SceneUndoCommands3D.h#L116-L158)。

自动化测试：

- `TopoShapeTest.MakeBox`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L28-L35)）：验证体积等于 `dx*dy*dz`。
- `BRepBuilderTest.MakeBox`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L62-L68)）：验证非空形状。
- `GmcMeshTest.TessellateBox`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L333-L341)）：断言网格有效且三角形数大于 0。
- `GmcMeshFaceIdTest.BoxFaceIdCoversAllFaces`（[GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L788-L807)）、`BoxNormalsPointOutward`（同文件 L828-L842）：验证六个面的 `faceId` 覆盖与法线朝向。
- 未找到针对 `model.make_box` 菜单入口与“纯新增实体撤销/重做”的自动化测试；`BRepUndoRestoreTests` 只覆盖布尔与分割的 B-Rep 恢复（`booleanUndoRestoresBRep` 等）。
- `OperationBus3DTests` 仅覆盖 `OperationId3D` 到字符串/命令 id 的映射（`OperationId3DMappingTest.EveryIdNameMatchesExpected`，[OperationBus3DTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/OperationBus3DTests.cpp#L98-L108)）。
