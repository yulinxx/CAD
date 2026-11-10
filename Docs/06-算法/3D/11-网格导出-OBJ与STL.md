# 网格导出 — OBJ 与 STL

> 状态说明（2026-11-10）　状态：现行　维护：架构组

本文描述 File → Export 下 OBJ 与 STL 两条网格导出链路：导出的范围、退化三角形过滤、二进制 STL 的字节布局、OBJ 的顶点/法线/面索引规则，以及菜单元数据与实际行为之间的差异。

## 1. 菜单入口与命令注册

| 菜单路径 | 菜单项 id | command id | `OperationId3D` | 定义 |
|---|---|---|---|---|
| File → Export → OBJ | `file.export_obj` | `file.export_obj` | `File_ExportOBJ` | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L42) |
| File → Export → STL | `file.export_stl` | `file.export_stl` | `File_ExportSTL` | [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L43) |

命令目录见 [CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L138-L153)：`File_ExportOBJ` 与 `File_ExportSTL` 都是 `kMenuOnly`，并带 `Cmd::RequiresSelection`。该标志只用于菜单启用条件（没有选中实体时置灰），**不改变导出范围**。

命令注册见 [FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L1118-L1134)，两者分别转调 `exportSceneToFile(ctx, "obj")` 与 `exportSceneToFile(ctx, "stl")`。同一函数里的 STEP 分支（[L816-L821](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L816-L821)）没有调用方，STEP 导出走独立实现，见 [12-STEP-B-Rep导出.md](12-STEP-B-Rep导出.md)。

## 2. 调用链路

```text
File_ExportOBJ / File_ExportSTL            FileOperations3D.cpp L1118-L1134
└─ exportSceneToFile(ctx, "obj" | "stl")   L798-L887
   ├─ QFileDialog::getSaveFileName         L823-L828
   ├─ Export3D::writeObjFile / writeStlFile  MeshExport3D.cpp L267 / L256
   │  └─ writeObjFileImpl / writeStlFileImpl L231 / L131
   │     └─ scene->forEachEntity(collectXxxEntity, &ctx)  L243 / L149
   └─ 成功/失败提示 + 日志                 L859-L886
```

`exportSceneToFile` 只做边界兜底与 UI 提示；几何写出与统计在 `MeshExport3D` 内完成（[MeshExport3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.h#L11-L33)）。`forEachEntity` 遍历场景内全部实体，不按可见性、锁定或选择状态过滤。

## 3. 输入与参数

| 项 | OBJ | STL |
|---|---|---|
| 保存过滤器 | `OBJ Files (*.obj);;All Files (*.*)` | `STL Files (*.stl);;All Files (*.*)` |
| 默认文件名 | `scene.obj` | `scene.stl` |
| 对话框标题 | `Export OBJ` | `Export STL` |
| 编码/格式 | 文本 OBJ | 二进制 STL |
| 单位 | 按场景坐标原值（毫米） | 同左 |
| 精度选项 | 无 | 无 |
| 坐标系 | 右手系 Z 向上，原样输出 | 同左 |

定义位置：[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L804-L828)。代码把对话框返回值直接作为路径使用，不强制补全或校验后缀。

导出范围由代码固定为**整个场景**：

- `writeStlFileImpl` 调 `scene->forEachEntity(collectStlEntity, &ctx)`（[MeshExport3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L149)）。
- `writeObjFileImpl` 调 `scene->forEachEntity(collectObjEntity, &ctx)`（[L243](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L243)）。
- `forEachEntity` 只按场景容器顺序回调，不读取 `selected()` 或 `visible()`。

## 4. 算法原理与分步流程

### 4.1 公共退化判定

1. 判定常量：`kMinValidTriangleAreaMm2 = 1e-12`，叉积长度平方阈值 `kMinCrossLengthSq = (2 * 1e-12)^2`（[L27-L28](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L27-L28)）。
2. `isFinitePoint` 要求三个坐标都是有限值，NaN/Inf 直接判退化（[L31-L35](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L31-L35)）。
3. `isDegenerateTriangle` 用双精度边向量计算叉积长度平方，小于阈值或非有限即退化（[L39-L63](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L39-L63)）。
4. `triangleNormal` 用 float 叉积归一化；长度为零或非有限返回 false，该三角形再计入一次“跳过”（[L67-L77](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L67-L77)）。
5. 阈值不随模型包围盒尺度变化，因此不同尺寸模型的判定口径一致。

### 4.2 二进制 STL

1. 以 `QIODevice::WriteOnly` 打开；失败返回 `Cannot open file for writing: %1`。
2. 写 80 字节头：`QByteArray header(80, 0)`，`memcpy` 写入 19 字节 `SanYi 3D STL export`（[L143-L145](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L143-L145)）。用固定 80 字节是为避免 `replace` 缩短数组导致后续数据错位一字节。
3. 在偏移 80 先写 4 字节占位三角形数 0（[L146-L147](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L146-L147)）。
4. 遍历实体：顶点数 < 3 的实体整体跳过，且不计入 `totalTriangles`（[L88-L93](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L88-L93)）。
5. 每个三角形先判退化；通过则写“面法线 3 float + v0/v1/v2 各 3 float + `quint16` 属性 0”，共 50 字节（[L113-L127](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L113-L127)）。法线由几何叉积计算，**不使用**实体里存的平滑法线。
6. 全部写完后 `seek(80)` 回填实际写出的三角形数，退化面不占文件槽位（[L150-L153](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L150-L153)）。
7. `file.error() != NoError` 返回 `Failed to write file: %1`；否则成功并返回统计 `totalTriangles / writtenTriangles / skippedDegenerateTriangles`。

### 4.3 OBJ

1. 以 `WriteOnly | Text` 打开；失败文本同上。
2. 写文件头 `# SanYi 3D OBJ export`。
3. 遍历实体（顶点数 < 3 整体跳过），每个实体先写 `o <name>`，空名回退 `mesh`（[L179-L180](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L179-L180)）。
4. 先一次性写出该实体全部 `v` 行（包含属于退化面的顶点），保证后续面索引连续可算。
5. 仅当 `normals.size() == vertices.size()` 才写 `vn` 行并按 `//` 形式引用；数量不匹配则整块法线不写、面退化为纯顶点形式（[L186-L193](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L186-L193)）。
6. 逐三角形判退化；通过则写 `f`，索引 = 实体内局部基准 + 全局偏移，1 基（[L208-L221](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L208-L221)）。
   - 有法线：`a//na b//nb c//nc`，顶点与法线一一对应；
   - 无法线：`a b c`。
7. 每实体结束把 `vertexOffset += vertices.size()`；有法线时 `normalOffset += normals.size()`，保证跨实体索引正确（[L224-L228](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L224-L228)）。
8. `flush()` 后检查 `file.error()`；失败返回 `Failed to write file: %1`。

## 5. 输出形态与撤销

- 导出是纯读操作：不新增/删除实体，不修改 `AprMeshEntity`，不写 B-Rep，也不产生任何撤销命令。
- 忽略选择集与可见性：隐藏实体、锁定实体、未选中实体都会写入。菜单的 `Cmd::RequiresSelection` 只控制“能否点”，不控制“写什么”。
- 无 B-Rep 的实体在 OBJ/STL 中没有损失，因为这两者本来就只承载三角网格；反之，STEP 导出会跳过这些实体（见第 12 篇）。
- 成功提示固定为 `Exported %1 of %2 triangles to:\n%3\nSkipped %4 degenerate triangles.`（[L873-L879](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L873-L879)），页面标题 `Export`。
- 场景无实体或全部退化时仍算成功：STL 写出 80 字节头 + 计数 0，OBJ 只写文件头，提示 `Exported 0 of N ...`。

## 6. 限制与边界行为

**明确报错（`Export Error` 弹框，文本为 `Failed to export file:\n%1\n%2`）**

- `Scene is not available.`：`scene` 为空（[MeshExport3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L256-L265)）。
- `Cannot open file for writing: %1`：文件无法创建或无权限。
- `Failed to write file: %1`：写盘错误（空间不足、设备断开等）。
- `Export format is not supported: %1`：`exportSceneToFile` 收到非 obj/stl 的格式（当前命令路径不会触发）。
- 底层抛出标准异常时直接用 `ex.what()`；其它异常为 `Unknown error during export.`（[L847-L857](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L847-L857)）。

**静默过滤 / 边界语义**

- 退化三角形（零面积、重复顶点、NaN/Inf、float 叉积下溢）不写盘，只计入 `skippedDegenerateTriangles` 并在成功对话框里明确告知，不静默丢面。
- 顶点数 < 3 的实体既不计入总数也不报错。
- STL 不写实体名、不写颜色、不写平滑法线；所有法线按几何重建。
- OBJ 写实体名但同一名字可能重复；不写 `mtllib` / `usemtl` / `vt`，材质与纹理不导出。
- 二进制 STL 的三角形数用 `quint32` 回填；写出面数超过 2^32-1 会被截断（实际受内存限制，几乎不可达）。
- 阈值固定为 1e-12 mm²：小于该面积的面被丢弃，因此极小特征不会出现在导出文件中。
- 隐藏/锁定实体照样导出；没有“仅导出选中”的代码路径。

## 7. 源码与测试

源码：

- UI 入口与提示：[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L798-L887)
- 写出实现：[MeshExport3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.cpp#L27-L276)
- 结果结构：[MeshExport3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/MeshExport3D.h#L11-L33)

测试见 [MeshExport3DTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Test/MeshExport3DTests.cpp#L153-L327)，全部为 `MeshExport3DTest` 命名空间：

- `StlSkipsDegenerateTriangles`
- `StlLegalMeshBytesMatchLegacyOutput`（逐字节兼容性）
- `StlMixedMeshSkipCountIsExact`
- `StlIgnoresShortNormalArray`
- `StlAllDegenerateWritesZeroTriangles`
- `ObjSkipsDegenerateTrianglesAndReferencesNormals`
- `ObjWithoutNormalsWritesPlainFaces`

未找到覆盖 `File_ExportOBJ/File_ExportSTL` 对话框流程或“整场景 vs 选中”行为的 UI 层测试。

## 8. 旧文档差异

- [00-索引与算法对照表.md](00-索引与算法对照表.md) 记录“OBJ/STL 导出共用一个写盘管线、只导出网格、不写 B-Rep”，与代码一致，无冲突。
- 该索引只说明“OBJ/STL 导出共用写盘管线”，未记录导出范围；本文核实为整场景，`Cmd::RequiresSelection` 仅是菜单启用条件，不代表只导出选中项。
- [3D撤销与B-Rep同步优化.md](3D撤销与B-Rep同步优化.md) 不涉及网格导出，无冲突。
