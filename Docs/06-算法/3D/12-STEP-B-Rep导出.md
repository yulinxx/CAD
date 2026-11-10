# STEP B-Rep 导出

> 状态说明（2026-11-10）　状态：现行　维护：架构组

本文描述 File → Export → STEP 的完整链路：命令入口、场景 B-Rep 收集、多实体布尔融合、`STEPControl_Writer` 写盘、跳过计数与错误回传，以及“只导出 B-Rep、丢失装配层级”等结构性限制。

## 1. 菜单入口与命令注册

- 菜单：File → Export → STEP，菜单项 `file.export_step`，command `file.export_step`，见 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L44)。
- 命令目录：`File_ExportSTEP`，标签 `STEP (*.step *.stp)`，带 `Cmd::RequiresSelection`（仅菜单启用条件），见 [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L155-L163)。
- 命令注册被 `#ifdef ENABLE_GEOMODELCORE` 包裹，见 [FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1136-L1178)；未启用 GeoModelCore 时命令不存在。
- 另有 `File_SaveBRepStep`（`file.save_brep_step`，标签 `Save B-Rep (STEP)`）与 `File_SaveBRepStepAs`（`file.save_brep_step_as`，标签 `Save B-Rep As STEP...`），目录条目见 [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L112-L127)，实现见 [ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L486-L510)、[L685-L710](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L685-L710)，但 `base.json` 没有对应菜单项，**点不到**。
- `File_OpenStep` 同理：有目录条目与实现（[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1092)），无菜单项，不可达。

## 2. 调用链路

```text
File_ExportSTEP                                    FileOperations3D.cpp L1136-L1176
├─ 上下文校验（brepService / sceneManager / mainWindow）   L1139-L1142
├─ brepService->pruneStaleEntries(sceneManager)          L1143
├─ QFileDialog::getSaveFileName（默认=当前文档路径）       L1144-L1151
├─ brepService->saveSceneAsStep(scene, path, buf, 512, &skipped)  L1152-L1158
│  └─ buildCompositeShape(scene, &skipped)               BRepModelService3D.cpp L465
│     ├─ scene->forEachEntity(...)                       L493
│     ├─ getBRep(entity->id)                              L501
│     └─ GmcBoolean::fuse(composite, shape, {})           L516-L522
│  ├─ document->setShape(composite)                       L569
│  └─ document->saveStepFile(path)                        L570
│     └─ STEPControl_Writer::Transfer / Write             GeoModelImpl.cpp L130-L141
├─ 失败 → QMessageBox `STEP Export Error`                 L1160-L1164
└─ skipped > 0 → QMessageBox `STEP Export Warning`        L1166-L1173
```

## 3. 输入与参数

| 项 | 值 | 位置 |
|---|---|---|
| 保存过滤器 | `STEP Files (*.step *.stp);;All Files (*.*)` | [L1147](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1147) |
| 标题 | `Export STEP` | [L1145](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1145) |
| 默认路径 | `documentManager->currentFilePath()`，没有则为空串 | [L1146](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1146) |
| 后缀强制 | 无，按对话框返回值原样写盘 | 同上 |
| 错误缓冲 | `char errorBuffer[512]` | [L1152](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1152) |
| 跳过计数 | `size_t skippedEntities`，出参 | [L1153](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1153) |
| 单位 | 场景坐标即毫米，`STEPControl_AsIs` 原样写出 | [GeoModelImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GeoModelImpl.cpp#L131) |
| 布尔参数 | `GmcBoolean::fuse(a, b, {})`，使用默认容差 | [BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L516-L517) |
| 编译开关 | `ENABLE_GEOMODELCORE`（命令是否存在）、`GMC_ENABLE_STEP_IO`（是否可写） | 两处 `#ifdef` |

默认路径直接沿用当前 `.sy3d` 文档路径，不改成 `scene.step`，用户需自行改后缀；对话框过滤器不会阻止写入任意后缀。

## 4. 算法原理与分步流程

1. **清理陈旧映射**：`pruneStaleEntries` 先收集场景现存实体 id 集合，再删除 `entityBRep`、`pickCache`、`entityPartInfo` 中不在集合内的条目（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L322-L366)）。这修复“撤销后实体已消失、映射仍在”的残留。
2. **遍历场景收集 B-Rep**：`buildCompositeShape` 通过 `forEachEntity` 遍历全部实体（不看可见性/锁定/选择）；`getBRep(entity->id)` 为空或 `isNull()` 时 `++skipped` 并继续（[L493-L507](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L493-L507)）。
3. **单实体直接复用**：第一个有效形状直接赋给 `composite`，不做克隆、不做平移、不做融合（[L509-L514](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L509-L514)）。
4. **多实体顺序融合**：后续每个形状与当前 `composite` 调 `GmcBoolean::fuse`，选项为空（默认容差），结果替换 `composite`（[L516-L522](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L516-L522)）。
5. **融合失败即整体失败**：任一 `fuse` 返回空或 `isNull()`，置 `failed = true`，后续实体不再处理，函数最终返回 `nullptr`。失败原因只写日志，不写入 `errorBuffer`。
6. **空结果判定**：`composite` 为空或 `isNull()` 时 `saveSceneAsStep` 写 `No B-Rep geometry to export as STEP.` 到错误缓冲并返回 false（[L556-L566](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L556-L566)）。融合失败也会落到这条文本。
7. **装入文档**：`document->setShape(composite)` 先 `resetParts()` 再复制原生 `TopoDS_Shape`（[GeoModelImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GeoModelImpl.cpp#L176-L192)）。
8. **写盘**：`saveStepFile` 检查形状非空 → `STEPControl_Writer::Transfer(shape, STEPControl_AsIs)` → `Write(path)`；任一步非 `IFSelect_RetDone` 记录 `lastStepIoError`（[L118-L149](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GeoModelImpl.cpp#L118-L149)）。
9. **错误回传**：`lastStepIoSucceeded()` 为 false 时把 `lastStepIoError` 拷进 512 字节缓冲并返回 false（[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L573-L585)）。
10. **UI 提示**：失败弹 `STEP Export Error` / `Failed to export STEP file:\n%1`；成功但 `skippedEntities > 0` 弹 `STEP Export Warning` / `%1 object(s) without B-Rep geometry were skipped in the STEP file.`；两者都不发生则无任何成功提示，只写 `SY_INFOF`。

**几何一致性**：导出取的是每个实体自己的 B-Rep（`getBRep`），不是网格对齐后的世界几何。实体与 B-Rep 的平移同步依赖变换命令调用 `applyTransformToBRep`（[L271-L306](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L271-L306)）。`getWorldBRepForEntity` 只做包围盒中心平移对齐（[L253-L269](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L253-L269)），且只供布尔/切割类功能使用，导出路径不调用它。

## 5. 输出形态与撤销

- **写出的内容**：一个 `TopoDS_Shape`（单实体原形状，或多实体融合后的单一实体）。没有装配结构、没有 `PRODUCT`/`NEXT_ASSEMBLY_USAGE_OCCURRENCE` 层级、没有名称、没有颜色、没有材质。
- **不修改场景**：不新增/删除实体，不改变选择，不触发重绘，不产生撤销命令。
- **副作用**：`document->setShape` 会清空 `m_parts`，因此导出后当前文档的 `partCount()` 变为 0，`getPartInfo` 不再可用；之后再依赖装配元数据的操作需要重新导入 STEP。
- **不建立保存点**：导出成功后文档的 modified 状态与保存点不变。
- **B-Rep 映射**：导出只读 `entityBRep`，不清空也不改写；`pruneStaleEntries` 仅在开始时删除已消失实体的条目。
- **失败不破坏场景**：融合或写盘失败只返回 false，场景实体与 B-Rep 映射保持不变。

## 6. 限制与边界行为

**明确报错**

| 文本 | 触发 |
|---|---|
| `No B-Rep geometry to export as STEP.` | 场景无任何 B-Rep，或任意一次 `fuse` 失败 |
| `STEP IO is not enabled in this build (GMC_ENABLE_STEP_IO).` | `saveSceneAsStep` 在未编译 STEP IO 时 |
| `No shape to write.` | 文档形状为空 |
| `STEP Transfer failed.` | `Transfer` 未返回 `IFSelect_RetDone` |
| `STEP Write failed.` | `Write` 未返回 `IFSelect_RetDone` |
| `STEP IO not compiled (GMC_ENABLE_STEP_IO=0).` | `GeoModelImpl::saveStepFile` 编译期关闭 |
| `Failed to export STEP file:\n%1` | UI 层统一失败弹框，`%1` 为上述之一 |

**跳过（成功但告警）**

- 无 B-Rep 的实体被计数并跳过，成功后弹 `STEP Export Warning`。包括全部 OBJ/STL 导入的网格实体、B-Rep 已因撤销/删除而失效的实体。
- 隐藏或锁定的实体不作区分：有 B-Rep 就导出，没有就跳过。

**结构性限制**

- **单实体路径**：完全不调用 `fuse`，直接写原形状，可能比多实体路径保留更多原始面结构。
- **多实体路径**：`BRepAlgoAPI_Fuse` 语义的布尔融合把多个实体合成一个 solid/compound，装配位姿被烘焙进几何，层级、名称、颜色、材质全部丢失。
- **融合失败文本误导**：真正的布尔失败原因在日志里（`composite fuse failed: ...`），用户看到的却是 `No B-Rep geometry to export as STEP.`。
- **无世界对齐**：若实体的 B-Rep 没有随网格变换同步，导出位置会与屏幕显示不一致；导出不做 `alignShapeToMesh`。
- **`alignShapeToMesh` 只平移**：即使被调用，也只按包围盒中心平移，不处理旋转与缩放，不能替代真正的位姿同步。
- **选择集无效**：`Cmd::RequiresSelection` 只是菜单启用条件，导出始终覆盖整场景。
- **无成功提示**：未跳过任何实体时，用户只能从日志确认结果。

## 7. 源码与测试

源码：

- UI 入口：[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1136-L1178)
- 服务层：[BRepModelService3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Service/BRepModelService3D.cpp#L465-L602)
- 文档写盘：[GeoModelImpl.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Source/GeoModelImpl.cpp#L118-L149)
- 未挂菜单的保存命令：[ModelOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/ModelOperations3D.cpp#L486-L510)

测试：

- [BRepUndoRestoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/BRepUndoRestoreTests.cpp#L273-L322) `booleanUndoThenStepExport`：两个分离盒子做布尔并集后撤销，源实体 B-Rep 恢复；`saveSceneAsStep` 成功且 `skipped == 0`，导出文本中 `MANIFOLD_SOLID_BREP` 恰好出现 2 次（对应两个盒子）。
- [BRepUndoRestoreTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/BRepUndoRestoreTests.cpp#L225-L272) `booleanUndoRestoresBRep` 覆盖布尔撤销后的 B-Rep 恢复，是导出前置状态。
- [GmcAssemblyTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/Test/GmcAssemblyTests.cpp#L171-L269) 覆盖 STEP 读取侧（属性与位姿），可作为“导出丢失元数据”的对照。
- 未找到针对 `saveSceneAsStep` 的融合失败、空场景、写盘失败分支的单元测试。

## 8. 旧文档差异

- [3D撤销与B-Rep同步优化.md](3D撤销与B-Rep同步优化.md) 的“B-Rep 拓扑不持久化、布尔结果撤销后映射失效、导出前需 prune”均与代码一致，无冲突。
- [00-索引与算法对照表.md](00-索引与算法对照表.md) 已按代码修正为“先入场景、再 `attachBRep`”，并区分了仅布尔/分割携带 B-Rep 快照的撤销命令，与代码一致。
- [00-索引与算法对照表.md](00-索引与算法对照表.md) 已正确标注 `File_SaveBRepStep` / `File_SaveBRepStepAs` / `File_OpenStep` 未挂菜单；重复的 `File_ImportStep` 条目已删除（2026-11-08），`file.import_step` 现只保留 `File_ImportSTEP` 一条，与代码一致。
