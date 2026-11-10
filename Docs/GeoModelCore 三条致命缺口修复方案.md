# GeoModelCore 三条致命缺口修复方案

> 状态：**设计文档，方案已全部落地（2026-11-10 复核）**。原撰写时只写文档、不改源码；后续提交已按本方案实施，
> 落地对照见 §0.3。历史正文保留，作为方案依据与取舍记录。
> 依据：《GeoModelCore 功能完备性审查》中 G-01 / G-02 / G-03 / G-04 / G-06。
> 日期：2026-11-04（撰写）/ 2026-11-10（复核）。

---

## 0.3 落地对照（2026-11-10 按代码核对）

| 缺口 | 原方案要点 | 当前代码 | 状态 |
|---|---|---|---|
| G-01 切割轮廓 | 新建平面面环抽取，`ShapeAnalysis_FreeBounds` 重组闭环，内外环分类，double 精度 | `GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcCutContour.h`（`GmcCutContour::extract` / `extractSilhouette`，POD `GmcContourLoop` / `GmcContourPlane` / `GmcContourResult`，带 `structSize`/`abiVersion` 布局守卫）+ `Source/GmcCutContour.cpp` | 已落地 |
| G-02 割缝 | 不在 GeoModelCore 造，落在 Engine2D + CAM 桥接，按外/内环分符号偏置 | `Engine/2D/Include/Engine2D/Cam/KerfCompensator.h`（`Eg::Cam::KerfOptions` / `KerfSide` / `KerfResult`）；`Main/Src/CAM/CamInputBridge.cpp:499-532` 按 `kerfMm` 正负映射内外侧，并关闭 CAM 侧二次偏置 | 已落地 |
| G-03 装配/颜色/名称 | 补 TKXCAF/TKCAF/TKDESTEP，导入保留零件名、颜色与层级 | `GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcStepAssembly.h`（零件名 UTF-8、`hasColor`、`ColorMissing` 状态位）；实现 `Source/GmcAssemblyImpl.cpp` | 已落地 |
| G-04 布尔可信度 | 布尔结果做有效性判定，失败重试/修复 | `GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcShapeHealing.h`（`validate` / `repair` / `sew` / `unifyTolerances`），`Source/GmcShapeHealing.cpp`；`GmcCoreTests.cpp` 覆盖 `GmcShapeHealing::validate` | 已落地 |
| G-06 isValid | 提供真正的有效性判定 | `GmcShapeHealing::validate(const TopoShape&, std::string*)` + `TopoShape::isValid()` | 已落地 |

> 仍需注意：`GmcCutContour` 文档明确「v1 只保证板厚方向为 Z 的板件轮廓」，任意剖面 `GmcSectionFrame` 仅为预留；
> 弦高按曲率自适应未实现，需要更高精度时下调 `GmcContourOptions::deflection`。

---

## 0. 结论摘要与依赖顺序

### 0.1 三条之间的关系（推荐实施顺序）

```
G-04 布尔可信度 ──► G-01/G-02 切割轮廓 ──► 主程序可用「STEP 进 2D 闭环 + 割缝」
        │                    ▲
        └── G-06 isValid() ──┘
G-03 装配/颜色/名称 ── 与上面三条互相独立，可并行；但必须在 G-01 的 STEP→2D 上线前合入，
                        否则多零件来图会被压成一个轮廓集合。
```

判断依据：

- 轮廓来自 B-Rep 的面和 wire。布尔结果无效（`GmcSplit` 拿 `!IsNull()` 当守卫）时，wire 可能自交/重叠，抽出的闭环不可信，所以**布尔先可信，轮廓才可信**。
- `G-06` 是 `G-04` 的前置：没有真正的有效性判定，就没法判定“重试是否成功”。
- `G-03` 只影响“导入进来是什么”，不影响几何算法，可并行；但它决定 G-01 的输入是“一整块”还是“多零件”，所以上线顺序排在 G-01 之前。

### 0.2 一句话结论

- **G-01**：B-Rep 侧**需新建**（GeoModelCore：平面面环抽取，double 精度，不依赖网格）；闭环重组与内外环分类**优先复用**（OCCT `ShapeAnalysis_FreeBounds` / 本仓 `Eg::ContainmentResolver`）。
- **G-02**：**不在 GeoModelCore 造**。kerf 是工艺语义，落在 `Engine2D + CAM 桥接`；几何内核复用已有的 `Geo2DOffset`（Clipper2），但必须新增“按外环/孔洞分符号偏移”的入口，现有 `offsetContours` 的语义是“外扩时孔洞同步扩大”，**不能**直接用于割缝。
- **G-03**：**需先补依赖**（`TKXCAF` / `TKCAF` / `TKDESTEP`）。已确认本机 vcpkg 的 OCCT 包含这三个库，无需新增包，但 CI/打包机需复核。

---

## 1. 第一节：现有轮廓能力盘查（决定落点）

### 1.1 核查结论

| 能力 | 结论 | 证据 |
|---|---|---|
| 3D B-Rep → 平面闭合环抽取 | **没有，需新建** | 全仓只有 `GmcProjection::projectTopView(shape, deflection)`，输出的是**逐边折线**（`GmcPolyline2d`），没有闭环、没有面/环概念；也没有“任意截面帧”入口 |
| STEP 导入的 2D 化 | **退化实现** | `StepParser` 把每条投影折线直接发一条 `EntityType::Polyline`，不闭合、不去重、不区分内外 |
| 闭环重组（散边→闭合环） | **没有**（2D 侧也没有） | 2D 侧 `GeometryExtractor` 是“逐图元 → 一条 Contour”，没有跨图元拼接；全仓无 `JoinEntities` / `ChainBuilder` |
| 内外环分类 | **有通用实现，但未接入加工链路** | `Eg::ContainmentResolver::resolveDepths()` 用包围盒+射线法算嵌套深度（0=外，1=孔）；`Cam::Contour::hole` 注释写明“由 ContainmentResolver 填写”，但全仓只有 `out.hole = src.hole` 的透传，**没有任何地方真的算过 hole** |
| 2D 矢量 kerf 偏置 | **有能力，无 kerf 语义** | `Geo2DOffset::offsetContour(s)` 是 Clipper2 实现，double + mm；但 `offsetContours` 是“整组同向偏移”，注释明确“外扩时孔洞同步扩大，内缩时孔洞收缩直至消失”（`Geo2DOffset.cpp:198-202`），与割缝要求（外环外扩、孔洞内缩）相反 |
| 另一套偏移实现 | **存在但不可用于 kerf** | `Engraving::PolygonOffset::offsetPolygons()`：坐标为 `Engraving::Point3D{float x,y,z}`、`offsetDistance` 也是 `float`；整组同向偏移（`PolygonOffset.cpp:182-211`）。千万级坐标/0.1 mm 割缝场景下 float 精度不足 |
| kerf 工艺契约 | **契约已定义，未实现** | `Cam::KerfParams{widthMm, enabled, side}` 已存在；CAM 架构文档 M0 明确“不含割缝补偿”，`CAM/Src/GeometryTransform/` 目录尚不存在 |
| Raster 的 `Raster_KerfCompensate` | **不可复用** | 实现是“栅格化 → 形态学膨胀/腐蚀 → 重新提轮廓”，按整像素取整（`lround(kerfWidth*0.5)`），精度 1 px，无法用于 mm 级矢量轮廓 |
| 闭合标记在 IR 里怎么表达 | 用类型表达，无字段 | 闭合折线在 IR 中映射为 `EntityType::Polygon`（DXF 解析同款约定）；IR 没有 `closed` / `hole` 字段 |

### 1.2 因此的落点决策

| 环节 | 归属 | 理由 |
|---|---|---|
| 平面面 → 有序 wire → 2D 点列（double） | **GeoModelCore 新建** `GmcCutContour` | 只有内核手里有 B-Rep；`projectTopView` 走的是 HLR/逐边投影，无法给出“面内环”的拓扑 |
| 自由边/轮廓线的闭环重组 | **GeoModelCore**，内部调用 OCCT `ShapeAnalysis_FreeBounds::ConnectEdgesToWires` | 不手写拼接；`ConnectEdgesToWires` 就是 OCCT 提供的“散边成环” |
| 外环/孔洞判定（3D 直出） | **GeoModelCore**，用 `BRepTools::OuterWire(face)` 拓扑判定 | 面上外环由拓扑直接给出，不需要几何包含判断，最可靠 |
| 外环/孔洞判定（2D 场景 / DXF 来图） | **复用 Engine2D** `ContainmentResolver` | 已实现、已测试（填充链路在用），避免第二套实现 |
| kerf 几何偏移 | **Engine2D** 新增 `KerfCompensator` + `Geo2DOffset::offsetContoursByRole` | Clipper2 已在 Engine2D，mm + double；GeoModelCore 不得反向依赖 Engine2D |
| kerf 工艺参数与调用时机 | **Main/Src/CAM/CamInputBridge** | CAM 不能依赖 Engine2D；桥接层是唯一同时认识两边的位置（与 `hole` 的既有设计一致） |
| 2D 落图（IR / 场景） | **FileIO `StepParser`** | 已有 `FILEIO_HAS_GEOMODELCORE` 门控与 try/catch，只需替换投影来源 |

**明确不做的事**：不在 GeoModelCore 里实现 kerf；不在 GeoModelCore 里实现“2D 图元拼接成环”；不复用 Raster 的像素级 kerf；不新增 `nativeHandle()` 逃生口。

**与既有规划文档的一处偏差（需记录）**：`Docs/CAM层架构规划.md` 计划把 `KerfCompensator` 放进 `CAM/Src/GeometryTransform/` 并调用 `Engraving::PolygonOffset`。本方案**不采用**该落点，原因有三：

1. `CAM/CAM/CMakeLists.txt:9` 明确写着“不依赖：Engine2D / Engraving / Hardware / UI 模块”，在 CAM 内调 Engraving 会破坏该约束（要么改约束，要么改落点；本方案选后者）。
2. `Engraving` 是“叶子模块，只依赖 OpenCV，没有兄弟 DLL”（`Engraving/Engraving/CMakeLists.txt:16`），无法把 `Eg::Cam::Contour`（Engine2D 类型）作为出入参；若强行引入会形成 Engine2D ↔ Engraving 的反向依赖。
3. `Engraving::Point3D` / `PolygonOffsetOptions::offsetDistance` 都是 `float`，而 kerf 需要在 1e3~1e5 mm 坐标下保持 0.01 mm 级精度；本方案要求 double。

因此 kerf 几何落在 `Engine2D`（已有 double + Clipper2 的 `Geo2DOffset`），只在 **Main 桥接层**被调用，CAM 契约与模块边界保持不变。若后续团队决定统一到 `Engraving`，只需把 `KerfCompensator` 的实现换成转调 `PolygonOffset`，接口签名不变（但必须先解决 float 精度问题）。

---

## 2. 通用约束（三份方案共同遵守）

### 2.1 精度基线

- `GmcMeshData` 顶点是 `float`（[GmcTypes.h:110](file:///c:/Users/xx/Documents/Cpp/CAD/GeoModelCore/GeoModelCore/Include/GeoModelCore/GmcTypes.h#L110-L111)）。1e5 mm 处 float ulp ≈ 7.8e-3 mm，**任何轮廓/割缝计算禁止从网格取点**。
- G-01 全部在 B-Rep 上用 `double`：`gp_Pnt` → 投影到 2D 仍是 `double`。1e5 mm 处 double ulp ≈ 1.5e-11 mm，余量足够。
- `BRepModelService3D::alignShapeToMesh()` 用 float 网格包围盒算平移量喂给 double B-Rep，存在系统偏差（~1e-2 mm 量级）。**本方案不修它**，但要求：调用端传给 `GmcCutContour` 的是**与 CAM/场景一致的同一份变换**（见 3.4），并提供 `GmcSectionFrame.origin` 让调用方显式给出基准，禁止“从网格反推原点”。

### 2.2 异常收口（强制）

现状：`GmcMesh` / `BRepBuilder` / `GmcCurve` / `GmcSurface` / `GmcBvh` / `GmcQuery` / `GmcProjection` / `GmcTopoExplorer` 共 8 个源文件零 `try/catch`，`Standard_Failure` 会穿出 DLL 边界。

新代码统一出口：

```cpp
try
{
    // ... OCCT 调用
}
catch (const Standard_Failure& e)
{
    return fail(GmcErrorCode::OperationFailed, e.GetMessageString());
}
catch (const std::exception& e)
{
    return fail(GmcErrorCode::OperationFailed, e.what());
}
catch (...)
{
    return fail(GmcErrorCode::InternalError, "unknown exception");
}
```

- 捕获后**不吞掉信息**：`GmcStatus.message` 必须非空。
- 对外错误文案一律英文（代码内不得出现中文字符串字面量）；中文提示由 UI 层按 `code` 映射。

### 2.3 错误模型

现状四套：`GmcStatus`、`GmcBooleanResult`、`GmcSplitResult`、裸 `nullptr`。本轮规则：

| 范围 | 规定 |
|---|---|
| GeoModelCore 新增接口 | 一律 `GmcStatus`；不使用裸 `nullptr`；不新增 `XxxResult{bool,char[256]}` |
| `GmcBoolean` 旧入口 | 保留签名不动（`std::shared_ptr<TopoShape>` + 可选 `GmcBooleanResult*`），内部改为组装 `GmcStatus`，避免破坏现有 9 处调用 |
| 已有 `GmcBooleanResult` | **只增字段**（`GmcBooleanDiagnostics diagnostics`），不删不改 `success/errorMessage`；旧调用继续工作 |
| `GmcSplit` | 不改返回结构，内部改用空判 + 严格 `isValid()` 入参守卫 + 两侧结果校验，见 5.5 |
| Engine2D / CAM | 各自沿用本模块既有 POD 状态（`bool ok + 静态 reason` / CAM 现有返回），**不引入对 GeoModelCore 的依赖** |

### 2.4 容差策略

- 现有 `GmcTolerance{linear=1e-7, angular=1e-9}` 是**绝对 mm**，与模型尺度无关。
- 新接口的容差全部沿用“绝对 mm”，并在字段注释里写明单位与量纲；**不引入“随尺度自动放大”的隐式行为**。
- 唯一例外：`GmcContourOptions.deflection <= 0` 时按包围盒对角线自适应（`max(1e-3, diag * 1e-4)`），且必须出现在返回值诊断里，避免“静默不同精度”。

### 2.5 并发与 TShape 共享

- `TopoShape::clone()` 只复制 `TopoDS_Shape` 句柄，**共享同一 TShape**；`GmcMesh::tessellate` 会把三角化写进 TShape，因此“多线程并行网格化同一 shape 的多个 clone”是数据竞争。
- G-01 若要多板件并行：每个线程先 `BRepBuilderAPI_Copy(shape, /*copyGeom*/ true, /*copyMesh*/ false)` 复制到独立 TShape，再处理；禁止直接对共享 TShape 做 `BRepMesh`。
- G-01 本身只读 B-Rep（不写三角化），只读并发安全；但 `BRepTools_WireExplorer` 会建立 pcurve 缓存（`BRep_Tool::CurveOnSurface` 可能写 `Poly_Triangulation`？不会，但会写 `CurveRepresentation`）。保守策略：**同一 `TopoShape` 对象不跨线程**，每线程一份 copy。

### 2.6 公开头边界

- 新增公开头只 include：`GeoModelDLL.h`、`GmcStatus.h`、`GmcTypes.h`、`TopoShape.h`、STL 容器。**不得** include 任何 `*_Handle` / `gp_*` / `TopoDS_*`。
- 实现类放 `Private/`，与 `.cpp` 同目录（仓库既有约定）。
- 不新增 `GetInternalShape()` 类逃生口。

---

## 3. 方案一：G-01 切割轮廓抽取 + G-02 kerf 偏置

> 分两期独立落地：**G-01A 内核侧**（3.1–3.5）与 **G-01B/G-02 应用侧**（3.6–3.9）。G-01A 合入后即使不做 kerf，STEP 也能落成闭合多边形；G-02 不依赖 G-01 的完成度，可以先在 DXF 来图上验证。

### 3.1 目标 / 非目标

**目标**

1. 从 `TopoShape` 直接抽出“与给定板厚方向平行的平面面”上的**闭合环**，double 精度，单位 mm。
2. 给出每条环的 `hole` 标志（拓扑判定）、有符号面积、周长。
3. 同板厚方向的两个平行面（板材上下面）轮廓去重，只回一份。
4. 提供“自由边/轮廓线投影 + 闭环重组”的退化路径，供非棱柱件使用。
5. STEP 导入落图从“散折线”改为“闭合 Polygon”，DXF 导出可直接下料。

**非目标（本版不做）**

- 不支持曲面/锥面/自由曲面轮廓（遇到即跳过并计数，不报错）。
- 不支持“板厚方向 = 任意斜向”的产品化取景（只支持调用方给定 `GmcSectionFrame`；UI 默认 Z）。
- 不做嵌套孔洞的“岛中孔中岛”递归分类之外的任何工艺排序（排序仍归 CAM）。
- 不做 2D 场景内“逐图元拼接成闭合环”（见 3.9 明确边界）。
- 不做展开/中面/共边切割。
- 不改 `GmcProjection` 的既有行为（保留作为 A/B 对照与回滚路径）。

### 3.2 API 设计（G-01A，GeoModelCore）

新增公开头：`GeoModelCore/Include/GeoModelCore/GmcCutContour.h`

```cpp
#pragma once

#include "GeoModelCore/GmcStatus.h"
#include "GeoModelCore/GeoModelDLL.h"

#include <cstdint>
#include <vector>

namespace GeoModelCore
{
    class TopoShape;

    // 截面平面内的二维点（mm）
    struct GmcPoint2d
    {
        double x = 0.0;
        double y = 0.0;
    };

    // 截面坐标系：原点 + 法向 + 面内 X 轴，全部是世界坐标（mm）
    struct GmcSectionFrame
    {
        double origin[3] = { 0.0, 0.0, 0.0 };
        double normal[3] = { 0.0, 0.0, 1.0 };   // 板厚方向
        double xAxis[3]  = { 1.0, 0.0, 0.0 };   // 面内 X，用于定 2D 坐标
    };

    struct GmcContourOptions
    {
        // 弦高偏差（绝对 mm）；<=0 时按包围盒对角线自适应
        double deflection = 0.01;
        // 闭环首尾合并容差（绝对 mm）
        double closureTol = 1e-4;
        // 平面法向与板厚方向的夹角容差（弧度）
        double planeAngleTol = 1e-4;
        // 同平面合并容差（绝对 mm）
        double planeMergeTol = 1e-5;
        // 是否把上下两个平行面的重复轮廓合成一份
        bool dedupeParallelDuplicates = true;
        // 是否把外环统一为 CCW、孔洞统一为 CW
        bool normalizeOrientation = true;
        // 最多返回环数，0 = 不限
        int32_t maxLoops = 0;
    };

    struct GmcContourLoop
    {
        std::vector<GmcPoint2d> points;  // 已投影到截面 2D；闭合时不重复首点
        bool closed = false;
        bool hole = false;               // 由 BRepTools::OuterWire 拓扑判定
        double signedArea = 0.0;         // mm^2，CCW 为正
        double perimeter = 0.0;          // mm
    };

    struct GmcContourPlane
    {
        double origin[3] = { 0.0, 0.0, 0.0 };
        double normal[3] = { 0.0, 0.0, 1.0 };
        std::vector<GmcContourLoop> loops;
        double totalArea = 0.0;          // 外环面积减孔洞面积（mm^2）
        int32_t sourceFaceCount = 0;     // 合并进来的面数（诊断用）
    };

    struct GmcContourResult
    {
        GmcStatus status;
        std::vector<GmcContourPlane> planes;
        int32_t skippedFaceCount = 0;    // 非平面 / 非平行面数量
        int32_t openWireCount = 0;       // 未能闭合的 wire 数量
        // ---- 追加字段（尾部追加，遵守 structSize/abiVersion 布局校验）----
        uint32_t statusDetail = 0;       // 位标志：1=开环 2=自交 4=偏置失败（由 2D 偏置层回填）
    };

    class GEOMODEL_API GmcCutContour
    {
    public:
        // 主路径：平面面环抽取
        static GmcContourResult extract(
            const TopoShape& shape,
            const GmcSectionFrame& frame = GmcSectionFrame(),
            const GmcContourOptions& options = GmcContourOptions());

        // 退化路径：自由边投影 + 闭环重组（非平面件）
        static GmcContourResult extractSilhouette(
            const TopoShape& shape,
            const GmcSectionFrame& frame = GmcSectionFrame(),
            const GmcContourOptions& options = GmcContourOptions());
    };
}  // namespace GeoModelCore
```

**所有权 / 返回值约定**

- 全部按值返回，无 `shared_ptr`、无裸指针、无 OCCT 类型。
- `options` 用结构体默认参数（与 `GmcTolerance` 风格一致）。
- 空结果**不是错误**：`planes.empty() + status.ok == true`；只有 frame 非法或 shape 为空才返回失败。
- 调用方（FileIO / 上层）不需要知道面、wire、edge 的存在。

### 3.3 内部实现步骤（G-01A）

新增私有实现：`Private/GmcCutContourImpl.cpp`（与公开头分离）。

> 落地形态（2026-11-10 复核）：实现放在 `GeoModelCore/GeoModelCore/Source/GmcCutContour.cpp`，
> 未单独拆出 `Private/GmcCutContourImpl.cpp`；行为与下面调用序列一致。

调用序列：

1. `if (shape.isNull()) return fail(NullShape)`。
2. 构造正交帧：`normal` 归一化；`xAxis` 去掉法向分量后归一化；`yAxis = cross(normal, xAxis)`。任一长度 < 1e-12 → `InvalidArgument`。
3. `TopExp_Explorer ex(shape, TopAbs_FACE)`；逐面：
   - `BRepAdaptor_Surface surf(face, Standard_False)`（只要几何，不要 pcurve 缓存）。
   - `if (surf.GetType() != GeomAbs_Plane) { skippedFaceCount++; continue; }`
   - `const gp_Pln& pln = surf.Plane();`
   - `const gp_Dir& n = pln.Axis().Direction();`
   - `if (std::abs(n.Dot(frameNormal)) < std::cos(angleTol)) { skippedFaceCount++; continue; }`
4. 按平面分组合并：签名 = (法向符号无关化) + 平面到 `frame.origin` 的距离。距离差 ≤ `planeMergeTol` 且法向点积 ≥ `cos(angleTol)` → 同一组。
5. 每组取“面积最大、且与其他面重复”的判重：
   - 先各自抽环（步骤 6-9）；
   - 若两组的投影环在 `closureTol` 内逐点一致（同点数、对应点距离 ≤ tol，允许起点错位，用首点角度归一化），且两平面平行、间距 ≤ 板厚上限（取 `maxLoops` 无关的 `planeMergeTol*1e3`，或直接取“投影一致”为准），合并为一组，保留 `sourceFaceCount` 累加。
6. 逐面抽外环：`TopoDS_Wire outer = BRepTools::OuterWire(face);` 若为空 → 退化：遍历该面所有 wire，取 `|area|` 最大者为 outer。
7. `TopExp_Explorer wex(face, TopAbs_WIRE)`；每个 wire：
   - `hole = !wire.IsSame(outer)`。
   - 用 `BRepTools_WireExplorer` 按 wire 顺序遍历边：
     ```cpp
     BRepTools_WireExplorer wx(wire, face);   // 传 face 才能取到正确的 pcurve 上下文
     for (; wx.More(); wx.Next())
     {
         const TopoDS_Edge& edge = wx.Current();
         // 注意：WireExplorer 给出的是"沿 wire 行进方向"的边，
         // 但 edge 的几何参数化方向可能是反的，必须按 Orientation 处理
     }
     ```
   - 每条边 `BRepAdaptor_Curve curve(edge)`；`GCPnts_QuasiUniformDeflection disc(curve, deflection, first, last)`。
   - `first/last` 取自 `curve.FirstParameter()/LastParameter()`；若 `edge.Orientation() == TopAbs_REVERSED` → 反向遍历离散点（**不要**改 `first/last`，改顺序即可）。
   - 每条边只取起点（末点由上一条边的起点接续），最后一点由 wire 闭合判断补上。
8. 3D 点 → 2D：`u = (p - origin) · xAxis`，`v = (p - origin) · yAxis`。**用 `frame` 的法向与 X 轴，不用面法向**，这样上下两个平行面得到同一套 2D 坐标，才能做去重。
9. 闭合判定：`dist(first, last) <= closureTol` → 去掉重复末点，`closed = true`；否则 `openWireCount++`，保留 `closed = false`（不强行闭合）。
10. 面积/周长：shoelace 求 `signedArea`；逐段欧氏距离求和 `perimeter`。
11. 方向归一化（`normalizeOrientation`）：外环若 `signedArea < 0` 反转；孔洞若 `signedArea > 0` 反转。反转后重算面积（保号）。
12. `maxLoops` 截断（按 `|signedArea|` 降序，先外后孔）。
13. 计算 `totalArea = Σ(outer) - Σ(hole)`。
14. 出口 try/catch（见 2.2）。

**退化路径 `extractSilhouette`**

1. `TopExp_Explorer(shape, TopAbs_EDGE)` 收集自由边（`edge` 属于 1 个面 = 边界边；属于 2 个面但两面对该边夹角 > 阈值也纳入，阈值默认 30°）。
2. 用 `Bnd_Box` + `BRepBndLib::Add` 求包围盒，投影到 2D（同 8）。
3. `ShapeAnalysis_FreeBounds::ConnectEdgesToWires(compound, tol, /*shared*/ false, wires)`，tol 取 `max(closureTol, 1e-3)`。
4. 对每条 wire 做 `ConnectWiresToWires` 二次闭合（开放链），再做步骤 9-11。
5. 孔洞判定：用 `Eg::ContainmentResolver` **不可**（内核不得依赖 Engine2D）→ 内核内用“射线法 + 面积最小父环”实现最小版本（~80 行），或在 v1 只返回 `hole=false` 并标注 `holeUnresolved=true` 字段。

> **决策点**：v1 的 `extractSilhouette` 只保证“闭合”，不保证 `hole` 正确；`hole` 由应用层用 `ContainmentResolver` 重算。这样内核不重复实现嵌套判断。

**已知坑（必须在实现时处理）**

| 坑 | 处理 |
|---|---|
| 边的几何方向与 wire 行进方向相反 | 按 `edge.Orientation()` 反转离散点顺序，不改参数区间 |
| 闭合线首尾差 1e-7 但点列不严格相等 | `closureTol` 判定后**丢弃末点**，不保留重复点（否则下游 Clipper 会认为自交） |
| 圆/整圆边离散后首尾同点 | 同上；面积为 πr²，周长为 2πr |
| 周期面 seam 边重复 | 平面面无 seam；若未来支持圆柱，需按 `TopoDS::Edge` 两次 pcurve 去重（本版不支持） |
| `BRepTools::OuterWire` 对部分面返回空 | 退化为“面积最大的 wire” |
| 上下平面轮廓因 float 网格平移而不一致 | 不用网格；两平面都在 B-Rep 内，去重按 double 比较 |
| `UnifySameDomain` / 布尔后的面索引失效 | G-01 不缓存面索引，每次重新遍历（拓扑变化不影响） |
| 面法向 REVERSED | 只用 `BRepAdaptor_Surface` 的几何法向做“是否平行”判断，不用 `face.Orientation()`；对内/外环判定无影响（判定来自 wire 拓扑） |

### 3.4 数据流与模块边界

```
STEP 文件
  │
  ├─[A] GeoModel::loadStepFile ──► TopoShape（单 compound）
  │        │
  │        └─ GmcCutContour::extract ──► GmcContourResult
  │                 │（double / mm / closed / hole / area）
  │                 ▼
  │            FileIO::StepParser（改写投影来源）
  │                 ├─ 每条 closed 环 → ParsedGeometryType::Polygon
  │                 ├─ 每条开放环 → EntityType::Polyline（保留诊断）
  │                 └─ 失败/空 → 回退旧 GmcProjection::projectTopView（可开关）
  │                        │
  │                        ▼
  │                  2D 场景（AprPolygon）──► DXF 导出（已有）
  │
  └─[B] G-03 loadStepAssembly ──► GmcPart[] ──► 3D 场景（分件）

2D 场景 / DXF
  │
  └─ Eg::GeometryExtractor ──► Contour[]
           ├─[新增] 包含关系阶段：ContainmentResolver 填 hole
           ├─[新增] KerfCompensator（Engine2D，Clipper2）
           └─ CamInputBridge ──► Cam::ToolpathJobInput ──► CamCore
```

责任划分：

| 环节 | 模块 | 是否新增 |
|---|---|---|
| B-Rep → 闭合环 + 拓扑 hole | GeoModelCore | 新增类 |
| 环 → IR Polygon | FileIO | 改 `StepParser::parseToIR` 内部 |
| 闭合环 → 2D 场景实体 | FileIO / Main（现有 IR 投影链） | 不改，沿用 `EntityType::Polygon` 既有映射 |
| 2D 环 → hole 标记 | Engine2D | 新增“包含关系阶段”（薄封装 `ContainmentResolver`） |
| hole → kerf 偏移 | Engine2D | 新增 `KerfCompensator` + `offsetContoursByRole` |
| kerf 触发与工艺参数 | Main `CamInputBridge` | 改，读取 `Cam::KerfParams` |
| 割缝补偿进 CAM | CAM | **不改**（仍在桥接层完成，M0 契约不变） |

**是否需要 FileIO 与 GeoModelCore 之间的适配层**：不需要新增模块。`StepParser.cpp` 已经是唯一桥点，且已有 `#if FILEIO_HAS_GEOMODELCORE && GMC_ENABLE_STEP_IO` 门控；直接在该文件内把 `projectTopView` 换成 `GmcCutContour::extract` 即可（约 40 行改动）。

### 3.5 精度与容差策略

| 项 | 规定 |
|---|---|
| 2D 坐标单位 | mm，`double` |
| 离散偏差 | `GmcContourOptions.deflection`，默认 0.01 mm（越小折线越密、面积越贴近解析值）；直线段不受影响（2 点）；按曲率自适应取弦高未实现，列为 P2 |
| 圆弧离散 | `GCPnts_QuasiUniformDeflection`，弦高 ≤ deflection；1e5 mm 处 double 足够 |
| 闭环容差 | 绝对 `closureTol = 1e-4` mm（≈ OCCT 默认 1e-7 的 1000 倍，容忍 STEP 导入的折点误差） |
| 平面判定 | `planeAngleTol = 1e-4 rad`（≈0.0057°）；避免把 0.01° 的斜面带进来 |
| 平面合并 | `planeMergeTol = 1e-5` mm |
| 板厚方向约定 | 由 `GmcSectionFrame.normal` 给定；2D 坐标系右手：`yAxis = normal × xAxis`；**外环 CCW 为正**（数学坐标，y 向上），与 `Geo2DRingLayer` 的“外环 CCW、孔洞 CW”一致 |
| 投影姿态 | 只支持调用方给帧；UI v1 固定 `normal=(0,0,1)`、`xAxis=(1,0,0)`、`origin=(0,0,0)` |
| 与网格的关系 | 完全不读 `GmcMeshData`；不吃 `alignShapeToMesh` 的 float 平移 |

> **调用方注意**：若 3D 场景里实体已被 `translate`（`TopoShape::translate` 写 location），B-Rep 与显示网格是同一坐标；G-01 读到的是“场景坐标”。CAM 需要的也是场景坐标，两者一致。**禁止**把网格包围盒反推的平移量再喂给 G-01。

### 3.6 G-01B：IR 落图改造（FileIO）

改动点（仅 `StepParser.cpp`）：

```cpp
// 1) 先尝试平面轮廓抽取
GeoModelCore::GmcContourOptions contourOptions;
GeoModelCore::GmcSectionFrame frame;      // 默认 Z 向
auto contourResult = GeoModelCore::GmcCutContour::extract(*shape, frame, contourOptions);

// 2) 有闭合环才走新路径；否则回退旧投影（便于灰度与回滚）
if (contourResult.status.ok && !contourResult.planes.empty())
{
    // 选面积最大的平面（板材上/下面去重后通常只剩一个）
    // 每条 closed 环发 ParsedGeometryType::Polygon；open 环发 Polyline
}
else
{
    // 原 GmcProjection::projectTopView 路径保持不变
}
```

- 灰度开关：编译期宏 `GMC_CONTOUR_V2`（默认 ON），运行期可用环境变量 `APR_STEP_CONTOUR=0` 回退，便于现场对比。
- 不新增 IR 字段；`hole` 不落到 IR（原因见 1.1：IR 无该字段，且 2D 侧统一由 `ContainmentResolver` 重算）。

### 3.7 G-02：kerf 偏置（Engine2D + Main 桥接）

#### 3.7.1 目标 / 非目标

**目标**：对已带 `hole` 标记的闭合轮廓做割缝补偿，外环与孔洞按相反方向偏移单边补偿值；输出仍是 `Eg::Cam::Contour` 序列，供 CAM 直接消费。

**非目标**：不做引入/引出、微连接、排序、多遍；不做“按层功率补偿”；不重建圆弧（v1 输出折线）。

#### 3.7.2 API

新增头：`Engine/2D/Include/Engine2D/Cam/KerfCompensator.h`

```cpp
namespace Eg
{
    namespace Cam
    {
        // 与 Cam::KerfParams 对齐，避免两套语义
        struct KerfOptions
        {
            double kerfWidth = 0.0;   // 等距偏置总宽（mm，>=0）= 2 × 单边补偿值，本模块只做内部换算
            int32_t side = 0;         // 0 = Outside, 1 = Inside, 2 = Center
            double arcTolerance = 0.01;  // Clipper 圆弧化弦高（mm）
            bool simplify = true;        // 输出去共线/去重合点
        };

        struct KerfStatus
        {
            bool ok = true;
            int32_t code = 0;         // 0 = success
            char message[128] = { 0 };
        };

        struct KerfResult
        {
            KerfStatus status;
            std::vector<Contour> contours;
            int32_t droppedLoopCount = 0;   // 补偿后消失的环（孔洞被割没）
        };

        class ENGINE2D_API KerfCompensator
        {
        public:
            static KerfResult apply(const std::vector<Contour>& in, const KerfOptions& opt);
        };
    }
}
```

`Geo2DOffset` 新增（additive，不改旧函数）：

```cpp
// 按环角色分符号偏移：外环 +delta，孔洞 -delta（delta 已含 side 方向）
static std::vector<std::vector<Ut::Vec2d>> offsetContoursByRole(
    const std::vector<std::vector<Ut::Vec2d>>& loops,
    const std::vector<int32_t>& holeFlags,
    double delta,
    const Geo2DOffsetParams& params,
    const GeometryContext& ctx);
```

#### 3.7.3 语义与符号（必须锁定）

- `half = kerfWidth * 0.5`。
- `sideSign`：`Outside → +1`，`Inside → -1`，`Center → 0`。
- 每条环的偏移量：`delta = sideSign * half * (hole ? -1.0 : +1.0)`。
  - `+delta` 的定义是“扩大该环自身围成的面积”。
  - 所以 `Outside` 时：外环扩大、孔洞缩小（孔洞面积变小）。
  - `Inside` 时：外环缩小、孔洞扩大。
  - `Center` 或 `kerfWidth<=0`：原样返回（拷贝），不报错。
- 物理含义说明：`side` 表示“刀路相对名义轮廓在哪一侧”。现场标定决定到底哪个 side 能让成品尺寸达标；代码只保证数学语义与测试断言，不把 `Outside` 硬编码成“零件不缩小”。
- **口径统一**：该语义与 `Cam::KerfParams.side`（0=Outside/1=Inside/2=Center）完全一致，桥接层直接透传，不做二次解释。
- **工艺参数口径**：`Eg::ProcessParams::kerfMm` 是**单边补偿值（mm）**，不是缝宽；桥接层按 `kerfWidth = 2 * |kerfMm|` 换算一次，仅用于本模块的等距偏置。UI 文案、参数注释与文档一律写「补偿值（单边）」，禁止再出现「缝宽 / kerf width」的表述。
- **偏置只在一处发生**：桥接层完成补偿后写回 `snapshot.contours`，同时必须把 `Cam::ToolpathJobInput::LayerBucket::kerf.enabled` 置为 `false`，避免 CAM 的 M1 阶段按 `KerfParams` 再次偏置，形成两倍补偿。

#### 3.7.4 实现步骤

1. 入参 `Contour` → 折线 `std::vector<Ut::Vec2d>`：`PathSegment::Line` 直接取端点；`Arc` 按 `arcTolerance` 离散（弦高 ≤ tol）。
2. 跳过 `closed == false` 的轮廓（开放路径不参与割缝），计数并原样透传（文档标注 `droppedLoopCount` 不含开放路径）。
3. 用 shoelace 统一为**外环 CCW / 孔洞 CW**（依据 `Contour.hole`；若 `hole` 与几何方向冲突，以 `hole` 为准并修正方向）。
4. 逐环调用 `offsetContour`（单环、带符号 delta）或一次性 `offsetContoursByRole`。
   - 坑：Clipper2 `InflatePaths` 的正负方向由**路径绕向 + delta 符号**共同决定，不能靠“正 delta 一定外扩”的经验假设；必须先用 3.9 的方框测试锁定。
   - 坑：**不能**直接把整个“外环+孔洞”集合丢给 `Geo2DOffset::offsetContours`——它内部先做 EvenOdd 归一再整组 `InflatePaths`，其语义是“外扩时孔洞同步扩大”（`Geo2DOffset.cpp:168-202`）。kerf 必须按环角色分开定符号。
   - 不复用 `Engraving::PolygonOffset`：float 坐标 + 整组同向，理由见 1.2 末尾。
5. 多环结果用 `Clipper2 Union` + `FillRule::EvenOdd` 合并，避免相邻孔在补偿后重叠产生交叠路径。
6. 面积守卫：补偿后 `|area| < minArea`（默认 `0.01 mm²`）的环丢弃，计 `droppedLoopCount`。
7. 重建 `Contour`：每环按点序生成 `Line` 段，`closed = true`，`hole` 继承或按新的嵌套关系重算（补偿后嵌套可能变化 → 调用 `ContainmentResolver` 再算一次）。
8. 出口 try/catch（Engine2D 已有 `GeometryContext` 的错误通道，沿用）。

#### 3.7.5 包含关系阶段（补 hole）

新增薄封装：`Engine/2D/Src/Cam/ContourContainment.cpp`（或在 `GeometryExtractor` 末尾）：

```cpp
// 把提取出的闭合轮廓转成 ContainmentContour（只喂闭合环）
// 调 ContainmentResolver::resolveDepths
// depth 为奇数 → hole = true；偶数 → hole = false
```

- 只填 `Contour::hole`，不改 `closed`/点列。
- 开放轮廓一律 `hole = false` 且不参与嵌套计算。
- 这一阶段是 **G-02 的前置**：没有 hole，kerf 就只会全向外扩。

#### 3.7.6 桥接（Main）

`Main/Src/CAM/CamInputBridge.cpp`：在“`Eg::Cam::Contour` → `Cam` 结构”之前插入：

```cpp
if (kerfParams.enabled && kerfParams.widthMm > 0.0)
{
    Eg::Cam::KerfOptions opt;
    opt.kerfWidth = kerfParams.widthMm;
    opt.side = kerfParams.side;
    auto result = Eg::Cam::KerfCompensator::apply(contours, opt);
    if (!result.status.ok) { /* 记日志并继续用未补偿轮廓，或按工艺要求整体失败 */ }
}
```

- 失败策略建议：`kerf` 失败时**整批失败并提示**，不静默降级（静默降级会切出废品）。
- 不改 CAM 的 `KerfParams` / `ToolpathJobInput`，不改 CAM 的 M0 范围。

### 3.8 失败与退化处理（G-01 + G-02 状态码表）

| 场景 | 返回 | 说明 |
|---|---|---|
| shape 为空 | `NullShape` | `isNull() == true` |
| 帧非法（法向/ X 轴退化） | `InvalidArgument` | 长度 < 1e-12 或 X 与 N 平行 |
| 没有平行平面（合法输入） | `Success` + `planes` 空 | 由调用方决定回退或提示 |
| 存在非平面面 | `Success` + `skippedFaceCount > 0` | 诊断，不失败 |
| wire 首尾不闭合 | `Success` + `openWireCount > 0`，环 `closed=false` | 不强行闭合，避免造假 |
| 环面积 < 1e-9 mm² | 丢弃 + 计数 | 退化环（零长度边） |
| OCCT 抛异常 | `OperationFailed` / `InternalError` | 消息非空 |
| kerf：`kerfWidth < 0` | `code=1` | 非法参数 |
| kerf：`side` 越界 | `code=1` | |
| kerf：空输入 | `code=2` | 空集合 |
| kerf：补偿后环全消失 | `ok=true` + `contours` 空 + `droppedLoopCount>0` | 合法退化（孔被割没） |
| kerf：Clipper 抛异常 | `code=99` | Clipper2 以异常上报时统一收口 |
| STEP 落图：无闭合环 | 回退旧投影 + 告警日志 | 不产生空文档 |
| 布尔失败重试超限 | 见 6.5 | 与 G-01 无关但同链路 |

### 3.9 测试方案（程序化构造，不依赖真实文件）

测试目标：`GeoModelCore/Test/GmcCutContourTests.cpp`（新），以及 `Engine/2D` 侧 `KerfCompensatorTests.cpp`（新）。

| 用例 | 构造 | 断言（具体数值） |
|---|---|---|
| 平板无孔 | `TopoShape::makeBox(100,100,2)` | `planes.size()==1`（上下去重后）；`loops.size()==1`；`closed==true`；`hole==false`；`points.size()==4`；`|signedArea−10000| < 1e-6`；`perimeter≈400`（±1e-6） |
| 板带一孔 | `box(100,100,2)` + `GmcBoolean::cut` 半径 10 圆柱，圆心 (30,40) | `loops.size()==2`；外环 4 点、面积 10000；内环 `hole==true`，`|面积−π*100| < 0.05`（圆离散误差）；孔质心 `(30,40)` 误差 < 0.02；内环 `signedArea < 0` |
| 板带两个孔 | 同板，圆心 (25,25) 与 (75,75)，r=8 | 外环 1、内环 2；两孔面积各 `≈201.06`；`totalArea ≈ 10000 − 2*201.06` |
| 孔中岛（三层嵌套） | 100×100 板 → 40×40 孔 → 孔内 10×10 实体（fuse 回来） | 环数 3；深度 0/1/2；`hole` 依次 false/true/false |
| 倾斜板（非 Z 向） | 板绕 X 轴转 30°，`frame.normal` 同步给 `(0,-sin30,cos30)` | 抽取面积仍 10000（±1e-4）；若不改 frame，`planes` 为空（验证 frame 真的生效） |
| Z 向错位（精度回归） | 板平移到 (100000, 100000, 100000) | 面积仍 10000±1e-4；孔质心偏移误差 < 1e-3（证明 1e5 mm 下不吃 float） |
| 非平面件 | `makeSphere(50)` | `planes` 空、`skippedFaceCount==1`、`status.ok==true` |
| 半壳 | 球切一半，只剩自由边 | `extractSilhouette` 返回 1 条 `closed` 环；`hole==false`（v1 不解析孔） |
| 开口 wire | 手写退化：`makeBox` 后删一个面（用 `thicken` 生成壳体） | `openWireCount>0` 或 `closed==false`，不崩溃 |
| 异常收口 | 传入空 `TopoShape` | `NullShape`，无异常穿出 |
| kerf 基本语义 | 100×100 方框，`kerfWidth=1, side=Outside` | 外环面积 `101×101 = 10201`（Miter，±0.01）；周长 404 |
| kerf 孔洞反向 | 100×100 带 20×20 孔，`kerf=1, Outside` | 外环 10201；孔面积 `19×19=361`（**缩小**，±0.01） |
| kerf Inside | 同上，`side=Inside` | 外环 99×99=9801；孔 21×21=441 |
| kerf Center | 同上，`side=Center` | 输入输出面积完全一致（±1e-9） |
| kerf 零宽 | `kerfWidth=0` | 原样返回，点数不变 |
| kerf 孔被割没 | 20×20 板上 19×19 孔，`kerf=2, Outside` | `droppedLoopCount==1`；只剩外环 |
| kerf 尖角 | 三角形 45° 尖角，`JoinType=Round` | 输出无自交（用 `Geo2DQuery::` 自交检测或 Clipper `Orientation` 检查）；尖角处为圆弧 |
| kerf 开放路径 | 一条直线 Contour | 原样透传，`contours.size()` 不变 |

> 全部用例零真实 STEP/STL 资产；形状由 `makeBox/makeSphere` + `GmcBoolean` 程序化生成。

### 3.10 依赖与 CMake 影响（G-01/G-02）

| 项 | 结论 |
|---|---|
| 新增 OCCT 库 | `TKBRep`（`BRepTools`/`BRep_Tool`）、`TKTopAlgo`（`BRepAdaptor`/`BRepBuilderAPI`）、`TKShHealing`（`ShapeAnalysis_FreeBounds`）——**均已链接**，无需新增 |
| `GmcCutContour.h` 是否 include OCCT | 否；只 include 自有头 |
| 新宏 | `GMC_ENABLE_CONTOUR` 不需要（无第三方可选依赖）。仅建议加编译期开关 `GMC_CONTOUR_V2`（FileIO 侧，PRIVATE） |
| `CMake/CheckDependencies.cmake` | 已确认该文件无 GeoModelCore 规则；新增类不改变依赖方向（仍只向下依赖 Ut/OCCT） |
| `BUILD_UI3D=OFF` | GeoModelCore 与 `BUILD_GEOMODELCORE` 独立；`GmcCutContour` 无 Qt、无 UI 依赖，可独立构建与测试 |
| Engine2D 侧 | `KerfCompensator` 只用 Clipper2（已有）与 `GeometryContext`；不加依赖 |
| 测试目标 | 新增 `GmcCutContourTests`（链 `GeoModelCore`，`AUTOMOC OFF`）；`KerfCompensatorTests`（链 `Engine2D`）；沿用仓库“测试链库不编源文件”的规则 |

### 3.11 分期与人日（G-01 + G-02）

| 期 | 内容 | 独立验收 | 粗略人日 |
|---|---|---|---|
| **A1** | G-01A `extract`（平面面环 + 拓扑 hole + 上下去重） | 3.9 前 8 个用例 | **4–5** |
| **A2** | `extractSilhouette`（自由边 + `ConnectEdgesToWires` 闭环） | 半壳/开口 wire 用例 | **3** |
| **A3** | FileIO `StepParser` 接新路径 + 灰度开关 + A/B 日志 | 同一 STEP 走两路，环数/面积对比表 | **2** |
| **B1** | `Geo2DOffset::offsetContoursByRole` + 方向语义锁定测试 | kerf 前 4 个用例 | **2** |
| **B2** | `KerfCompensator` + `ContourContainment`（hole 回填） | 全部 kerf 用例 | **2–3** |
| **B3** | `CamInputBridge` 接入 + 失败策略 + 日志 | 端到端：2D 场景 → 补偿后轮廓 → CAM 输入 | **1–2** |

合计 **14–17 人日**。其中 **A1+A3 = 6–7 人日**即可让主程序“STEP 进 2D 得到闭合多边形，可 DXF 导出”，是第一个可演示里程碑。

### 3.12 风险与回滚

| 风险 | 影响 | 处理 / 回滚 |
|---|---|---|
| 平面面抽取选错面（板件有凸台/凹槽） | 轮廓不是真正切割外形 | v1 只取“面积最大的平面组”；`sourceFaceCount`/`totalArea` 全部回传，UI 可提示“检测到 N 个平行面”；保留 `projectTopView` 回退 |
| 上下平面轮廓不严格一致（倒角/拔模件） | 去重失败 → 双份轮廓 | `dedupeParallelDuplicates` 只对“投影一致”的组生效，不一致就都返回，交给上层判断；不硬合并 |
| `deflection=0.01` 对外形尖角产生过多点 | DXF 体积大、CAM 段数多 | 直线段不受影响；圆弧按弦高；弦高已暴露为 `GmcContourOptions::deflection`，可按件调高；按曲率自适应取弦高列为 P2 |
| 相对容差与旧固定 1e-7 的换算不一致 | 老调用方对闭环判定预期漂移 | 换算口径固定为「包围盒对角线 × `relativeTolerance`」，默认 1e-6；旧值 1e-7 在 128mm 尺度等价于相对 7.8e-10，过严易误判开环 |
| `kerfMm` 被当成缝宽使用 | 偏置量翻倍，切出废品 | 单边语义在 `ProcessParams`/UI/文档统一标注；`kerfWidth = 2 × 单边补偿值` 只出现在桥接层一处 |
| CAM M1 二次偏置 | 叠加成两倍补偿 | 桥接层补偿后强制 `bucket.kerf.enabled=false`；M1 若接入需先核实该标志 |
| 老行为依赖 `projectTopView`（如虚线的“可见边”） | 视图外观变化 | 编译期 + 运行期双开关；A/B 用同一 STEP 对比“环数、闭合数、面积和” |
| `StepParser` 从散线变闭合多边形 | 老文档/测试断言数量变化 | 先只切“闭合环”为 Polygon，开放环仍 Polyline；相关测试按新语义修订（本轮不动测试） |
| kerf 方向直觉错误 | 切出废品 | 数学语义用方框测试锁定；`side` 完全沿用 CAM 既有枚举；UI 上标注“补偿方向需现场标定” |
| Clipper2 在小线段/尖角退化 | 输出自交或空环 | `droppedLoopCount` + 面积守卫 + 尖角用 Round/`miterLimit`；失败整批报错，不静默 |
| Engine2D 改动影响填充/偏移既有命令 | 回归 | 新函数为**新增**，不改 `offsetContours` 主体；只增加“分角色”入口 |

回滚点：`GMC_CONTOUR_V2=OFF`（FileIO）、`kerfParams.enabled=false`（桥接）、`KerfCompensator` 新增文件删除即可（无既有调用）。

### 3.13 DoD

1. `GmcCutContour` 的 3.9 用例全绿，且连续跑 20 次结果一致（无顺序依赖）。
2. 任意 1e5 mm 平移下，面积/周长误差 ≤ 1e-4 相对误差。
3. STEP 导入一份带孔平板，DXF 导出后闭合环数 = 预期（外 1 + 内 1），DXF 读取回环数一致。
4. kerf：方框/方孔面积断言全部命中，`side=Center` 与未补偿结果 bit 级一致（面积差 ≤1e-9）。
5. 全过程无异常穿出 DLL：用“故意空 shape / 非法 frame / 空轮廓集”三类输入验证状态码。
6. 无新增 OCCT 类型出现在公开头（CI 用 `CheckDependencies` + 人工 grep 双查）。

---

## 4. 方案二：G-03 STEP 装配 / 颜色 / 名称 / 材料

### 4.1 目标 / 非目标

**目标**

1. STEP 读取时保留**装配层级**（多零件），每个零件带名称、颜色、可选材料。
2. `GeoModel` 从“单 shape 容器”扩展为“可承载 N 个零件”，同时保持现有单 shape API 不变。
3. 提供装配的位姿（`TopLoc_Location` → 4x4 变换），供 3D 场景按实例摆放。
4. 写出 STEP 时保留颜色（可选期）。

**非目标**

- 不做 PMI/公差/标注（`XCAFDoc_DimTolTool` 不在本期）。
- 不做材质库/物理属性（只保留材料名字符串）。
- 不改 3D 场景的实体模型（只产出分件数据，UI 分件显示另行排期）。
- 不做 IGES（另一个缺口 G-17，不在本轮）。

### 4.2 API 设计

新增公开头：`GeoModelCore/Include/GeoModelCore/GmcAssembly.h`（落地文件名为 `GmcStepAssembly.h`）

```cpp
#pragma once

#include "GeoModelCore/GmcStatus.h"
#include "GeoModelCore/GeoModelDLL.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace GeoModelCore
{
    class TopoShape;

    // 零件元数据（POD，无 std::string，避免跨 DLL 分配）
    struct GmcPartInfo
    {
        char name[128] = { 0 };
        char material[64] = { 0 };
        uint32_t rgba = 0xFF808080u;   // 0xAARRGGBB，默认灰
        bool hasColor = false;
        bool hasMaterial = false;
        double transform[16] = { 1.0, 0.0, 0.0, 0.0,
                                 0.0, 1.0, 0.0, 0.0,
                                 0.0, 0.0, 1.0, 0.0,
                                 0.0, 0.0, 0.0, 1.0 };  // 列主序，与 applyTransformMatrix 一致
        double volume = 0.0;            // mm^3（诊断/分组用）
        int32_t partIndex = -1;
    };

    struct GmcAssemblyInfo
    {
        size_t partCount = 0;
        bool hasColors = false;
        bool hasNames = false;
        char unitName[16] = "mm";       // 诊断用；OCCT 读 STEP 统一归一到 mm
    };

    struct GmcAssemblyResult
    {
        GmcStatus status;
        GmcAssemblyInfo info;
        // 零件形状单独取，避免在结果里塞 shared_ptr 数组
        std::vector<std::shared_ptr<TopoShape>> parts;
    };

    class GEOMODEL_API GmcStepAssembly
    {
    public:
        // 读取装配；parts 与 info.partCount 一一对应
        static GmcAssemblyResult read(const char* filePath);
    };
}  // namespace GeoModelCore
```

`GeoModel` 扩展（`GeoModelDocument.h`，additive）：

```cpp
class GEOMODEL_API GeoModel
{
public:
    // 现有：loadStepFile / saveStepFile / getShape / setShape ...

    // 新增：装配导入
    GmcStatus loadStepAssembly(const char* filePath);

    size_t partCount() const;
    bool getPartInfo(size_t index, GmcPartInfo* outInfo) const;
    std::shared_ptr<TopoShape> getPartShape(size_t index) const;
};
```

**约束**

- `GmcPartInfo` 用固定数组，不用 `std::string`（与 `GmcBooleanResult` 相同的 ABI 收口原则）。
- `parts` 用 `std::vector<shared_ptr>` 只在“同编译器/同 CRT”内使用，与现有 `TopoShape` 约定一致。
- 失败时 `parts` 为空、`status.ok=false`，`getPartInfo` 越界返回 false 并写 `InvalidArgument`。

### 4.3 内部实现步骤

1. 确认 `GMC_ENABLE_XCAF` 编译期打开；否则 `read()` 直接返回 `NotSupported`（不抛异常）。
2. `STEPCAFControl_Reader reader;`
   - `reader.SetNameMode(true);`（**必须**，否则 `TDataStd_Name` 读不到）
   - `reader.SetColorMode(true);`
   - `reader.SetMaterialMode(true);`（若 OCCT 版本支持）
   - `reader.SetLayerMode(false);`（v1 不要图层）
3. `IFSelect_ReturnStatus st = reader.ReadFile(filePath);` 非 `IFSelect_RetDone` → `FileFormatError`（区分 `RetFail`/`RetVoid` 消息）。
4. `Handle(TDocStd_Document) doc = new TDocStd_Document("BinXCAF");`（或 `XCAFDoc_DocumentTool::NewDocument`），`reader.Transfer(doc)`。
5. `Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());`
6. `TDF_LabelSequence roots; shapeTool->GetFreeShapes(roots);`
7. 递归展开实例（`shapeTool->GetComponents(label, comps, false)`）：
   - 对每个 `comp`：`shapeTool->GetReferredShape(comp, refLabel)`；
   - 形状 = `shapeTool->GetShape(comp)`；位姿 = `comp.Location().Transformation()` → 4x4（列主序，注意 OCCT `gp_Trsf` 的行主序转置）；
   - 名称：`TDataStd_Name::Get(refLabel)` 或 `comp` 本身；
   - 颜色：`colorTool->GetColor(refLabel, XCAFDoc_ColorGen|Surf|Curv, quantity)` → `Quantity_Color` → `rgba`；
   - 材料：`XCAFDoc_MaterialTool::GetMaterial(refLabel, name, description, density, ...)`；
   - 体积：`BRepGProp::VolumeProperties`（诊断用，失败置 0，不影响整体）。
8. 无 color/name 时 `hasColor/hasName` 保持 false，`rgba` 用默认灰。
9. 组装次数上限保护：`partCount > 10000` → `OperationFailed`（防病态文件撑爆内存）。
10. 出口统一 try/catch。

**已知坑**

| 坑 | 处理 |
|---|---|
| `TDataStd_Name` 读不到 | 必须 `SetNameMode(true)`；名称也可能挂在 `refLabel` 而不是实例 label，两处都取 |
| 颜色挂在“面/曲面”上而非零件上 | 依次尝试 `XCAFDoc_ColorGen` → `ColorSurf` → `ColorCurv`；都取不到则 `hasColor=false`，**不猜** |
| 装配里的“子装配” | 递归 `GetComponents`；子装配本身不产出零件，只展开叶子 |
| 同一零件多次实例化 | 每个实例一条 `GmcPartInfo`，`transform` 不同、形状共享；由 UI 决定是否合并 |
| `gp_Trsf` 行列序 | OCCT 是行主序（`Value(row,col)`），本项目 `applyTransformMatrix` 是列主序 → 必须转置 |
| 单位 | `STEPCAFControl_Reader` 按 `Interface_Static("xstep.cascade.unit")` 或文件单位换算到 mm；读完把 `unitName` 固定回报 "mm"，并在日志打印原文件单位（若可读） |
| 大文件 | `Transfer` 全程阻塞，需在后台线程调用；本接口不承诺异步，调用方负责 |
| XCAF 文档生命周期 | `TDocStd_Document` 必须在返回前释放；`TopoDS_Shape` 已脱离文档（shape 独立持有 TShape），不依赖文档存活 |

### 4.4 数据流与模块边界

```
STEP 文件
   │
   └─ GeoModel::loadStepAssembly  （GeoModelCore，需 TKXCAF/TKDESTEP）
        │
        ├─ partCount / getPartInfo(i) ──► 3D 场景（名称/颜色/位姿）
        └─ getPartShape(i) ────────────► GmcMesh::tessellate → 每零件一个实体
                                           │
                                           └─ G-01 每零件单独抽轮廓（天然分件）

兼容路径：
   GeoModel::loadStepFile 仍然存在（单 compound），旧调用方零改动
```

边界：

- **不把装配信息塞进 `GmcMeshData`**（网格是渲染物）；颜色/名称只在 `GmcPartInfo`。
- 3D 场景的分件显示由 `UI/3D` 决定，本轮方案只保证内核能给出“N 个形状 + N 份元数据”。
- `FileIO::StepParser` 走 2D 落图，**不消费装配**；多零件 STEP 的 2D 化仍按“所有零件轮廓叠加”处理，是否按零件分图层属后续排期（本方案只要求 `GmcCutContour::extract` 能对单零件 shape 调用）。

### 4.5 精度与容差策略

- 装配不涉及数值容差；位姿 `gp_Trsf` 为 double，直接转 4x4。
- 体积用 `BRepGProp::VolumeProperties`，`OnlyClosed=true`；非实体返回 0 并标 `volume=0`（不报错）。
- 颜色用 `Quantity_Color` 的 `Red/Green/Blue`（0..1）→ 8 位量化，量化误差 ≤1/255，文档写明。

### 4.6 失败与退化

| 场景 | 返回 |
|---|---|
| 文件不存在 | `FileNotFound` |
| 非 STEP / 解析失败 | `FileFormatError` |
| XCAF 未编译 | `NotSupported`（消息说明需 `GMC_ENABLE_XCAF`） |
| 无根对象 | `Success` + `partCount==0`（合法空装配） |
| 存在坏零件（形状空） | 跳过该件 + `partCount` 只计有效件；日志告警 |
| 颜色/名称缺失 | `hasColor/hasName=false`，不失败 |
| 零件数超限 | `OperationFailed` |
| OCCT 异常 | `OperationFailed` / `InternalError` |

### 4.7 测试方案

| 用例 | 构造 | 断言 |
|---|---|---|
| 读取装配（2 件） | **自写最小 STEP 文本**（测试内联字符串，写入 `%TEMP%`，用完删除）或二期用 `saveStepAssembly` 往返 | `partCount==2`；名称分别为 `PartA/PartB`；`rgba` 精确等于写入值；`transform` 平移分量 = (10,0,0)/(0,20,0) |
| 单零件旧文件兼容 | `makeBox` 写单实体 STEP | `partCount==1`；`name` 非空或空均可；`hasColor==false` |
| 无颜色文件 | 纯几何 STEP | `hasColors==false`；`rgba` 全为默认灰 |
| 颜色量化 | 写入 RGB(0.2,0.4,0.6) | 量化后 `(51,102,153)` ±1 |
| 位姿 | 写入带平移/旋转的实例 | `transform` 与写入矩阵逐元素差 < 1e-9 |
| 空装配 | 写只有根节点的 STEP | `partCount==0`、`status.ok` |
| 不存在文件 | 路径 `"Z:/__no_such__.step"` | `FileNotFound` |
| XCAF 关闭 | 编译宏关闭时 | `NotSupported`，不崩 |
| 体积 | 100×100×2 板 | `volume≈20000`（±1e-6 相对） |

> 说明：`G-03` 无法完全避免“写文件”，因为 XCAF 的读取入口是文件/流。测试采用“测试内生成的最小 STEP 文本（不可变内联字符串）+ 临时目录写读”，不依赖仓库内任何 STEP 资产，也不依赖外部下载。

### 4.8 依赖与 CMake

| 项 | 结论 |
|---|---|
| 新增库 | `TKXCAF`、`TKCAF`、`TKLCAF`、`TKDESTEP`（`STEPCAFControl_Reader`）；可选 `TKXDECAF` 不需要 |
| 可用性 | 已在本机 `C:\Users\xx\vcpkg\installed\x64-windows\lib` 确认存在 `TKXCAF.lib / TKCAF.lib / TKDESTEP.lib`；**CI/打包机未核实**（见第 8 节） |
| 链接方式 | 建议 **PRIVATE**：公开头不含 XCAF/OCCT，消费方无需 include。注：现有 `_occ_lib` 是 PUBLIC（历史遗留），本方案不为新库沿用该泄漏 |
| 新宏 | `GMC_ENABLE_XCAF`（`option`，默认 ON；实现文件内 `#if defined(GMC_ENABLE_XCAF)`），**PRIVATE 定义**，不向 FileIO 传播；`read()` 在关闭时返回 `NotSupported` |
| `GMC_ENABLE_STEP_IO` | 不改语义；但 `loadStepAssembly` 依赖 STEP 传输 → 若 `GMC_ENABLE_STEP_IO=OFF`，`loadStepAssembly` 也返回 `NotSupported` |
| `CheckDependencies.cmake` | 不新增模块依赖（仍是 GeoModelCore 内部增加 OCCT 库），门禁不触发 |
| `BUILD_UI3D=OFF` | 可独立构建；XCAF 只影响 `GeoModelDocument.cpp` 的一个分支 |
| 构建失败风险 | 若 CI 的 OCCT 不含 `TKDESTEP`，需回退到 `GMC_ENABLE_XCAF=OFF`；不得假设可用 |

### 4.9 分期与人日

| 期 | 内容 | 独立验收 | 粗略人日 |
|---|---|---|---|
| **C1** | CMake 打开 `TKXCAF/TKDESTEP` + `GMC_ENABLE_XCAF`，空实现返回 `NotSupported` | 全量构建通过（含 CI 依赖探测） | **0.5** |
| **C2** | `GmcStepAssembly::read` 读根/实例/名称/颜色/位姿 | 4.7 前 5 个用例 | **3** |
| **C3** | `GeoModel` 装配容器 + 兼容旧入口 | 旧 STEP 用例零回归 | **1.5** |
| **C4** | 3D 场景按零件出实体 + 元数据透传（UI/3D 侧） | 导入 2 件 STEP，场景 2 实体、颜色正确 | **2–3** |
| **C5** | 写出保留颜色（可选） | 往返测试 | **2** |

合计 **7–10 人日**（C5 可延后）。

### 4.10 风险与回滚

| 风险 | 处理 |
|---|---|
| CI 无 `TKDESTEP` | 先跑 C1 的依赖探测；失败则 `GMC_ENABLE_XCAF=OFF`，功能降级为 `NotSupported`，不阻塞其它两条 |
| 3D 场景从“单实体”变“多实体”冲击既有拾取/选择 | C4 单独立项 + 特性开关；旧路径 `loadStepFile` 保持单实体 |
| 颜色挂在面上导致取色不准 | 只在零件级取色；取不到就 `hasColor=false`，不臆造 |
| 大装配内存 | 上限 10000 件 + 日志；超限失败而不是 OOM |
| 回滚点 | `GMC_ENABLE_XCAF=OFF`；`loadStepAssembly` 无既有调用方，删除新文件即可 |

### 4.11 DoD

1. 读入 2 件装配，`partCount/name/rgba/transform` 全部命中测试断言。
2. `GMC_ENABLE_XCAF=OFF` 时构建通过、`read()` 返回 `NotSupported`、无异常。
3. 旧 `loadStepFile` 行为、已有 STEP 相关测试零回归。
4. `GeoModelCore` 公开头不出现 `Handle(...)` / `TDF_Label` / OCCT include。

---

## 5. 方案三：G-04 布尔可信度 + G-06 `isValid()` 语义

### 5.1 目标 / 非目标

**目标**

1. `isValid()` 从“非空”变为“`BRepCheck_Analyzer` 通过”，并提供“只判空”的 `isNull()`（已有）。
2. 布尔增加：结果分析器校验、共面/同面合并（`SimplifyResult` 或 `ShapeUpgrade_UnifySameDomain`）、失败按 fuzzy 逐级放大重试、体积健全性检查。
3. 布尔诊断可回传（重试次数、实际 fuzzy、面数前后、体积、无效面数），供 UI/日志定位。
4. `GmcSplit` 的守卫改为“空判 + 严格有效性 + 两侧结果校验”，无效输入返回明确错误。

**非目标**

- 不实现局部布尔（G-09 另立）。
- 不实现自交修复（G-14 另立）。
- 不改变布尔算法的输入语义（仍是两两布尔；XOR 仍是三趟）。
- 不承诺“任何病态输入都能成功”：重试有上限，超限必须失败并给出原因。

### 5.2 API 设计

`GmcTypes.h`（additive）：

```cpp
// 布尔诊断（只增字段，不改旧字段）
// 不提供 degreesOfFreedom：OCCT 的 BRepCheck 与布尔链路没有可靠的自由度来源，
// 硬造数值等于假数据，因此本结构只暴露可实测的面/子形状统计与体积。
struct GmcBooleanDiagnostics
{
    int32_t retryCount = 0;
    double fuzzyUsed = 0.0;          // 实际生效的 fuzzy（mm）
    int32_t facesBefore = 0;
    int32_t facesAfterUnify = 0;
    bool analyzerPassed = false;
    bool unified = false;
    double volumeA = 0.0;
    double volumeB = 0.0;
    double volumeResult = 0.0;
    int32_t invalidFaceCount = 0;    // BRepCheck 判为无效的面数；无面归属时保持 0
};

struct GmcBooleanOptions
{
    bool unifySameDomain = true;     // 共面合并
    bool checkResult = true;         // 跑 BRepCheck_Analyzer
    bool verifyVolume = true;        // 体积健全性
    double volumeTolerance = 0.0;    // 体积误差带（绝对值）：<=0 用 1e-6*max(1,Va,Vb)
    int32_t maxRetries = 3;          // fuzzy 重试上限（不含首轮）
    double fuzzyGrowth = 10.0;       // 每级放大倍数
    double fuzzyMax = 1.0;           // fuzzy 上限（mm，绝对）
};

struct GmcBooleanResult
{
    bool success = false;
    char errorMessage[256] = {};
    GmcBooleanDiagnostics diagnostics;   // 新增
};

// 形状检查结果
struct GmcShapeCheck
{
    bool valid = false;
    int32_t failedSubShapes = 0;
    char message[256] = {};
};
```

`GmcBoolean` 新增重载（不改旧签名）：

```cpp
// 带选项的布尔；旧签名内部转调此重载
static std::shared_ptr<TopoShape> operateWithOptions(
    GmcBooleanOp op,
    const TopoShape& a,
    const TopoShape& b,
    const GmcTolerance& tol,
    const GmcBooleanOptions& options,
    GmcBooleanResult* outResult = nullptr);
```

`TopoShape`（additive）：

```cpp
// 严格有效性：BRepCheck_Analyzer
bool isValid() const;
bool isNull() const;                 // 已有，语义不变
GmcShapeCheck checkShape() const;    // 新增，带失败子形状计数与消息
```

### 5.3 内部实现步骤（G-04）

统一入口 `operateWithOptions`：

1. 入参校验：`a.isNull() || b.isNull()` → `NullShape`，`success=false`。
2. 准备副本：`BRepBuilderAPI_Copy`（几何复制、不复制网格），避免 `BRepAlgoAPI` 修改入参（`SetNonDestructive(true)` 也已经保证，但复制让重试的起点一致）。
3. `fuzzy` 序列：`f = max(tol.linear, 1e-7)`；`attempts = max(1, maxRetries + 1)`：
   - 构造 `BRepAlgoAPI_Fuse/Common/Cut`（XOR：`Cut(a,b) ∪ Cut(b,a)`，两趟后再 fuse，**保留三趟语义**）。
   - `SetFuzzyValue(f)`、`SetNonDestructive(true)`、`SetRunParallel(true)`、`SetGlue(BOPAlgo_GlueOff)`（不猜）。
   - `Build()`；`if (HasErrors())` → 记录消息，`f = min(f * fuzzyGrowth, fuzzyMax)`，continue。
4. 成功即 `result = op.Shape()`；`if (result.IsNull())` → 视为失败，重试。
5. 共面合并（`unifySameDomain`）：
   - 先试 `op.SimplifyResult(/*unifyEdges*/ true, /*unifyFaces*/ true)`（同一对象内完成，改动最小）；
   - 若 `SimplifyResult` 未降面数，再走 `ShapeUpgrade_UnifySameDomain unifier(result, /*unifyEdges*/ true, /*unifyFaces*/ true, /*concatBSplines*/ false)`；`SetAngularTolerance(1e-3)`、`SetLinearTolerance(max(1e-3, 模型对角线 * 1e-6))`、`Build()`、`result = unifier.Shape()`。线性容差按模型尺度取值，不使用固定的绝对 1e-7。
   - **坑**：unify 后子形状索引全部失效（`getFace(i)` 顺序变化、之前缓存的 `TopoDS_Face` 句柄失效）。文档与 API 注释必须写明“布尔返回的是新形状，索引不可跨调用缓存”。G-05（未来 faceId）必须在本步之后重新生成。
6. 结果校验：`BRepCheck_Analyzer analyzer(result, /*geomControls*/ true)`；`IsValid()`（严格）；同时逐面统计 `invalidFaceCount`（状态非 `BRepCheck_NoError` 的面数，整体无效但无面归属时记 0）。
   - 失败 → 视为本轮失败，fuzzy 放大重试；
   - 仍失败且到达上限 → 统一由收尾消息给出，见步骤 10。
7. 体积健全（`verifyVolume`，仅当 `Va > 0 && Vb > 0` 时执行，无界半空间/非实体跳过）：
   ```
   eps = volumeTolerance > 0 ? volumeTolerance : 1e-6 * max(1.0, Va, Vb)
   Union        : V >= max(Va,Vb) - eps && V <= Va+Vb + eps
   Intersection : V <= min(Va,Vb) + eps
   Difference   : V <= Va + eps
   Xor          : V <= Va+Vb + eps
   ```
   不满足 → 视为异常，重试；超限失败（消息含三个体积）。
8. 回填 `GmcBooleanDiagnostics`（`retryCount/fuzzyUsed/facesBefore/facesAfterUnify/analyzerPassed/unified/volumes/invalidFaceCount`）。
9. 出口 try/catch；`Standard_Failure` → `OperationFailed`，`std::exception` → `OperationFailed`，其它 → `InternalError`。
10. 全部尝试失败时统一消息：`boolean failed after <attempts> attempts: <最后一次失败原因>`；旧入口 `fuse/cut/common/operate` 转调 `operateWithOptions(op, a, b, tol, GmcBooleanOptions{}, outResult)`，**默认开启** unify + check（这是行为变化，见 5.10 风险）。

**fuzzy 上限的理由**：`GmcTolerance.linear=1e-7` 在 1e5 mm 尺度下相当于相对 1e-12，过于严格；薄壁/共面件需要放大。初始值取 `max(tol.linear, 1e-7)`，每次按 `fuzzyGrowth` 放大并 clamp 到 `fuzzyMax=1.0 mm`，后者是“明显错位不该靠 fuzzy 蒙混”的护栏。

### 5.4 G-06 `isValid()` 实现

1. `TopoShapeImpl` 增加 `mutable int8_t m_validateState = 0;  // 0 未知 / 1 有效 / 2 无效`。
2. `isValid()`：
   - `if (m_shape.IsNull()) return false;`
   - 命中缓存直接返回；
   - `BRepCheck_Analyzer analyzer(m_shape, Standard_True);` 结果写缓存。
3. `checkShape()`：构造 `GmcShapeCheck`，填 `valid`、`failedSubShapes`（遍历顶层/壳/实体/面/边/顶点，统计 `Status() != BRepCheck_NoError` 的子形状数；整体无效但无归属时补记 1）、消息（取第一个失败状态的 `BRepCheck_Status` 文本映射为英文短句）。
4. 失效点：`setShape`、`makeBox/makeSphere/makeCylinder/makeCone/makeTorus`、`translate/rotate/scale/applyTransformMatrix` 内统一调用 `invalidateValidation()` 置 0；`fromImpl` 新建时天然 0。
5. 开销：一个 1000 面实体约 1–5 ms；`GmcSplit` 每次 2 次调用可接受。**禁止**在渲染/拾取的热路径调用 `isValid()`（在头文件注释写明）。
6. 不提供 `degreesOfFreedom`：`BRepCheck_Analyzer` 只给状态枚举，OCCT 没有逐面/逐边的可靠自由度计数来源；自行推导只能是假数据，故只暴露 `failedSubShapes` 与 `invalidFaceCount`。

### 5.5 GmcSplit 守卫

守卫统一走 `validateSplitInput`，位于 `splitByPlane` 与 `splitAtBoundingBoxMidplane` 入口：

```cpp
if (shape.isNull())  return error("null input shape");
if (!shape.isValid())
    return error("input shape failed validity check: <checkShape 消息> (failed subshapes: N)");
```

- 入参守卫同时判空与严格有效性，返回明确错误而不是静默继续。
- 两次布尔之后、判断实体体积之前，对两侧结果再做一次严格 `isValid()`，失败返回 `"split produced invalid shape"`。
- `GmcSplit` 的返回结构不改（`success` + `errorMessage`），保持既有调用方兼容。

### 5.6 失败与退化（状态码）

| 场景 | `success` | `errorMessage` 内容（英文） |
|---|---|---|
| 入参空 | false，`NullShape` | `"null operand"` |
| `HasErrors()` 且可重试 | 重试 | 记录最后一条 OCCT 错误 |
| 重试超限 | false | `"boolean failed after <attempts> attempts: <最后原因>"` |
| 结果空 | false | `"empty result shape"` |
| 分析器失败超限 | false | 收尾消息内嵌 `"result invalid: <status> (failed subshapes: N)"` |
| 体积越界超限 | false | 收尾消息内嵌 `"volume out of bounds (Va=..., Vb=..., V=...)"` |
| unify 后再次分析失败 | false | 收尾消息内嵌分析器状态 |
| 异常 | false | `"exception: <what>"` |

### 5.7 测试方案（全部程序化）

| 用例 | 构造 | 断言 |
|---|---|---|
| 共面合并 | `box(10,10,10)` + `box(10,10,10)` 平移 (10,0,0)，`fuse` | `unified==true`；`facesAfterUnify==6`；`analyzerPassed==true`；`volume≈2000`（±1e-6） |
| 不合并的对照 | 同上但 `GmcBooleanOptions.unifySameDomain=false` | 面数 > 6（典型 10）；证明合并确实起作用（A/B） |
| 薄壁 cut | `box(100,100,0.2)` − `box(50,50,0.2)` | `success==true`；`isValid()==true`；`volume≈1500`（±1e-5） |
| 共面相切 cut | `box(10,10,10)` − `box(10,10,10)` 平移 (10,0,0)（仅贴面） | `success==true`；volume≈1000；结果有效 |
| 完全重合 | 同上平移 (0,0,0)，`cut` | `success`（体积 0）或明确失败；**不得返回无效实体**（`isValid()` 必须为 true 或形状为空） |
| fuzzy 重试 | 两 box 留 1e-5 缝隙做 fuse（期望失败） | `retryCount>=1`；`fuzzyUsed > tol.linear`；最终 `success` 或明确失败二选一，由实测锁定 |
| fuzzy 上限 | 构造 0.5 mm 缝隙 | 超过 `fuzzyMax` 后失败；`retryCount == maxRetries`；消息含 `attempts` |
| 体积越界 | 用 `makeTorus(50,40)` 与自身 `cut`（结果为 0） | `volumeResult≈0`；不越界；成功 |
| XOR | 两个相交 box | 体积 ≈ `Va+Vb-2Vcommon`（±1e-4），且有效 |
| 分析器负例（开口壳体） | 从 box 中移除一个面后包成实体 | `isValid()==false`；`checkShape().failedSubShapes>0`；`message` 非空 |
| 加厚过度 | 10×10 面加厚 50（结果自交） | OCCT 不抛异常且 `isValid()==true`：按现状记录，不作为预期契约 |
| 体积误差带 | 大尺度相邻 box（舍入使 V 略高于 Va+Vb） | 默认口径通过；`volumeTolerance=1.0` 时失败，消息含 `volume out of bounds` |
| 无效面数诊断 | 共面 box fuse，unify 关/开各取一次 | `invalidFaceCount` 两次均为 0（均可通过分析器） |
| 缓存 | 同一 shape 连续 `isValid()` 3 次 | 结果一致；可用计数器注入验证只跑一次分析器（测试友好改造，最小侵入） |
| 空形状 | 默认构造 `TopoShape` | `isNull()==true`、`isValid()==false`、`checkShape().message` 非空 |
| GmcSplit 回归 | `box` 沿 X 中面切 | 成功；两半均 `isValid()`；`GmcSplitResult` 结构不变 |
| 异常收口 | 传入 `TopoShape::fromImpl` 的异常 shape | `GmcStatus` / `success=false`，无 `Standard_Failure` 穿出 |
| 性能基线 | 1000 面实体 `isValid()` | 单次 < 20 ms（记录值，不做硬门槛） |

### 5.8 依赖与 CMake

| 项 | 结论 |
|---|---|
| `BRepCheck_Analyzer` | `TKTopAlgo`（已链接） |
| `ShapeUpgrade_UnifySameDomain` | `TKShHealing`（已链接） |
| `BRepAlgoAPI::SimplifyResult` | `TKBO`（已链接） |
| 新头文件 | 不需要新增头文件（只扩展 `GmcTypes.h` / `GmcBoolean.h` / `TopoShape.h`），OCCT 仍不进公开头 |
| 宏 | 无 |
| `BUILD_UI3D=OFF` | 无影响 |
| CheckDependencies | 无影响（只增 OCCT 库使用，不增模块依赖） |

### 5.9 分期与人日

| 期 | 内容 | 验收 | 粗略人日 |
|---|---|---|---|
| **D1** | `isValid()` + `checkShape()` + 缓存 + 失效 | 缓存/空形状/负例用例 | **1.5** |
| **D2** | 布尔诊断结构 + fuzzy 重试骨架（暂不 unify） | fuzzy/异常/超限用例 | **2** |
| **D3** | unify + 体积健全 + 诊断回填 | 共面/薄壁/体积用例 | **2–2.5** |
| **D4** | `GmcSplit` 守卫迁移 + 既有测试回归 | GmcSplit 全部测试 | **1** |

合计 **6.5–8 人日**。

### 5.10 风险与回滚（G-04/G-06）

| 风险 | 影响 | 处理 |
|---|---|---|
| `isValid()` 变严格后，既有测试/流程把“非空即可”当作有效 | 回归失败 | D1 先只在 `isValid()` 上加严格；`GmcSplit` 同步迁移（D4）；全量跑 `MainTests`，逐条确认失败原因是“确实无效形状”而不是误判 |
| unify 改变面索引 | 依赖面序号的下标访问全部失效 | 布尔结果注释明确“新形状、索引不可缓存”；`getFace(i)` 调用方（UI/导出）不得跨布尔缓存索引；如发现依赖，先关 `unifySameDomain` 再逐案修 |
| unify 合并了不该合并的面（不同颜色/材质） | 视觉/属性信息丢失 | 当前 `TopoShape` 无颜色/材质，风险为零；G-03 的 `GmcPartInfo` 在零件级，不在面级，不受影响 |
| fuzzy 放大掩盖真实几何错误 | 切出错误但“有效”的实体 | `fuzzyMax` 硬上限 + 体积健全 + 诊断回传；UI 显示 `fuzzyUsed`，超阈值告警 |
| 布尔性能下降（多跑分析器与 unify） | 交互卡顿 | 诊断里带耗时；`checkResult` 可关（只对批量/后台用）；默认开 |
| `SimplifyResult` 在旧 OCCT 版本行为差异 | 合并效果不稳定 | 先探测 `SimplifyResult` 是否降面数，不行再走 `UnifySameDomain`；两条路径都写测试 |
| 回滚 | — | `GmcBooleanOptions.unifySameDomain=false / checkResult=false` 即可回到接近旧行为；`isValid()` 无法逐调用回滚，需以测试清单确认 |

### 5.11 DoD

1. 共面 fuse 的面数 = 6（±0），并给出“关闭 unify 时的面数”作为 A/B 证据。
2. 薄壁 0.2 mm cut 结果 `isValid()==true` 且体积误差 ≤1e-5 相对。
3. 重试次数、实际 fuzzy、面数前后、体积、无效面数全部出现在 `diagnostics` 中。
4. `isValid()` 对空形状返回 false，对有效实体返回 true，分析器只跑一次（缓存生效）。
5. `GmcSplit` 空入参返回 `null input shape`，无效入参返回带子形状计数的有效性错误，结果无效返回 `split produced invalid shape`，既有 split 测试全绿。
6. 所有失败路径 `errorMessage` 非空且为英文。

---

## 6. 总排期（建议）

| 批次 | 内容 | 可独立验收 | 人日 |
|---|---|---|---|
| 第 1 批 | D1 + D2（`isValid()` + 重试骨架） | 布尔负例/空形状用例 | 3.5 |
| 第 2 批 | D3 + D4（unify + 体积 + `GmcSplit` 迁移） | 共面/薄壁 + split 回归 | 3.5 |
| 第 3 批 | A1（G-01A 平面面环） | 平板/带孔/1e5 mm 用例 | 4.5 |
| 第 4 批 | A3（FileIO 落图 + 灰度） | STEP → DXF 端到端 | 2 |
| 第 5 批 | B1 + B2（kerf + hole 回填） | kerf 数值用例 | 4.5 |
| 第 6 批 | B3 + C1/C2（桥接 + XCAF 读） | 2D→CAM 端到端；2 件装配 | 5.5 |
| 第 7 批 | A2 + C3（silhouette + 装配容器） | 退化件；旧入口零回归 | 4.5 |
| 第 8 批 | C4 + C5（3D 分件显示 + 颜色写出，可延后） | 场景 2 实体；STEP 往返 | 4.5 |

合计约 **32.5 人日**（不含 C5 则 30）。第 4 批结束即可给业务演示“STEP 进 2D、闭合轮廓、DXF 可下料”。

---

## 7. 需拍板问题

| 编号 | 问题 | 影响 | 建议 |
|---|---|---|---|
| Q1 | **是否接受新增 `TKXCAF` / `TKCAF` / `TKDESTEP` 依赖？**（本机已有，CI 未核实） | 决定 G-03 是否可做 | 接受，但 C1 先做依赖探测；CI 缺库则 `GMC_ENABLE_XCAF=OFF` 降级 |
| Q2 | **G-01 v1 是否只支持“板厚方向 = Z”？** | 决定是否要自研取景 UI / 姿态识别 | 建议接受（`frame` 已允许任意方向，UI 默认 Z；斜放件靠人工指定坐标系，后续再做自动识别） |
| Q3 | 2D 场景“逐图元拼接成闭合环”是否本轮做？ | 决定 DXF 散线能否自动成环 | 建议**不做**（3D/STEP 路径不缺；DXF 闭合多段线原本就映射 Polygon） |
| Q4 | kerf 是否允许“失败即整批失败”？ | 用户体验 | 建议允许（静默降级会切废品），但需 UI 明确提示 |
| Q5 | 布尔默认开启 `unifySameDomain` 是否可接受（行为变化） | 面数/索引变化 | 建议接受，但先跑全量测试，若发现索引依赖再局部关 |
| Q6 | `GmcProjection`（HLR 顶视）是否保留 | 回退与 A/B | 建议长期保留，作为非棱柱件的兜底与对照 |
| Q7 | 颜色/名称口径：`rgba` 用 0xAARRGGBB 还是 0xRRGGBBAA？ | 与 UI 颜色约定一致性 | 建议对齐 UI 现有约定（需 UI 侧确认，见 8） |
| Q8 | kerf 落点是否偏离 `Docs/CAM层架构规划.md`（该文档计划在 CAM 内调 `Engraving::PolygonOffset`） | 决定是否要先修 CAM 架构文档与依赖约束 | 建议按本方案落 Engine2D + 桥接，并把 CAM 架构文档第 681 行改为“桥接层调 Engine2D::KerfCompensator”，以免后续照旧文档实施时打破依赖约束 |
| Q9 | `GmcCutContour::extractSilhouette` 的 `hole` 是否必须在内核内解析 | 决定内核是否要写一遍嵌套判定 | 建议 v1 不解析（`hole=false`），由 2D 侧 `ContainmentResolver` 统一回填 |

---

## 8. 未确认项

1. **OCCT 实际版本**：`GmcCurve.cpp` 里有 `OCC_VERSION_MAJOR>=8` 分支，但 vcpkg 包版本未核实；`STEPCAFControl_Reader::SetMaterialMode` 与 `SimplifyResult` 在不同版本行为可能不同。
2. **CI / 打包机的 OCCT 是否包含 `TKDESTEP`**：本机 vcpkg 有，CI 是否同一份包未确认。
3. **`GMC_ENABLE_HLR` 是否真的生效**：仅当 `TKHLRBRep` 目标存在才启用，当前构建是否命中未确认；影响旧路径兜底效果（不影响新路径）。
4. **Clipper2 `InflatePaths` 的正负 delta 与绕向关系**：必须以 3.9 的方框测试实测锁定，文档不假设。
5. **`BRepCheck_Analyzer` 对“加厚过度”是否稳定返回无效**（可能直接抛出 `Standard_ConstructionError`）：需实测，测试用例会分两条路径断言。
6. **UI 颜色约定（ARGB 还是 RGBA）**：需 UI 侧确认后才能定 `GmcPartInfo.rgba` 的位序。
7. **STEP 单位**：OCCT 读取时按 `xstep.cascade.unit` 换算到 mm；本仓是否有全局设置未确认（当前无显式设置，走默认）。
8. **3D 场景是否已有“按零件多实体”的容器能力**：`entityBRep` 是 `unordered_map`（支持多实体），但 `loadStepFile` 只写一个；C4 需 UI/3D 侧确认渲染与选择链路能承受多实体。
9. **`BRepModelService3D::alignShapeToMesh` 的 float 偏差是否已被其它任务修复**：本轮不修，但若并行任务正在改该函数，需要合并时对齐（G-01 不依赖它）。

---

## 9. 本轮声明

- 本轮**只写本文档**：未修改任何源码、未修改 CMake、未执行 `git commit` / `push`。
- 未触碰工作区其它并行任务的未提交改动（`SceneRefreshCoordinator`、`ImportProgressRunner`、翻译 `.ts`、`Raster/Nesting/CAM` 相关等）。
- 文中所有“已有能力”的判断均来自源码阅读与调用点检索；人日为粗略估算，用于排期参考而非承诺。
