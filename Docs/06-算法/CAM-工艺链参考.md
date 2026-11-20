# CAM 工艺链参考

> 性质：实现参考（"现在到底有什么、怎么开、坑在哪"），不是设计提案。
> 代码快照：2026-11-12，以磁盘上的 `CAM/CAM` 源码为准。
> 同目录评审：[工业级激光加工算法完备性评审-2026-11-11.md](./工业级激光加工算法完备性评审-2026-11-11.md)（历史描述，结论以本文为准）。
> 架构规划：[CAM层架构规划.md](../CAM层架构规划.md)。

本文只描述 `CAM/CAM`（`Cam::` 命名空间）这条工艺编译管线。生产链实际走的是 `Main/Src/Hardware/MotionPlanCompiler`，两者关系见第 4 章——**能力实现 ≠ 生产生效**。

---

## 1. 模块边界与数据流

### 1.1 输入

唯一入口是 `Cam::compile(const ToolpathJobInput&, CamPlan&, Progress*, CamResult*)`，声明在 `CAM/CAM/Include/CAM/CamCore.h`。

输入结构（`CAM/CAM/Include/CAM/ToolpathJobInput.h`）：

```
ToolpathJobInput
├── jobId[128]
├── layers : vector<LayerBucket>
│   └── LayerBucket
│       ├── layerId / visible / locked / outputEnabled
│       ├── params        : ProcessParams   ← 功率/频率/脉宽/速度/遍次/弦高容差
│       ├── kerf          : KerfParams      ← 割缝（默认关闭）
│       ├── leadInOut     : LeadInOutParams ← 引线/引出（默认关闭）
│       ├── pierce        : PierceParams    ← 穿孔（默认关闭）
│       ├── corner        : CornerParams    ← 拐角（仅被功率曲线消费）
│       ├── powerCurve    : PowerCurve      ← 功率曲线（默认关闭）
│       ├── passStrategy  : PassStrategy    ← 当前编译流程未读取
│       ├── commonEdge    : CommonEdgeParams← 共边切割（默认关闭）
│       └── entities : vector<EntitySnapshot>
│           └── EntitySnapshot
│               ├── kind ∈ {Contour, Point, Fillable, Raster, Unsupported}
│               ├── contours : vector<Contour>   （kind == Contour）
│               ├── point                        （kind == Point）
│               ├── scanlines : vector<ScanlineSegment>（kind == Raster，桥接层已换成 mm）
│               └── visible / locked / unmachinableReason
└── selectionOnly / selectionCount / emitArcs / skipHiddenLayers / skipLockedLayers
    / emitTrailingLaserOff / failOnUnmachinable
```

坐标与单位：位置 mm、角度度、速度 mm/s、功率百分比 0..100、时间 µs，全部用户坐标（左下原点、X 右 Y 上），CAM 内不做机器坐标变换。

### 1.2 内部管线的真实调用顺序

入口函数：`CAM/CAM/Src/CamCore.cpp` 的 `compile()`。

```
compile()
├── 阶段 1  validateInput()                      // 参数合法性 / 弦高上界 / 空作业
├── 阶段 2  逐 LayerBucket（保持输入顺序）
│   ├── isLayerExcluded() 命中 → 整层跳过，skippedEntityCount += 图元数
│   ├── orderedEntities = 按 entityId 升序排序      // 骨架顺序，无最近邻/2-opt
│   ├── prepareContourEntities()  ← 每层只做一次，各遍次复用
│   │   ├── ① KerfCompensator::apply()           // 逐源轮廓
│   │   ├── ② CommonEdgePlanner::apply()         // 整层一次，跨图元（关闭时直接搬移）
│   │   └── 逐链：③ LeadInOutPlanner::apply()
│   │            ④ PowerCurvePlanner::apply()
│   │            ⑤ PiercePlanner::apply()
│   ├── 逐 pass（passCount = max(1, params.passes)）
│   │   └── 逐图元：emitPreparedContour() / Point / Raster / Unsupported
│   │        ├── 从 currentPosition 到首段起点插一条 rapid（距离 > 1e-6 时）
│   │        └── 累计切削/空移长度 + 包围盒（圆弧并入四个基向极值点）
│   └── 非空 PlanLayerResult 入 plan.layers
└── 阶段 3  判空 → 组装包围盒 + CamResult（错误分支与成功分支各组装一次统计）
```

**顺序要点（与部分设计文稿不同，代码为准）：**

- 实际顺序是 **割缝 → 共边 → 引线 → 功率曲线 → 穿孔**。
- `CommonEdgePlanner` 必须在引线之前：它按"发射顺序靠前的轮廓删共享边"工作，删边后轮廓会变成多条开口链，引线才有"该不该生成"的判断依据。
- `PiercePlanner` 放在最后：它会把 `piercePowerPercent` / `pierceSpeedMmPerSec` 覆盖到穿孔后的首条切削段上，优先级高于功率曲线（用例 `CamPowerCurveTests.PiercePowerWinsOnFirstCut`）。
- 几何变换按层做一次、按遍次复用；`currentPosition` 跨图层、跨遍次连续，不每层归零。

### 1.3 输出

`CamPlan`（`CAM/CAM/Include/CAM/CamPlan.h`）：

```
CamPlan
├── layers : vector<PlanLayerResult>       // 一个图层 × 一个遍次 = 一个元素
│   ├── layerId / passIndex + 工艺参数副本（功率/频率/脉宽/速度/空移速度）
│   └── contours : vector<PlanContour>
│       ├── closed / hole / sourceEntityId / sourceLayerId
│       ├── piercePoints : vector<Vec2d>
│       ├── segments : vector<PlanPathSegment>   // Line / Arc / Dwell
│       │   └── isRapid / dwellUs / powerPercent / speedMmPerSec / passIndex（-1 = 继承）
│       └── lengthMm / cuttingLengthMm / rapidLengthMm
├── totalCuttingLengthMm / totalRapidLengthMm
└── boundsMinX/MinY/MaxX/MaxY
```

`CamResult`（`CAM/CAM/Include/CAM/CamContext.h`）承载错误码、可读错误文本与各阶段统计字段；字段全集见 2.1～2.5 的"统计字段"条目。

说明：
- `PlanPathSegment` 的 `dStartAngle` / `dEndAngle` 由点重算，不采信输入值（`toPlanSegment()`）。
- `Dwell` 段长度为 0，不进入 `lengthMm`。
- 只有产出至少一条轮廓的 `PlanLayerResult` 才会进入 `plan.layers`。

---

## 2. 逐阶段说明

### 2.1 割缝补偿 `KerfCompensator`

- 代码：`CAM/CAM/Src/GeometryTransform/KerfCompensator.h|.cpp`，`KerfCompensator::apply(params, chordToleranceMm, contour, stats)`；调用点 `CamCore.cpp` 的 `prepareContourEntities()`。
- 做什么：把闭合轮廓按割缝半宽做等距偏置，使切完后的零件尺寸等于名义轮廓。外轮廓与孔洞的物理偏移方向相反，因此必须依赖正确的 `hole` 标记。

**参数入口与默认值**（`KerfParams`）：

| 字段 | 默认 | 含义 |
|---|---|---|
| `enabled` | `false` | 总开关 |
| `widthMm` | `0.0` | **总缝宽**（mm），补偿量 = `widthMm / 2` |
| `side` | `0` | `0=Outside` 向材料外偏，`1=Inside` 向材料内偏，`2=Center` 不补偿 |
| `arcToleranceMm` | `0.01` | Clipper2 偏置的圆弧弦高容差；实际取 `max(1e-3, arcToleranceMm)` |

另有两个隐式入口：输入轮廓离散用 `LayerBucket::params.chordToleranceMm`（默认 0.01）。

**几何判定规则**：

1. `!enabled || widthMm <= 1e-9 || side == 2` → 原样返回单元素副本，不计数。
2. `flattenContour()` 必须 `valid && closed`，否则不补偿。
3. 用有向面积符号判断材料侧：`materialIsLeft = hole ? (area < 0) : (area > 0)`。
4. 左向偏移量 `offsetLeft`：Outside 时 `materialIsLeft ? -half : half`；Inside 时取反。
5. Clipper2 的正 delta 恒为"向外扩"，故 `delta = (area > 0) ? -offsetLeft : offsetLeft`。
6. `Clipper2Lib::InflatePaths(..., JoinType::Miter, EndType::Polygon, miterLimit=2.0, arcTol)`，再用 `Union(NonZero)` 消自交/重叠/重复环——尖角按 miterLimit 截断，不会出无限长尖刺。
7. 输出是折线 `Contour`（`closed=true`，`hole` 继承源轮廓，保持与源轮廓一致的绕向），段数 < 3 或面积过小的环丢弃。

**退化与拒绝路径**：

| 场景 | 行为 | 统计字段（`KerfStats` / `CamResult`） |
|---|---|---|
| 开口 / 不连续轮廓 | 原样保留，不补偿 | `KerfStats::skippedOpenContours` → `kerfSkippedOpenContourCount` |
| 输入环面积 ≤ `1e-4 mm²` | 直接丢弃该轮廓 | `KerfStats::droppedContours` → `kerfDroppedContourCount` |
| 偏置后无有效环 | 返回空（该轮廓不产出刀路） | 同上 |
| 偏置后裂成多环 | 全部保留并逐一发射 | `KerfStats::splitContours` → `kerfSplitContourCount` |
| 成功补偿 | —— | `KerfStats::compensatedContours` → `kerfCompensatedContourCount` |
| Clipper2 抛异常 | 原样保留 | `KerfStats::failedContours`（**`CamResult` 没有对应字段，不会上报**） |
| 全部轮廓都塌陷 | `compile` 返回 `CamError::DegenerateGeometry`（`"Kerf compensation collapsed all contours"`） | 走错误分支组装统计 |

**测试**：`CAM/CAM/Test/CamKerfTests.cpp`
`Disabled_KeepsOriginalGeometry`、`Outside_ExpandsOuterAndShrinksHole`、`Inside_ShrinksOuterContour`、`ArcContour_RemainsClosedWithinTolerance`、`Inside_ThinPartCollapses_ReportsDegenerate`、`OpenContour_IsSkippedAndPreserved`、`Inside_ThinNeckSplitsIntoTwoLoops`。

---

### 2.2 引线 / 引出 `LeadInOutPlanner`

- 代码：`CAM/CAM/Src/GeometryTransform/LeadInOut.h|.cpp`，`LeadInOutPlanner::apply(params, chordTolMm, closed, hole, cuttingSegments, stats, outLeadInApplied)`；调用点同上。
- 做什么：在首条切削段之前插入引线、在末条之后追加引出线，并校验引线落点位于**废料侧**（外轮廓之外 / 孔内），避免落进成品。

**参数入口与默认值**（`LeadInOutParams`）：

| 字段 | 默认 | 含义 |
|---|---|---|
| `enabled` | `false` | 总开关 |
| `leadInLengthMm` | `0.0` | 引线长度；`> 1e-9` 才生成直线/切线/法向引线 |
| `leadOutLengthMm` | `0.0` | 引出线长度；`> 1e-9` 才生成 |
| `leadInMode` | `0` | `0=tangent`、`1=normal`、`2=straight`、`3=arc` |
| `leadInAngleDeg` | `0.0` | 仅 `mode=2`：与切线夹角，内部夹到 0..90 |
| `leadInArcRadiusMm` | `0.0` | 仅 `mode=3`：`<= 0` 时按 `leadInLengthMm / sweep` 推导 |
| `leadInArcAngleDeg` | `90.0` | 仅 `mode=3`：扫角，内部夹到 1..180 |
| `leadOutMode` | `0` | 取值同 `leadInMode` |
| `leadOutAngleDeg` | `0.0` | 引出直线角度 |
| `checkScrapSide` | `true` | 关闭后只按几何方向生成，不做落点校验 |
| `rampAngleDeg` | `0.0` | **当前规划器未使用** |

**几何判定规则**：

1. 跳过首尾 `Dwell` 后定位第一条/最后一条切削段；找不到则直接返回。
2. 切线：Line 取方向向量；Arc 按绕向取左/右法向（`counterClockwise ? left : right`）。零长度线或零半径弧无法求切线 → 整个规划器直接返回（不计数）。
3. `flattenPlanSegments()` 得到的折线只在 `valid && pts.size() >= 3` 时作为落点判定边界。
4. 材料侧：`materialIsLeft = hole ? (area < 0) : (area > 0)`；废料侧法向取相反一侧。
5. 引线方向：`0` 切线反向；`1` 废料侧法向；`2` 切线反向与废料侧法向按 `leadInAngleDeg` 插值；`3` 以废料侧为圆心侧构造圆弧（圆心在 `锚点 + 废料法向 × 半径`），绕向取与首段切线同向的那一侧。
6. 落点校验 `scrapCheckPasses()`：`checkScrapSide && closed && boundary >= 3` 才生效；外轮廓要求点在环外，孔要求点在环内；直线引线只校验端点，圆弧引线同时校验远端与中点。
7. 校验失败时先镜像方向（直线取反向，圆弧把圆心换到材料侧），成功则记一次 `fallbackCount`；两方向都失败则放弃该引线。

**退化与拒绝路径**：

| 场景 | 行为 | 统计字段（`LeadInOutStats` / `CamResult`） |
|---|---|---|
| 首/末段切线退化 | 不生成任何引线，直接返回 | 不计数 |
| 落点校验两侧均失败 | 放弃引线（引出线同理） | `skippedContours` → `leadInOutSkippedContourCount` |
| 镜像方向成功 | 生成镜像引线 | `fallbackCount` → `leadInOutFallbackCount` |
| 至少生成一条引线或引出线 | —— | `appliedContours` → `leadInOutAppliedContourCount` |
| 共边删边后的开口链 | `CamCore` 侧直接跳过，不调用本规划器 | `CommonEdgeStats::leadInSkippedCount` → `commonEdgeLeadInSkippedCount` |

**测试**：`CAM/CAM/Test/CamLeadInOutTests.cpp`
`Disabled_NoExtraSegments`、`TangentLeadIn_StartsBehindOnScrapSide`、`NormalLeadIn_EntersPerpendicularFromScrap`、`StraightLeadIn_HonorsAngle`、`TangentLeadIn_SkipsWhenBothSidesInMaterial`、`ArcLeadIn_IsTangentAndOnScrapSide`、`LeadOut_ContinuesAlongEndTangent`、`HoleLeadIn_LandsInsideHole`、`LeadInWithKerf_UsesCompensatedBoundary`。

---

### 2.3 穿孔 `PiercePlanner`

- 代码：`CAM/CAM/Src/Process/PiercePlanner.h|.cpp`，`PiercePlanner::apply(params, chordTolMm, closed, hole, hasLeadIn, cuttingSegments, outPiercePoints, stats)`。
- 做什么：在切削起点插入穿孔延时（`Dwell`），支持穿孔点沿废料侧法向避让、以及"已预穿孔"只标注不打孔。
- 调用条件（`CamCore.cpp`）：`layer.pierce.enabled || layer.pierce.prePierced`。

**参数入口与默认值**（`PierceParams`）：

| 字段 | 默认 | 含义 |
|---|---|---|
| `enabled` | `false` | 总开关 |
| `pierceDelayUs` | `500` | 单次穿孔延时；`0` 时不生成该 Dwell |
| `piercePowerPercent` | `30.0` | 覆盖到穿孔后首条切削段；`< 0` 不覆盖 |
| `pierceSpeedMmPerSec` | `50.0` | 同上；`<= 0` 不覆盖 |
| `pierceCount` | `1` | 穿孔次数；`<= 0` 视为 1 |
| `dwellUs` | `200` | 穿孔后的稳定延时；`0` 时不生成 |
| `pierceOffsetMm` | `0.0` | `> 0` 且无引线时，穿孔点沿废料侧法向偏移 |
| `prePierced` | `false` | `true` 时不插延时，只记录穿孔点 |

**几何判定规则**：

1. 定位首条非 `Dwell` 段；没有则返回。
2. `prePierced` 优先：把首段起点记入 `outPiercePoints`，`prePiercedContours++`，直接返回（**即使 `enabled == false` 也会标注**）。
3. 首段切线退化（零长度线 / 零半径弧）→ 放弃穿孔，`skippedContours++`。
4. 废料侧法向：与引线模块同一套判定（`materialIsLeft` → 右法向，否则左法向）。
5. 避让条件：`pierceOffsetMm > 1e-9 && closed && !hasLeadIn`。此时插入一条 `偏移点 → 切削起点` 的直线段（等效短引线），`offsetContours++`。
6. 延时组成：`pierceDelayUs > 0` 时插入 `max(1, pierceCount)` 个 Dwell；`dwellUs > 0` 时再追加 1 个。插入位置在首条切削段之前（若已插入避让直线，则 Dwell 排在它的前面，即"在偏移点打孔，再切到起点"）。
7. 功率/速度覆盖落在 `firstIndex + dwells.size()` 处的首条非 Dwell 段：功率夹到 0..100，速度直接赋值。
8. `outPiercePoints` 每个轮廓最多一个点。

**退化与拒绝路径**：

| 场景 | 行为 | 统计字段（`PierceStats` / `CamResult`） |
|---|---|---|
| 首段切线退化 | 跳过穿孔 | `skippedContours` → `pierceSkippedContourCount` |
| 开口轮廓 | 不参与内外侧判定，不做偏移，只在起点穿孔 | 不额外计数 |
| 两个延时都为 0 | 不插 Dwell，但仍计一次穿孔 | `piercedContours`、`pierceEvents` |
| 预穿孔 | 只标注点 | `prePiercedContours` → `piercePrePiercedContourCount` |
| 正常插入 | —— | `piercedContours` → `pierceContourCount`；`pierceEvents` → `pierceEventCount`；`dwellSegments` → `pierceDwellSegmentCount`；`offsetContours` → `pierceOffsetContourCount` |

**测试**：`CAM/CAM/Test/CamPierceTests.cpp`
`Disabled_NoDwell`、`Enabled_InsertsDwellBeforeCut`、`PowerAndSpeed_ApplyToFirstCutOnly`、`PrePierced_NoDwellButMarksPoint`、`OffsetPierce_LandsOnScrapSide`、`OffsetPierce_SkippedWhenLeadInExists`、`OpenContour_NoOffset`、`NonPositiveCount_NormalizedToOne`、`DegenerateFirstSegment_Skipped`。

---

### 2.4 功率曲线 `PowerCurvePlanner`

- 代码：`CAM/CAM/Src/Process/PowerCurvePlanner.h|.cpp`，`PowerCurvePlanner::apply(curve, corner, layerSpeedMmPerSec, cuttingSegments, stats)`。
- 做什么：按速度查"速度→功率"曲线逐段赋功率；可选起停过渡（沿刀路距离渐变）；并在 `corner.strategy == 2` 时对进入锐角的段降速（功率随新速度查表一起降）。

**参数入口与默认值**：

`PowerCurve`：

| 字段 | 默认 | 含义 |
|---|---|---|
| `enabled` | `false` | 总开关 |
| `points` | 空 | `{speedMmPerSec, powerPercent}` 点表 |
| `startTransitionMm` | `0.0` | 起刀过渡距离；0 = 不做 |
| `endTransitionMm` | `0.0` | 收刀过渡距离；0 = 不做 |
| `transitionMinPowerPercent` | `0.0` | 过渡端点功率下限，夹到 0..100 |

`CornerParams`：`strategy`（0=sharp, 1=round, 2=slowdown，默认 0）、`radiusMm`、`overcutMm`、`slowdownFactor`（默认 0.5）。

**几何判定规则**：

1. 点表归一化（`pcBuildPoints`）：非有限值、负速度丢弃并计入 `droppedPoints`；功率夹到 0..100；**相同速度后写覆盖先写**（也计入 `droppedPoints`）；按速度升序。
2. 查表（`pcSample`）：低于首点取首点值、高于末点取末点值（不外推），段内线性插值。
3. 段速度：`seg.speedMmPerSec > 0` 用段级覆盖，否则用 `layerSpeedMmPerSec`。
4. 拐角降速：`strategy == 2 && 0 < slowdownFactor < 1`；相邻非 Dwell 段的转向角 `>= 10°` 时，把**进入段**速度设为 `layerSpeedMmPerSec × slowdownFactor`（不是在该段原速度上乘）。切线退化（零长度段）的拐角不做标记。
5. 起停过渡：先按累计路径长度在 `startTransitionMm` 与 `total - endTransitionMm` 处切段（长段被切开，否则逐段功率表达不出渐变），再按段中点距离算 `factor = minScale + (1 - minScale) × f`，两端取 `min`，功率乘 `factor` 后夹到 0..100。
6. `Dwell` 不参与功率映射（穿孔功率由 `PiercePlanner` 负责）。

**退化与拒绝路径**：

| 场景 | 行为 | 统计字段（`PowerCurveStats` / `CamResult`） |
|---|---|---|
| 点表归一化后为空 | 完全跳过，不改任何段 | `skippedContours` → `powerCurveSkippedContourCount`；`droppedPoints` → `powerCurveDroppedPointCount` |
| 非法/重复点 | 丢弃或覆盖 | 同上 `droppedPoints` |
| 拐角降速生效 | 改段速度 | `cornerSegments` → `powerCurveCornerSegmentCount` |
| 段落入过渡区 | 功率被缩放 | `transitionSegments` → `powerCurveTransitionSegmentCount` |
| 有段被赋功率 | —— | `curveSegments` → `powerCurveSegmentCount`；`appliedContours` → `powerCurveAppliedContourCount` |
| 零长度段 | 切线退化，该拐角不标记，其余照常 | 不计数 |

**测试**：`CAM/CAM/Test/CamPowerCurveTests.cpp`
`Disabled_NoSegmentOverrides`、`MapsSpeedToPower_WithEndpointClamp`、`InterpolatesBetweenPoints`、`ClampsBelowFirstAndAboveLastPoint`、`CornerSlowdown_ReducesSpeedAndPower`、`StartTransition_RampsUpAndSplitsFirstEdge`、`EndTransition_RampsDownAtTail`、`InvalidAndDuplicatePoints_AreNormalized`、`EmptyPoints_SkipsContour`、`PiercePowerWinsOnFirstCut`。

---

### 2.5 共边切割 `CommonEdgePlanner`

- 代码：`CAM/CAM/Src/GeometryTransform/CommonEdgePlanner.h|.cpp`，`CommonEdgePlanner::apply(params, input, output, stats)`；输入输出元素类型为 `CommonEdgeChain`。
- 做什么：相邻零件的公共轮廓只切一次。共享边从发射顺序靠前的轮廓上删除、保留在靠后的轮廓上——先切的零件在公共边未切开时仍被材料固定，后切的零件一次切出公共边并释放两件。

**参数入口与默认值**（`CommonEdgeParams`）：`enabled = false`、`toleranceMm = 0.05`（端点/圆心/半径匹配容差）。

**几何判定规则**：

1. 参与检测的前提（`ceContourEligible`）：`closed == true` 且段数 `>= 3`。零长度直线段不参与匹配。
2. 直线共享边：端点互换后重合（容差内）。
3. 圆弧共享边：同圆心、同半径、`counterClockwise` 相反、端点互换后重合。
4. 每个候选边只向后（`j > i`）寻找匹配；同一条边匹配到 2 个及以上后续轮廓 → 三方共享，拒绝。
5. 删边前必须两端闭合（`ceEndpointsClosed`）：前段 `ptEnd == 当前段 ptStart` 且当前段 `ptEnd == 后段 ptStart`。
6. 用并查集把互相共边的轮廓归组；若组内成员在发射顺序上不连续（`maxIdx - minIdx + 1 != members.size()`），整组拒绝。
7. 应用去重：只删 `earlier` 轮廓上的那条边，`later` 轮廓保留。
8. 未处理的重叠只计数不删边：共线且重叠长度 `> toleranceMm`，或同圆但端点不匹配。
9. 输出：未删边的轮廓原样输出；有删边的轮廓拆成若干开口链（`closed = false`、`trimmedByCommonEdge = true`），链间由发射器插入空移。

**退化与拒绝路径**：

| 场景 | 行为 | 统计字段（`CommonEdgeStats` / `CamResult`） |
|---|---|---|
| 输入不足 2 条链 | 原样输出 | 不计数 |
| 打开/少于 3 段 | 不参与检测 | 不计数 |
| 三方及以上共享 | 拒绝去重 | `threeWayConflictCount` → `commonEdgeThreeWayConflictCount` |
| 共享边两端不闭合 | 拒绝删边 | `openEndpointConflictCount` → `commonEdgeOpenEndpointConflictCount` |
| 共边组不连续 | 整组拒绝 | `nonAdjacentGroupCount` → `commonEdgeNonAdjacentGroupCount` |
| 部分重叠 / 同圆不同端点 | 只计数，不裁剪 | `unsupportedOverlapCount` → `commonEdgeUnsupportedOverlapCount` |
| 成功删边 | 拆开口链 | `sharedSegmentCount` → `commonEdgeSharedSegmentCount`；`trimmedContourCount` → `commonEdgeTrimmedContourCount`；`chainCount` → `commonEdgeChainCount` |
| 全部边被删光 | 丢弃该轮廓 | `droppedContourCount` → `commonEdgeDroppedContourCount` |
| 拆链后引线被跳过 | `CamCore` 侧不调用引线 | `leadInSkippedCount` → `commonEdgeLeadInSkippedCount` |

**测试**：`CAM/CAM/Test/CamCommonEdgeTests.cpp`
`Disabled_KeepsBothContoursClosed`、`Enabled_SharedEdgeCutOnce`、`ThreeWayConflict_RejectedAndCounted`、`NonAdjacentGroup_RejectedAndCounted`、`OpenEndpointConflict_RejectedAndCounted`、`TrimmedChain_SkipsLeadInAndCounts`、`PartialOverlap_NotTrimmedOnlyCounted`。

---

## 3. 怎么开启

前置：`Cam::compile()` 只读 `ToolpathJobInput`。以下片段针对"直接构造 `ToolpathJobInput` 的调用方"（测试、基准、桥接层）。经 `CamInputBridge` 的生产/测试桥接另有约束，见第 4 章。

### 3.1 割缝补偿

```cpp
// 总缝宽 0.4 mm，向材料外侧补偿（外轮廓外扩、孔洞内缩）
input.layers[0].kerf.enabled = true;
input.layers[0].kerf.widthMm = 0.4;        // 总缝宽；单边 = widthMm / 2
input.layers[0].kerf.side = 0;             // 0=Outside, 1=Inside, 2=Center(不补偿)
input.layers[0].kerf.arcToleranceMm = 0.01;
```

### 3.2 引线 / 引出

```cpp
auto& lead = input.layers[0].leadInOut;
lead.enabled = true;
lead.leadInLengthMm = 3.0;
lead.leadInMode = 0;                       // 0=tangent 1=normal 2=straight 3=arc
lead.leadInAngleDeg = 0.0;                 // 仅 mode=2：0=切线，90=法向
lead.leadInArcRadiusMm = 0.0;              // 仅 mode=3：<=0 时按长度/扫角推导
lead.leadInArcAngleDeg = 90.0;             // 仅 mode=3：1..180
lead.leadOutLengthMm = 2.0;                // 0 = 不生成引出线
lead.leadOutMode = 0;
lead.leadOutAngleDeg = 0.0;
lead.checkScrapSide = true;
```

### 3.3 穿孔

```cpp
auto& pierce = input.layers[0].pierce;
pierce.enabled = true;
pierce.pierceDelayUs = 500;
pierce.piercePowerPercent = 30.0;
pierce.pierceSpeedMmPerSec = 50.0;
pierce.pierceCount = 1;
pierce.dwellUs = 200;
pierce.pierceOffsetMm = 0.3;               // >0 且无引线时，穿孔点先退到废料侧
pierce.prePierced = false;                 // true = 只标注穿孔点，不插延时
```

### 3.4 功率曲线（含拐角降速）

```cpp
auto& pc = input.layers[0].powerCurve;
pc.enabled = true;
pc.points = { { 100.0, 20.0 }, { 200.0, 50.0 }, { 300.0, 80.0 } };
pc.startTransitionMm = 1.0;
pc.endTransitionMm = 1.0;
pc.transitionMinPowerPercent = 5.0;

// 拐角降速是功率曲线的子能力，必须同时打开 slowdown 策略
input.layers[0].corner.strategy = 2;
input.layers[0].corner.slowdownFactor = 0.5;
```

### 3.5 共边切割

```cpp
input.layers[0].commonEdge.enabled = true;
input.layers[0].commonEdge.toleranceMm = 0.05;
// 前提：参与轮廓必须 closed 且段数 >= 3；
// 与割缝补偿同时开启时两侧共享边各自偏置，通常检测不到（保守但无收益）
```

### 3.6 "默认关闭 = 刀路不变" 的守护

五个功能全部默认关闭。关闭态的回归由各测试文件的 `Disabled_*` 用例守着，它们逐段断言几何、段数与统计为零：

| 功能 | 用例 |
|---|---|
| 割缝 | `CamKerfTests.Disabled_KeepsOriginalGeometry` |
| 引线/引出 | `CamLeadInOutTests.Disabled_NoExtraSegments` |
| 穿孔 | `CamPierceTests.Disabled_NoDwell` |
| 功率曲线 | `CamPowerCurveTests.Disabled_NoSegmentOverrides`（同时断言 `powerPercent < 0`、`speedMmPerSec <= 0`，即无逐段覆盖） |
| 共边 | `CamCommonEdgeTests.Disabled_KeepsBothContoursClosed` |

生产链侧的"逐字节"比对在 `Main/Src/UI/Test/MotionPlanKerfLeadTests.cpp`：`MotionPlanKerfLeadTest.KerfOff_IsByteIdenticalToBaseline`、`MotionPlanKerfLeadTest.LeadInOff_IsByteIdenticalToBaseline`（逐条 `MotionCommand` 比较）。

---

## 4. 与生产链的关系

**结论先说：`Cam::compile` 不是生产链。** 生产链是 `MotionPlanCompiler`，两条管线各有一套实现，参数类型也不通用。

### 4.1 两条管线

```
【A. Cam::compile 管线】（当前无生产调用方）
  2D 文档
    ↓ CamBridge::buildJobInput()        Main/Src/CAM/CamInputBridge.cpp  ← 仅测试调用
  Cam::ToolpathJobInput
    ↓ Cam::compile()                     CAM/CAM/Src/CamCore.cpp
  Cam::CamPlan
    ↓ CamPlanEmitter::emitCamPlan()      Main/Src/CAM/CamPlanEmitter.cpp  ← 仅测试调用
  Hw::MotionPlan

【B. 生产链】
  2D 文档 + ToolpathJobSpec
    ↓ MotionPlanCompiler::compile()      Main/Src/Hardware/MotionPlanCompiler.cpp
    │   ├─ Eg::Cam::GeometryExtractor          （几何唯一入口）
    │   ├─ Eg::Cam::KerfCompensator            （生产自有割缝实现，prepareKerf）
    │   ├─ makeLeadInPoint()                   （生产自有引线，mode 0/1/2）
    │   └─ orderEntitiesInnerFirst()           （contourOrdering==1 时先内后外）
    ↓ Hw::MotionPlanBuilder
  Hw::MotionPlan → ProcessingJobService → DeviceHost → 适配器
```

- `Main/CMakeLists.txt`：`CamInputBridge.cpp` 仅在 `BUILD_CAM=OFF` 时被排除，`CamPlanEmitter.cpp` 在 `BUILD_CAM` 或 `BUILD_HARDWARE` 关闭时被排除——两者会编进主程序，但全仓除测试外无调用点。
- `CamBridge::buildJobInput()` 只搬运割缝与引线两类参数（`kerf.enabled/widthMm/side` 与 `leadInOut.enabled/leadInLengthMm/leadInMode/leadInAngleDeg`），穿孔、功率曲线、共边、拐角在桥接层没有参数来源，因此即使走 `Cam::compile`，由桥接驱动的输入里它们也永远是默认关闭。

### 4.2 能力对照

| 能力 | `Cam::compile` 管线 | 生产链 `MotionPlanCompiler` | 差异说明 |
|---|---|---|---|
| 割缝补偿 | 有（`KerfCompensator`） | **已生效**（`Eg::Cam::KerfCompensator`，`kerfMm != 0` 触发） | 两套实现；生产侧先做整层 `fillHoleFlags` 再统一偏置 |
| 引线 | 有（直线/切线/法向/圆弧，可镜像兜底） | **已生效**（`makeLeadInPoint`） | 生产侧只支持 mode 0/1/2，无圆弧引线 |
| 引出线 | 有 | **未接入** | 生产链无引出线参数与实现 |
| 穿孔 | 有 | **未接入**（`Main/Src/Hardware` 检索 `pierce` 零匹配） | 生产链不产生穿孔 Dwell / 穿孔功率覆盖 |
| 功率曲线 | 有 | **未接入** | 生产链功率只按图层 `SetLaserParams` 下发 |
| 拐角降速 | 有（功率曲线内） | **未接入** | —— |
| 共边切割 | 有 | **未接入** | —— |
| 轮廓顺序 | 按 `entityId` 升序（骨架） | 默认文档顺序；`contourOrdering == 1` 时先内后外 | 两条管线顺序策略不同 |
| 位图扫描线 | 支持（`EntitySnapshot::scanlines`） | `GeometryKind::Raster` 归入不可加工 | 位图未进生产链 |
| 参数类型 | `Cam::LayerBucket` 各结构体 | `Eg::ProcessParams` | 字段语义/默认值/单位不同，不能互填 |

**因此：在 `CAM/CAM` 里做完某个阶段，不代表真机上会执行它。** 判断某能力是否生效，只认 `MotionPlanCompiler.cpp` 里的实际调用，而不是 `CamCore.cpp`。

---

## 5. 已知陷阱

### 5.1 割缝：总缝宽 vs 单边 + 符号

同一件事有三套表达，换算方向写错会直接切错尺寸：

| 位置 | 字段 | 语义 |
|---|---|---|
| `Cam::KerfParams`（`CAM/CAM/Include/CAM/ToolpathJobInput.h`） | `widthMm` + `side` | **总缝宽**（非负）+ 方向枚举 `0=Outside,1=Inside,2=Center` |
| `Eg::ProcessParams`（`Engine/Common/Include/Engine/Process/ProcessParams.h`） | `kerfMm` | **单边补偿、带符号**：`>0` 外侧、`<0` 内侧、`0` 不补偿 |
| `Eg::KerfSpec`（`Engine/Common/Include/Engine/Process/ProcessModel.h`） | `kerfMm` | 同上，单边 + 符号 |
| `Eg::Cam::KerfOptions`（生产链） | `kerfWidth` | 总宽 = `2 × fabs(kerfMm)` |

换算只准走显式函数：`Eg::KerfSignedFromTotalWidth(totalWidthMm, side)`、`Eg::KerfSideFromSigned(signedKerfMm)`、`Eg::KerfSingleSideFromTotalWidth()`、`Eg::KerfTotalWidthFromSingleSide()`。

方向错误示例（都会静默成功，不报错）：
- 把单边值填进 `KerfParams.widthMm`：实际补偿量再减半。
- 把总缝宽填进 `kerfMm`：生产链按 `2 × |kerfMm|` 解释，总宽翻倍。
- 只改 `widthMm` 不改 `side`：`side` 默认 `0`（Outside），想内缩却仍外扩。
- `side = 2`（Center）会让整个补偿器直接返回原轮廓，`widthMm` 被完全忽略。

`CamInputBridge` 的正确写法（可作参照，`Main/Src/CAM/CamInputBridge.cpp`）：`width = 2 * fabs(kerfMm)`、`side = kerfMm >= 0 ? 0 : 1`。

### 5.2 引线 mode 枚举在不同层的语义差异

同一个 `leadInMode` 整数在三个地方含义不完全一致：

| 层 | 0 | 1 | 2 | 3 | 其它值 |
|---|---|---|---|---|---|
| `Cam::LeadInOutParams`（`LeadInOut.cpp` 的 `lineOutward` / `apply`） | tangent | normal | straight（`leadInAngleDeg` 夹 0..90） | arc | 落到 `else` 分支，按 tangent 处理 |
| `Eg::ProcessParams::leadInMode`（生产链） | 沿切线反向 | 沿废料侧法向 | 斜线（`leadInAngleDeg` 未夹到 90，只校验 0..180） | **未实现** | 按切线反向处理 |
| `Eg::ProcessModel::LeadInOutSpec` | 0..3 全部合法声明 | | | | 校验依赖 `validateProcessModel` |

需要注意的三点：
1. **在生产链填 `leadInMode = 3` 不会生成圆弧引线，会静默退化成切线引线**（`makeLeadInPoint` 只有 `==1` / `==2` 两个分支，其余走切线）。要圆弧引线只能走 `Cam::compile`。
2. `leadOutMode == 3` 在 CAM 里**复用引线的半径与扫角字段**（`leadInArcRadiusMm` / `leadInArcAngleDeg`），没有独立的引出圆弧参数；而 `leadOutLengthMm` 在 mode 3 下只用于"半径 <= 0 时推导半径"，不决定弧长。
3. `leadInMode != 3` 但只填了 `leadInArcRadiusMm > 0`、`leadInLengthMm == 0` 时：触发条件 `leadInLengthMm > 1e-9 || leadInArcRadiusMm > 1e-9` 成立，但直线分支算出的引线长度为 0，起点落在轮廓上。`Geom::pointInPolygon` 把边界点视为内部，于是外轮廓两次镜像都判为材料侧、最终计入 `leadInOutSkippedContourCount`；孔轮廓则"校验通过"却生成一条零长度引线。两种情况都是静默无效，属于无意义参数组合。

### 5.3 其它已知边界

- **共边 + 割缝同时开启**：割缝把每条轮廓各自偏置后，公共边不再逐字节重合，共边检测通常命中不了（`CommonEdgePlanner.h` 文件头已注明）。不会切错，但没有收益。
- **共边拆出的开口链不生成引线**：`CamCore` 检测到 `trimmedByCommonEdge` 就跳过 `LeadInOutPlanner` 并计 `commonEdgeLeadInSkippedCount`，因为不闭合的剩余边界无法判定废料侧。
- **穿孔功率优先级最高**：穿孔在功率曲线之后执行，`piercePowerPercent` / `pierceSpeedMmPerSec` 会覆盖首条切削段（`CamPowerCurveTests.PiercePowerWinsOnFirstCut`）。
- **`prePierced` 绕过 `enabled`**：`CamCore` 的调用条件是 `enabled || prePierced`，所以只开 `prePierced` 也会标注穿孔点。
- **`KerfStats::failedContours` 没有上报出口**：`CamResult` 无对应字段，偏置异常时只能从"该轮廓几何未变化"侧面发现。
- **拐角降速改写段速度的基准**：`corner` 分支用 `layerSpeedMmPerSec × slowdownFactor` 覆盖段速度，会抹掉该段原有的逐段速度覆盖。
- **`passStrategy` 未被读取**：`LayerBucket::passStrategy` 目前在 `CamCore.cpp` 中没有任何读取点，遍次只看 `params.passes`。

---

## 6. 扩展约定

新增一个工艺参数或一个阶段时，按以下规矩办（现状代码已经这么组织）：

1. **参数结构体只允许尾部追加字段**，不改既有字段顺序、不改既有字段语义。
   - 依据：`ToolpathJobInput.h` / `CamContext.h` 文件头的 ABI 约定；`CAM/CAM/Include/CAM/CAMExport.h` 规定"任何公开结构体的字段增删改（含顺序调整）都必须递增 `CAM_ABI_VERSION`"（当前为 2）。
2. **默认关闭 / 零影响**。新开关默认 `false`，新数值默认 `0`，且规划器的第一段必须是"关闭即原样返回"，不得触碰 `segments`、不得写 `powerPercent` / `speedMmPerSec` 覆盖字段。
3. **补统计字段，两处都要填**。`CamContext.h` 的 `CamResult` 尾部追加；同时在各规划器的 `*Stats` 结构里加对应计数。`CamCore.cpp` 的 `compile()` 有**两个**结果组装点（判空失败分支、成功分支），都要赋值；层内统计还要在 `prepareContourEntities()` 返回后累加进 `*StatsTotal`。
4. **补逐字节回归用例**。在 `CAM/CAM/Test/` 对应文件加 `Disabled_*` 用例，断言段数、端点与统计不变；若是生产链可用的能力，再在 `Main/Src/UI/Test/MotionPlanKerfLeadTests.cpp` 加 `*Off_IsByteIdenticalToBaseline` 式逐条 `MotionCommand` 比对。
5. **新测试文件要登记**：`CAM/CAM/Test/CMakeLists.txt` 的 `CAMTests` 源列表是显式枚举的。
6. **不要复用旧字段表达新语义**（例如不能用 `side` 兼作"是否避让"）；也不要在关闭路径上顺手"修正"输入几何。
7. **统计字段必须能被拒绝路径命中**：每条"拒绝/退化"分支都要有唯一计数，避免"静默少切"，这与已有的 `threeWayConflictCount`、`openEndpointConflictCount` 等做法一致。

---

## 7. 工艺模型（进行中）

> **本节仍在进行中，以实际落地代码为准。** 以下描述的是目标契约与当前已存在的结构，不代表已经接入 `Cam::compile`。

`Eg::ProcessModel`（`Engine/Common/Include/Engine/Process/ProcessModel.h`）正在定稿为**参数权威**：

- 结构：`version`（当前 `kProcessModelVersion = 1`）+ `defaults` / `defaultDetail` + `global: JobProcessSettings` + `layers: vector<LayerProcessEntry>`。
- `LayerProcessEntry` = `layerId` + `outputEnabled` + `params: Eg::ProcessParams` + `detail: ProcessDetail`。
- `ProcessDetail` 与 `LayerBucket` 的工艺字段一一对应：`KerfSpec`、`LeadInOutSpec`、`PierceSpec`、`CornerSpec`、`PowerCurveSpec`、`PassStrategySpec`、`CommonEdgeSpec`。
- `global` 承载跨层设置：`rapidSpeedMmPerSec`、`chordToleranceMm`、`emitArcs`、`skipHiddenLayers`、`skipLockedLayers`、`emitTrailingLaserOff`、`failOnUnmachinable`。
- 解析入口：`findLayerProcess()`、`resolveLayerParams()`（层内 rapid 为 0 时继承全局）、`resolveLayerDetail()`。
- 校验与钳制：`validateProcessModel()` 收集全部问题（`ProcessModelIssue`，`field` 是稳定路径如 `layers[3].pierce.powerPercent`），`normalizeProcessModel()` 把 Warning 级越界钳到合法范围。
- 版本兼容：字段只追加；读到高于本端支持的版本必须拒绝并报错，不许静默降级；旧档案缺字段按"零影响默认"读取。

目标态与当前态的关系：

```
现在：  2D 文档 → CamInputBridge（手写搬运）→ ToolpathJobInput::LayerBucket → Cam::compile
目标：  Eg::ProcessModel（参数权威）
            ↓ 生成编译快照
         ToolpathJobInput::LayerBucket（由 ProcessModel 派生，不再手写搬运）
            ↓
         Cam::compile
```

即：`LayerBucket` 会退化为 `ProcessModel` 的一份**编译快照**，字段语义以 `ProcessModel` 为准（割缝尤其注意：模型的权威表达是"单边 + 符号"，`KerfParams` 的"总宽 + side 枚举"要靠第 5.1 节的显式换算函数对接）。

当前事实（阅读本文时请以代码复核）：
- `CAM/CAM/Include/CAM/ToolpathJobInput.h` 不 include `ProcessModel.h`，只依赖 `Ut/Vec.h` 与标准库——`Cam::compile` 仍只读 `ToolpathJobInput`。
- `CamBridge::buildJobInput()` 仍是手写字段搬运，只覆盖割缝与引线。
- 生产链 `MotionPlanCompiler` 读的是 `Eg::ProcessParams`（经 `ToolpathJobSpec`），尚未改成读 `ProcessModel`。
