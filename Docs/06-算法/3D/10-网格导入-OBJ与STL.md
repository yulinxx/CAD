# 网格导入 — OBJ 与 STL

> 状态说明（2026-11-10）　状态：现行　维护：架构组

本文描述 OBJ / STL 两条网格导入路径：它们共用 FileIO 解析框架与 `FioEntityConverter`，最终都只产生一个合并后的三角网格实体。偏向装配与 B-Rep 的部分见 [09-STEP装配导入.md](09-STEP装配导入.md)，总入口的替换/追加语义见 [08-模型导入-总入口与格式分派.md](08-模型导入-总入口与格式分派.md)。

## 1. 菜单入口与命令注册

- File → Import → OBJ...：菜单项 `file.import_obj`，command `file.import_obj`，目录条目 `File_ImportOBJ`（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L27-L34)），菜单定义见 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L34)。
- File → Import → STL...：菜单项 `file.import_stl`，command `file.import_stl`，目录条目 `File_ImportSTL`（[CommandCatalog3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/CommandCatalog3D.cpp#L35-L42)），菜单定义见 [base.json](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L35)。
- 两者都是 `kMenuOnly`（无工具栏、无快捷键），注册 lambda 见 [FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L902-L920)。
- File → Import → All Supported...（`File_ImportModel`）也会把 OBJ/STL 送进同一条链，见 [L892-L900](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L892-L900)。
- 既没有单独的“STL 二进制/ASCII”菜单，也没有单位或焊接选项菜单。

## 2. 调用链路

```text
File_ImportOBJ / File_ImportSTL (L902-L920)
└─ importModelWithFilter(ctx, false, path, "obj" | "stl")       L717-L796
   └─ importModel(ctx, false, path, stepOnly=false)             L387-L715
      └─ loadModelFile (OBJ/STL 分支)                           L227-L337
         ├─ FileParserFactory::createParser(FileFormat)         FileParserFactory.cpp
         ├─ ParserGuard (RAII 保证 destroyParser)                L233-L242
         ├─ parser->parseToIR(path, FioParseResult&)            IFileParser.h
         ├─ ir.entities 只保留 Type::Mesh3D                     L255-L281
         │  └─ Eg::FioEntityConverter::convertEntity            FioEntityConverter.cpp
         ├─ 多个 mesh 合并成一个 AprMeshEntity                   L286-L292
         ├─ 实体名 = 文件基名                                   L322-L326
         └─ 返回 UI3D::LoadedPart3D（brep 为空）
      ├─ sceneManager->addEntities(...)                        L647
      └─ attachPartInfo（默认 GmcPartInfo）                    L650-L665
```

`importModelWithFilter` 只负责构造对话框；实际分派由 `loadModelFile` 按扩展名决定（[L230-L231](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L230-L231)）：是 `.obj` 就走 OBJ，否则一律走 STL。

## 3. 输入与参数

| 项 | OBJ | STL | 权威位置 |
|---|---|---|---|
| 扩展名 | `.obj`（大小写不敏感） | `.stl`（大小写不敏感） | [FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L230-L231) |
| 编码形态 | 文本 | ASCII 或二进制，自动探测 | [StlParser.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/Parsers/StlParser.cpp#L91-L193) |
| 单位 | 无换算，按文件数值 | 无换算，按文件数值 | 解析器不写 `sourceUnit` |
| 三角面上限 | 20,000,000 | 二进制声明值上限 50,000,000 | `kMaxTriangles` / `MAX_STL_TRIANGLES` |
| 单面顶点上限 | 1024 | 固定 3 | `kMaxFaceVerts` |
| 对话框过滤器 | `All Supported Files (*.stl *.obj ...)` 优先，其次 `3D Models` | 同左 | [L432-L477](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L432-L477) |
| 默认参数 | 无 | 无 | — |

`FileParserFactory::createParser` 按 `FileFormat` 查注册表；未注册返回空，UI 报 `No parser is registered for this 3D format.`。解析器的生命周期由 `ParserGuard` 管理，`parseToIR` 返回的 `FioParseResult` 指向解析器内部的 `thread_local` 缓冲，必须在 guard 析构前完成转换。

## 4. 算法原理与分步流程

### 4.1 框架层

`FileParserFactory` 用静态注册表把扩展名映射到 `IFileParser` 子类；`StlParser` / `ObjParser` 各自在匿名命名空间里注册（[ObjParser.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/Parsers/ObjParser.cpp#L445-L460)）。`parseToIR` 只做语法到 IR 的转换，不做三角化、不做单位换算。

IR 中每个实体是 `Fio::Entity`：`Type::Mesh3D` 在 `extensionBlob` 里携带“顶点 float 序列 + 法线 float 序列”，三角形由顶点顺序隐含（每连续 3 个顶点一个面），不单独存索引；`EntityInfo` 只记名称、颜色、可见性、`meshVertCount`、`meshTriCount` 与 blob 偏移/长度。OBJ/STL 解析器都不写显式颜色，所以转换后实体没有独立渲染色。

### 4.2 OBJ 解析

入口 `ObjParser::parseToIR`（[ObjParser.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/Parsers/ObjParser.cpp#L136-L442)）：

1. 以 `std::ifstream` 按行读取；行首去空白，`#` 与空行跳过。
2. `v x y z [w]`：读 3 个 float，`w` 及后续忽略；顶点数组按 0 基索引累积。遇到非有限数值跳过该行并计一次 warning。
3. `vn`：读 3 个 float 存法线池；`vt` 读 2 个 float 后丢弃，并只告警一次（纹理坐标不参与几何）。
4. `f`：按空白切分，逐 token 解析 `v`、`v/vt`、`v//vn`、`v/vt/vn` 四种形式；索引支持负值（相对当前池尾），`0` 视为非法。
5. 每个面的顶点数必须 ≥ 3；读取时超过 `kMaxFaceVerts = 1024` 会截断该面的顶点列表并告警，仍继续三角化已读到的部分。
6. 扇形三角化：`(0, i, i+1)`，`i` 从 1 到 `n-2`。凹多边形可能被错误三角化，这是 OBJ 导入的固有近似。
7. 顶点按三角形展开复制，直接写入“三角形汤”顶点序列；法线取面内对应顶点索引，缺失时用面法线（`cross(b-a, c-a)` 归一化）。
8. `o` / `g` / `usemtl` 开启新分段（`Chunk`）；`mtllib`、`s` 忽略；`curv`、`surf`、`cstype`、`deg` 等自由曲面语句只告警一次。
9. 累计三角形数达到 `kMaxTriangles = 20,000,000` 后停止三角化，剩余面全部跳过并告警 `OBJ: triangle limit reached, remaining faces skipped`；已解析的部分仍会导入。
10. 每个分段生成一个 `Entity`：有 `o`/`g` 名就用该名，否则用 `mesh`；只写顶点/法线 blob，不写索引。
11. 若一个三角形都没有，返回带 error 的空结果。

### 4.3 STL 解析

入口 `StlParser::parseToIR`（[StlParser.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/Parsers/StlParser.cpp#L40-L372)）：

1. 读入整个文件（`std::ifstream` 二进制）；文件长度 < 15 字节直接返回失败，不产生可显示的错误文本。
2. 二进制探测：在偏移 80 读 4 字节 `uint32` 声明三角形数。若 `84 + count * 50 == fileSize` 且 `count <= 50,000,000`，判定为二进制。
3. 二进制解析：逐三角形读 12 个 float（法线 3 + 顶点 9）与 2 字节属性（忽略）。法线为零向量时用两条边叉积重算；重算失败（退化）写入零法线并告警。每个三角形展开 3 个顶点、3 条同值法线。
4. 否则回退 ASCII 探测：取前 256 字节转小写，包含 `solid` 视为 ASCII；不包含则判失败。
5. ASCII 解析：识别 `facet normal nx ny nz`、`outer loop`、`vertex x y z`、`endloop`、`endfacet`；忽略 `solid`/`endsolid`。一个 facet 必须恰好 3 个 `vertex`，多出的顶点丢弃并告警；缺少 `vertex` 的 facet 跳过。
6. ASCII 路径的法线同样在零向量时重算；该路径**没有**三角形数量上限（上限只用于二进制探测）。
7. 读取结束后 `triangleCount == 0` 返回空结果，只打日志 `parseToIR: no triangle parsed (neither binary nor ASCII layout matched, ...)`；UI 层表现为“无有效实体”。
8. 生成单个实体：`vertices` 与 `normals` 都是 `triangleCount * 3` 个 float，按三角形角点顺序平铺、不写索引；`sourceId = 1`；实体名取文件 stem；`meshVertCount = 3N`、`meshTriCount = N`。
9. 二进制与 ASCII 两条路径最终 IR 结构完全一致，后续转换无差异。

### 4.4 转换与合并

1. `FioEntityConverter::convertEntity`（[FioEntityConverter.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/Import/FioEntityConverter.cpp#L67-L126)）先用 `extensionRangeValid` 校验 `extensionDataOffset + extensionDataSize <= blob.size`；越界则拒绝整个实体并写 `SY_ERRORF`（[L33-L59](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/Import/FioEntityConverter.cpp#L33-L59)）。
2. 只处理 `Type::Mesh3D`：按 `meshVertCount` 读顶点，但用 `min(meshVertCount, totalFloats / 3)` 截断到实际可用字节，声明数与可用数不符只写错误日志（[L94-L101](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/Import/FioEntityConverter.cpp#L94-L101)）。
3. 法线紧随顶点之后读取；读不满时用 `+Z` 补齐到与顶点等长（[L111-L121](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/Import/FioEntityConverter.cpp#L111-L121)）。不会按面法线回退。
4. 名称写入实体；`EntityInfo` 有显式颜色时才设置覆盖色与可见性。
5. 回到 `loadModelFile`：遍历 IR 实体，第一个 mesh 作为结果实体，后续 mesh 只把 `vertices` 与 `normals` 平铺追加（[L286-L292](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L286-L292)）。拼接依赖两端顶点/法线数量一致，不重映射索引。
6. 实体名统一改成文件基名（`QFileInfo::completeBaseName`），文件内的 `o`/`g` 名称在此被覆盖。
7. `markDirty()` 后返回 `LoadedPart3D`；`brep` 为空指针，`partInfo` 为默认值（`partIndex = -1`、单位矩阵、`hasName/hasColor/hasMaterial` 全 false）。

## 5. 输出形态与撤销

- 一次导入只产生 **一个** `Eg::AprMeshEntity`，不保留 OBJ 的分组、STL 无分组概念。
- 实体没有 B-Rep；`attachBRep` 不执行。启用 GeoModelCore 时仍会 `attachPartInfo` 一份默认 `GmcPartInfo`（[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L650-L665)），内容为空壳，不可用于 B-Rep 相关操作。
- 追加导入：命令为 `ReplaceEntitiesCommand3D(scene, nullptr, 0, added, n, "Import Model")`，撤销摘出实体、重做放回（[L370-L385](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L370-L385)）。文档置脏、不建保存点。
- 替换导入（Recent Files 打开 `.obj/.stl`，`replaceScene = true`）：先清撤销栈与 B-Rep 映射，`clearScene` 后加载，成功压 `SceneReplaceCommand3D` 并 `markSavePoint`；失败恢复 `before` 快照。
- 网格导入不解单位、不重算坐标轴；导入后实体的位置就是文件坐标。

## 6. 限制与边界行为

**报错文本**

| 场景 | 文本 | 位置 |
|---|---|---|
| 无解析器 | `No parser is registered for this 3D format.` | [L233-L242](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L233-L242) |
| 解析异常 | `Failed to parse model file: %1` / `Failed to parse model file: unknown error.` | [L295-L309](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L295-L309) |
| 无有效实体 | `Failed to load the model file. It may be malformed or empty.` | [L311-L320](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L311-L320) |
| 上层失败提示 | `Load Error` / `Failed to load model:\n%1` | [L587-L599](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L587-L599) |

**静默/半静默行为**

- 解析器内部警告只累计到 `ir.warningCount`，UI 仅写日志（[L327-L331](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L327-L331)），没有逐条提示。
- OBJ：纹理坐标、材质、平滑组、自由曲面全部丢弃；`v` 的 `w` 分量忽略。
- OBJ：面顶点数 > 1024 时截断该面并继续；三角形数达到 2000 万后停止解析剩余面，不报整体失败。
- OBJ：扇形三角化对凹多边形不正确。
- STL：文件 < 15 字节直接失败且无具体错误文本。
- STL 二进制声明数与文件长度不匹配时回退 ASCII；若内容不含 `solid` 则失败。
- STL ASCII 路径没有三角形上限，超大文本文件可能耗尽内存。
- 退化三角形不在导入阶段过滤；它们保留到导出阶段才被跳过（见 [11-网格导出-OBJ与STL.md](11-网格导出-OBJ与STL.md)）。
- 任意非 `.obj` 扩展名都会尝试 STL 解析（[L230-L231](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L230-L231)）。
- 网格实体没有 B-Rep：参与布尔、STEP 导出、按面拾取等依赖 B-Rep 的功能时会被跳过或不可用。

## 7. 源码与测试

源码：

- UI 分支：[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L227-L337)
- OBJ 解析：[ObjParser.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/Parsers/ObjParser.cpp#L136-L460)
- STL 解析：[StlParser.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/Parsers/StlParser.cpp#L40-L372)
- IR 类型：[FioTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Include/FileIO/FioTypes.h)
- 实体转换：[FioEntityConverter.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/Import/FioEntityConverter.cpp#L33-L126)

测试：

- [Obj3DTests.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Test/Obj3DTests.cpp#L47-L211)：`ObjParserTest.ParsesSingleTriangle`、`FanTriangulatesQuad`、`NegativeIndicesResolveRelativeToCurrentPool`、`IgnoresTextureCoordsAndVnFileRefs`、`RejectsOutOfRangeVertexIndex`、`ReportsMissingFaceData`、`IgnoresUnsupportedFreeFormStatements`、`ParsesMultipleObjectsAsSeparateChunks`、`StlParserTest.AsciiStlStillParsesAfterBufferUnification`。
- 未找到针对 `loadModelFile` OBJ/STL 分支合并逻辑或 `File_ImportOBJ/STL` 的 UI 层测试。

## 8. 旧文档差异

- [00-索引与算法对照表.md](00-索引与算法对照表.md) 说“OBJ/STL 导入共用一个解析管线、只取 `Mesh3D`、多个 mesh 合并成一个实体”，与代码一致，无冲突。
- 同文第 5 节“先 `attachBRep` 再入场景”与代码“先入场景再挂接”冲突（[FileOperations3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/FileOperations3D.cpp#L647-L665)）；网格路径无 B-Rep，但 partInfo 同样在入场景后挂。
- [3D撤销与B-Rep同步优化.md](3D撤销与B-Rep同步优化.md) 未涉及网格导入，其“网格强转 UB 修复”与本链路无冲突。
