# STEP 装配导入

> 状态说明（2026-11-10）　状态：现行　维护：架构组

本文描述 STEP（`.step` / `.stp`）导入的完整链路：UI 入口如何进入 GeoModelCore 的装配读取、XCAF 与结构拆分两条读取路径的差异、零件如何被三角化成可渲染网格、B-Rep 与零件元数据如何挂回实体，以及单位、位姿、降级与跳过的边界行为。

## 1. 菜单入口与命令注册

- 菜单：File → Import → STEP...，菜单项 id `file.import_step`，command `file.import_step`；定义见 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L33)，该菜单项的 `workbenches` 同时含 `2D` 与 `3D`。
- 命令目录：[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L45-L52) 的 `File_ImportSTEP`，标签 `STEP (*.step *.stp)`，仅 `kMenuOnly`。
- 同 command 的冗余条目 `File_ImportStep`（旧标签 `Import STEP...`）已删除（2026-11-08）：它与 `File_ImportSTEP` 最终都调用 `importModel(ctx, false, path, stepOnly=true)`，属同一功能的不同写法；此前目录索引按 command id first-wins（[L578-L629](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L578-L629)），该条永远不可达。
- `File_OpenStep`（`file.open_step`，“Open STEP...”）只有目录条目（[L53-L60](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L53-L60)），`base.json` 未挂菜单，点不到。
- 除 STEP 专用菜单外，File → Import → All Supported...（`File_ImportModel`）也会把 `.step/.stp` 分派到同一条 STEP 读取链。

命令实现：[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L922-L932)。未启用 `ENABLE_GEOMODELCORE` 时该命令根本不注册。

## 2. 调用链路

```text
File_ImportSTEP (FileOperations3D.cpp L922)
└─ importModelWithFilter(ctx, false, path, "step")        L717
   └─ importModel(ctx, false, path, stepOnly=true)        L387
      └─ loadModelFile (STEP 分支)                        L115-L216
         ├─ brepService->ensureInitialized()              L125
         ├─ document().loadStepAssembly(path)             GeoModelImpl.cpp L93
         │  └─ readStepAssemblyParts(...)                 GmcAssemblyImpl.cpp L670
         │     ├─ readWithXcaf(...)     （启用 XCAF）      L573
         │     └─ readStructureOnly(...)（降级/无 XCAF）   L229
         ├─ lastAssemblyInfo() / partCount()              L138-L139
         └─ 逐件 getPartShape / getPartInfo               L155-L191
            ├─ tessellateToEntity(...)                   BRepModelService3D.cpp L151
            │  └─ GmcMesh::tessellate(...)               GmcMesh.cpp L189
            └─ applyPartColor(...) 仅 RGB                 L88-L95
      ├─ sceneManager->addEntities(partPtrs)              L647
      └─ attachBRep / attachPartInfo                      L650-L665
```

`GeoModelImpl::loadStepAssembly` 成功后调用 `rebuildShapeFromParts()` 把多零件合成一个 `TopoDS_Compound`，单件则直接复用（[GeoModelImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GeoModelImpl.cpp#L240-L262)）。UI 侧只使用 `parts` 数组，不使用这个复合整体形状。

## 3. 输入与参数

- 支持扩展名：`.step`、`.stp`，大小写不敏感（先 `toLower`，再 `endsWith`），见 [L113-L116](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L113-L116)。
- 对话框过滤器：`STEP B-Rep (*.step *.STEP *.stp *.STP)` 优先，另有 All Supported 与 All Files；标题 `Import STEP Model`（追加语义），见 [L453-L461](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L453-L461)。过滤器不限制实际可解析格式，分派仍看扩展名。
- 无用户可调参数：网格化精度固定取 `BRepModelService3D::meshParams()` 的默认值，即 `linearDeflection = 0.1`、`relative = false`、`angularDeflection = 0.5`（弧度）、`parallel = true`，权威定义在 [GmcTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L97-L107)。
- 零件数上限 `kMaxParts = 10000`（[GmcAssemblyImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L48)）。
- 单位：STEP 读取器把源单位统一换算为毫米；`GmcPartInfo::unitToMm` 只记录“源单位 → 毫米”的追溯系数，几何不得二次缩放（契约见 [GmcStepAssembly.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcStepAssembly.h#L44-L49)）。
- 编译开关：`ENABLE_GEOMODELCORE` 决定命令是否注册；`GMC_ENABLE_STEP_IO` 决定 STEP 读写是否编入；`GMC_ENABLE_XCAF` 决定是否能读到名称/颜色/材质。

## 4. 算法原理与分步流程

### 4.1 XCAF 路径（属性完整）

入口 `readWithXcaf`（[GmcAssemblyImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L573-L664)）：

1. 初始化内核 `GmcKernel::instance().ensureInitialized()`。
2. 新建 `TDocStd_Document("MDTV-XCAF")`，并用 `XCAFDoc_DocumentTool::SetLengthUnit(document, 1.0, UnitsMethods_LengthUnit_Millimeter)` 把文档单位固定为毫米。
3. `STEPCAFControl_Reader` 打开颜色、名称、图层、属性、材质五个读取模式，并 `SetSystemLengthUnit(1.0)`。
4. `ReadFile`；返回 `IFSelect_RetVoid` 视为“空或不可读”，返回非 `RetDone` 视为解析失败。
5. `Transfer(document)` 把 STEP 转成 XCAF 文档。
6. 取 `XCAFDoc_ShapeTool` 与 `XCAFDoc_ColorTool`；`shapeTool` 为空则失败。
7. 对 `GetFreeShapes` 的每个根标签递归 `collectLabelParts`：
   - `IsAssembly(label)` 为真 → 取 `GetComponents` 递归子件，不把装配节点本身当零件；
   - 否则 `shapeTool->GetShape(label)` 取形状。该形状已带装配位姿，坐标是世界坐标。
8. `resolveLabelMeta` 按“实例标签优先、被引用形状兜底”的顺序解析名称、颜色、材质（[L390-L416](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L390-L416)）。
   - 名称走 `TDataStd_Name`，转 ASCII 后 trim；`=>[...]` 形式的 XCAF 实例名视为无效并回退。
   - 颜色依次尝试 `XCAFDoc_ColorSurf`、`ColorGen`、`ColorCurv`；打包为 `0xAARRGGBB`。
   - 材质走 `XCAFDoc_MaterialTool::GetMaterial`。
9. `explodeSolids(shape)` 把形状拆成 `TopAbs_SOLID`；没有任何 SOLID 时整体作为一件（[L145-L162](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L145-L162)）。
10. 数量检查：`parts.size() + solids.size() > kMaxParts` 时置 `GmcStepAssemblyStatus_PartLimitExceeded` 并失败。
11. 每个 SOLID 生成一条 `GmcAssemblyPartRecord`：
    - 有名称时，同一标签拆出多件且 `index > 0` 追加 `_2`、`_3`；首件保持原名；
    - 无名称时 `assignDefaultName` 生成 `Part_<n>` 且 `hasName = false`；
    - 标签颜色缺失时，再用 `readUniformSolidColor` 尝试“所有面颜色一致”回填；
    - `unitToMm` 初始 1.0，`volume` 用 `BRepGProp::VolumeProperties` 计算；
    - `locationToColumnMajor` 把 `shape.Location()` 的 `gp_Trsf` 写入列主序 4×4（平移在 `transform[12..14]`）。
12. 从 `reader.ChangeReader().FileUnits(...)` 取源长度单位名，`unitNameToMm` 换算追溯系数（cm=10、mm=1、um=0.001、inch=25.4、foot=304.8、yard=914.4、mil/thou=0.0254、m=1000），见 [L84-L140](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L84-L140)。
13. `summarize` 汇总 `partCount`、`hasColors`、`hasNames`，并逐件补齐 `NameMissing` / `ColorMissing` / `MaterialMissing` 状态位；`info.unitName` 固定写 `"mm"`（几何坐标单位）。

### 4.2 结构拆分降级路径

`readStructureOnly`（[L229-L286](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L229-L286)）：

1. `STEPControl_Reader` 打开文件，`SetSystemLengthUnit(1.0)` 表示目标系统单位为毫米，换算由读取器内部完成。
2. `TransferRoots()` + `OneShape()` 得到整体形状；空形状报 `STEP file contains no transferable geometry`。
3. `explodeSolids` 拆件，超过上限同样失败。
4. 状态位固定置 `XcafUnavailable | StructureOnly | UnitUnknown`；拆出多件再置 `SolidSplit`。
5. 每件颜色默认 `0xFF808080`、`hasColor = false`、`hasMaterial = false`、`unitToMm = 1.0`、名称 `Part_<n>`、`hasName = false`、`volume` 照算、`partIndex` 顺序递增。
6. `summarize` 追加缺名/缺色/缺材质位。

降级判定在 `readStepAssemblyParts`（[L670-L722](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L670-L722)）：

- 路径为空 → `InvalidArgument`；文件不存在 → `FileNotFound`。
- XCAF 成功直接返回。
- XCAF 返回 `FileNotFound` / `FileFormatError` / `InvalidArgument` 时**不降级**，把真实原因返回。
- 其余失败（包括 XCAF 抛 `Standard_Failure`）在 `allowStructureOnlyFallback` 为真时走结构拆分，并追加 `XcafReadFailed` 位；否则返回 `NotSupported`。
- `GeoModel::loadStepAssembly` 始终传 `allowStructureOnlyFallback = true`；`GmcStepAssembly::read` 传 `false`，所以直接调用内核读取时无 XCAF 会显式返回 `NotSupported`。

### 4.3 UI 层：三角化、命名、着色

`loadModelFile` 的 STEP 分支（[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L115-L216)）：

1. `ensureInitialized()` 后 `document().loadStepAssembly(path)`；`status.ok == false` 直接报错。
2. `lastAssemblyInfo()` 与 `partCount()`；`partCount == 0` 报 “STEP file loaded but contains no valid part.”。
3. `statusFlags & GmcStepAssemblyStatus_StructureOnly` 非零时打印 `SY_WARNF`，随后继续导入（只是没有名称/颜色/材质）。
4. 逐件 `getPartShape`：空指针或 `isNull()` → `SY_WARNF` 跳过。
5. 逐件 `getPartInfo`：失败 → `SY_WARNF` 跳过。
6. 名称：`hasName` 时用 `partInfo.name`；否则 `baseName + "_" + (index + 1)`，`baseName` 取文件 `completeBaseName`。
7. `tessellateToEntity(*shape, name)`：内部 `GmcMeshBridge3D::tessellateToEntity` → `GmcMesh::tessellate`，失败（空指针或顶点为空）→ `SY_WARNF` 跳过。
8. `hasColor` 时 `applyPartColor`：把 `0xAARRGGBB` 的 R/G/B 各除以 255 写入 `diffuseColor`，`ambientColor` 取 0.3 倍；alpha 被忽略。
9. 全部零件都被跳过时报 “Failed to tessellate STEP model for display.”，返回失败。
10. 成功时打印一条汇总日志：零件总数、成功数、是否有颜色/名称、状态位。

三角化细节（[GmcMesh.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcMesh.cpp#L189-L253)）：

1. 先 `BRepBuilderAPI_Copy` 复制一份独立形状，避免把三角化写进调用方共享的 `TShape`；复制失败退回原形状。
2. `BRepMesh_IncrementalMesh(meshShape, linearDeflection, relative, angularDeflection, parallel)` 执行网格化。
3. 焊接容差 `wtol = max(1e-7, linearDeflection * 1e-3)`，按容差网格量化顶点坐标。
4. Pass 1：每个三角形的法线累加到三个顶点对应的法线簇；簇内方向夹角余弦大于 `cos(60°)` 就合并（平滑），否则新建簇（硬边）。
5. Pass 2：逐三角形输出 float 顶点与匹配簇的平滑法线，同时写入 `faceIds`（三角形 → B-Rep 面序号）。
6. `GmcMeshBridge3D::toAprMeshEntity` 把 `vertices`/`normals` 转成 `Ut::Vec3f` 平铺到 `AprMeshEntity`；`normals.size() < vertices.size()` 时不拷贝法线。

## 5. 输出形态与撤销

- 每个有效 STEP 零件对应一个 `Eg::AprMeshEntity`，实体名优先取文件内产品名，缺失时 `文件基名_序号`。
- B-Rep 以 `shared_ptr<TopoShape>` 经 `attachBRep` 挂到实体 id；`attachBRep` 同时重建拾取缓存 BVH（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L190-L202)）。
- 零件元数据经 `attachPartInfo` 复制到 `entityPartInfo`，供后续按材质/厚度分组、排样使用；版本守卫见 [L211-L226](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L211-L226)。
- 入场景顺序：先 `addEntities` 批量入库，再逐件挂 B-Rep 与 partInfo（[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L647-L665)）。
- 追加导入（`File_ImportSTEP`）压 `ReplaceEntitiesCommand3D`，撤销把新增实体摘出、重做放回；B-Rep 映射不会被命令搬运。
- 通过 Recent Files 打开 STEP 时 `replaceScene = true`：先清撤销栈、清 B-Rep 映射、`clearScene`，导入成功后 `SceneReplaceCommand3D` + `markSavePoint`。
- 场景文档 `.sy3d/.syx` 不保存 B-Rep 拓扑（[SceneDocumentIO3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Storage/SceneDocumentIO3D.cpp#L110-L115)），保存再打开后必须重新从 STEP 源文件导入才能恢复。

## 6. 限制与边界行为

**明确报错（返回失败并弹框）**

- `B-Rep modeling service is not available.`：`brepService` 为空。
- `Failed to load STEP file:\n%1`：`loadStepAssembly` 的 `GmcStatus.message`。
- `STEP file loaded but contains no valid part.`：`partCount == 0`。
- `Failed to tessellate STEP model for display.`：所有零件都被跳过。
- `STEP import is not available in this build.\nRebuild with OpenCASCADE data exchange (TKDESTEP) enabled.`：`GMC_ENABLE_STEP_IO` 关闭。
- `STEP file import is not available in this build.\nPlease enable GeoModelCore module in Config.cmake and rebuild.`：`ENABLE_GEOMODELCORE` 关闭。
- 内核层：`STEP file is empty or unreadable`、`STEP file parsing failed`、`STEP file contains no transferable geometry`、`STEP transfer to XCAF document failed`、`STEP file contains no readable parts`、`STEP part count exceeds limit`。

**静默跳过（只写日志，不中断整批）**

- 空形状：`STEP part %zu has null shape, skipped`。
- 元数据缺失：`STEP part %zu info unavailable, skipped`。
- 三角化失败：`STEP part %zu tessellation failed, skipped`。
- structure-only：`STEP read degraded to structure-only`，导入继续。

**语义边界**

- **structure-only**：无名称/颜色/材质，名称统一 `Part_<n>`，颜色统一灰 `0xFF808080`；`GmcAssemblyInfo::hasNames/hasColors` 均为 false。
- **单位**：几何已由读取器换算到毫米，`unitToMm` 仅为追溯；不得再次缩放。XCAF 路径 `unitName` 记录源单位名，结构拆分路径 `unitName` 为空并置 `UnitUnknown`。
- **位姿**：`getPartShape` 返回的几何已烘焙装配位姿；`partInfo.transform` 只是记录，渲染/加工不得二次应用，否则重复位移。
- **颜色 alpha**：`AprMeshEntity` 只有 RGB，alpha 丢弃。
- **零件上限**：超过 10000 件直接失败，不做截断。
- **失败回滚**：替换语义下 UI 层 `restoreBeforeEntities` 恢复导入前快照；追加语义下场景尚未改动，无需恢复。

## 7. 源码与测试

源码：

- UI 入口：[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L97-L216)
- 装配读取：[GmcAssemblyImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcAssemblyImpl.cpp#L229-L722)
- 数据结构与状态位：[GmcStepAssembly.h](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcStepAssembly.h#L24-L95)
- 文档容器：[GeoModelImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GeoModelImpl.cpp#L93-L116)
- 三角化：[GmcMesh.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GmcMesh.cpp#L189-L253)
- B-Rep 挂接：[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L190-L226)

测试：

- [GmcAssemblyTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcAssemblyTests.cpp#L171-L269) `GmcStepAssembly.SplitPartsKeepsMetadata`：两件装配、名称/颜色、体积、包围盒、位姿平移。
- [GmcAssemblyTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcAssemblyTests.cpp#L271-L304) `GmcStepAssembly.UnitConversionInchToMillimeter`：英寸立方体读成 25.4 mm，`unitToMm = 25.4`。
- [GmcAssemblyTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcAssemblyTests.cpp#L306-L328) `GmcStepAssembly.ReadWithoutXcafIsExplicit`：无 XCAF 时直接读取显式 `NotSupported`，`GeoModel` 回退结构拆分并置 `StructureOnly`。
- [GmcCoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcCoreTests.cpp#L333-L374) `GmcMeshTest.TessellateBox`、`GmcMeshTest.SmoothVertexNormalsKeepHardEdges` 覆盖三角化与法线平滑。
- 未找到直接覆盖 `loadModelFile` STEP 分支或 `File_ImportSTEP` 的 UI 层测试。

## 8. 旧文档差异

- [00-索引与算法对照表.md](00-索引与算法对照表.md) 第 5 节写“先 `attachBRep` 再入场景”，代码是先 `addEntities` 再挂接。见 [FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L647-L665)。
- [3D撤销与B-Rep同步优化.md](3D撤销与B-Rep同步优化.md) 的“B-Rep 拓扑不持久化、重开需重新挂接”与 `SceneDocumentIO3D` 的注释一致，无冲突；其“文档级替换清空映射仅 Open/New”范围偏窄，实际通过 Import 打开 `.sy3d/.syx` 也会清空。
