# 结论

当前 2D CAM **不是空壳，已经完成了一个可测试的 CAM 编译骨架，并且 M1 的多项算法已经写出来**，但还不能称为“完整可生产的 2D CAM”。

当前状态更准确地说是：

- **CAM 独立 DLL、输入输出契约、几何提取、割缝、引线、穿孔、功率曲线、共边、栅格扫描桥接、MotionPlan 发射器已经存在。**
- 但其中有一部分功能只是“代码存在”，另一部分是“测试能跑”，还有一部分没有真正接入完整生产链。
- 目前最严重的问题不是缺少某一个算法，而是：
  1. **生产链到底使用 `Cam::compile` 还是旧的 `MotionPlanCompiler`，需要统一。**
  2. **大量参数已经进入 ABI，但没有真正实现或没有下传到设备。**
  3. **缺少加工前检查、刀路仿真、时间估算、设备能力校验。**
  4. **当前多个算法存在边界条件和工艺安全问题，不能直接默认用于真机。**

我给出的总体评价：

| 领域 | 当前状态 | 评价 |
|---|---:|---|
| DLL / CMake / ABI 边界 | 基本完成 | 约 70% |
| 2D 几何提取 | 较完整 | 约 75% |
| 基本轮廓切割 | 可用 | 约 70% |
| 割缝补偿 | 已实现，但仍需增强 | 约 65% |
| 引线/引出线 | 已实现基础版 | 约 60% |
| 穿孔 | 已实现骨架，但存在设备语义风险 | 约 50% |
| 功率曲线 | 已实现基础版 | 约 60% |
| 拐角处理 | 只有减速，缺少圆角/过切 | 约 25% |
| 共边切割 | 有保守实现，能力较窄 | 约 45% |
| 栅格加工 | 有桥接，但仍有工艺与单位缺口 | 约 55% |
| 路径排序 | 当前只有 `sourceEntityId` 排序 | 约 15% |
| 加工前检查 | 基本缺失 | 约 5% |
| 刀路仿真 | 缺失 | 0% |
| 时间估算 | 缺失 | 0% |
| 生产链集成 | 需要确认和统一 | 高风险 |

---

# 一、当前已经完成的内容

## 1. CAM DLL 的模块边界是正确的

当前 CAM 的公共输入是 `Cam::ToolpathJobInput`，公共输出是 `Cam::CamPlan` 和 `Cam::CamResult`。

`ToolpathJobInput` 没有依赖 `Engine2D`、`SceneManager`、`AprEntity` 或 Hardware，这个方向是正确的。Engine2D 到 CAM 的转换集中在：

- `Main/Src/CAM/CamInputBridge.h`
- `Main/Src/CAM/CamInputBridge.cpp`

这符合“桥接层隔离 Engine2D 类型”的设计。

CAM 本身的 CMake 也保持了：

- PUBLIC：`Utility`
- PRIVATE：`Log`
- PRIVATE：`Clipper2`

没有把 Engine2D、Hardware、UI 类型暴露到 CAM 公共头中。

对应边界可以参考：

```1:29:CAM/CAM/Include/CAM/CAMExport.h
#ifndef CAM_EXPORT_H
#define CAM_EXPORT_H

/**
 * @file CAMExport.h
 * @brief CAM 模块 DLL 导出/导入宏与 ABI 标识。
 *
 * ABI 定位：C++ 内部 ABI（不是 C ABI）。
 *   - 公开头会暴露 std::vector / std::string 等 STL 容器，成员布局与 CRT 实现强相关；
 *   - 使用方必须与 CAM 使用同一工具链构建：同编译器、同 CRT、同 _ITERATOR_DEBUG_LEVEL、
 *     同运行时库选项，否则容器布局或堆归属不一致会直接破坏内存；
 */
```

### 评价

这一部分是正确方向，但需要注意：

- 这是**内部 C++ ABI**，不是外部插件 ABI。
- CAM DLL 不能给不同编译器、不同 CRT、不同 Debug/Release 配置使用。
- 不能让 Python、C#、客户插件直接传递 `CamPlan`。
- 将来如果需要跨进程或跨语言，必须另做 C ABI 或序列化协议，不能直接复用当前头文件。

---

## 2. 几何输入类型基本完善

当前支持：

- 直线
- 圆弧
- 圆
- 多边形
- 椭圆
- Bezier
- Spline
- NURBS
- SmartLine
- 文字轮廓
- 二维码轮廓
- Point 打点
- Raster 扫描线

`GeometryExtractor` 的分类方向比较好，尤其是没有把图片、二维码、填充区域简单地当成拾取用的矩形。

当前几何提取入口位于：

- `Engine/2D/Include/Engine2D/Cam/GeometryExtractor.h`
- `Engine/2D/Src/Cam/GeometryExtractor.cpp`

其中，曲线类最终被离散成 Line，圆弧仍然保留为 Arc。

```1:23:Engine/2D/Include/Engine2D/Cam/GeometryExtractor.h
#pragma once

/**
 * @file GeometryExtractor.h
 * @brief 图元 → 加工几何的**唯一**提取入口（CAM 层的 Extract 阶段）。
 *
 * 加工链路只认类别，不认图元类型。
 */
```

### 已有能力

| 图元 | 当前处理 |
|---|---|
| `LINE` | 直线轮廓 |
| `POLYGON` | 直线轮廓，支持 bulge 展开 |
| `ARC` | 保留 Arc |
| `CIRCLE` | 拆成两个半圆 |
| `ELLIPSE` | 离散为折线 |
| `BEZIER` | 离散为折线 |
| `SPLINE` | 离散为折线 |
| `NURBS` | 自适应离散 |
| `TEXT` | 转换成字形轮廓 |
| `QR_CODE` | 转换为模块轮廓 |
| `POINT` | 打点 |
| `IMAGE` | 交给 Raster |
| `FILL_REGION` | 目前只有边界，未生成填充刀路 |
| `BAR_CODE` | 尚未实现 |
| `GROUP` | 本身不加工，成员单独提取 |
| `MESH` | 不走 2D CAM |

### 需要继续完善

1. `BAR_CODE` 目前仍然是 `Unsupported`。
2. `FILL_REGION` 只能提取边界，不能真正生成填充路径。
3. 文字缺字虽然有 `missingGlyphs`，但没有看到它被转成 CAM 的最终错误或警告结果。
4. NURBS 达到点数上限时只是警告，不会将“弦高精度不达标”写入 `CamResult`。
5. 提取器没有统一检查：
   - NaN
   - Inf
   - 零半径圆弧
   - 圆心到端点距离与半径不一致
   - 段与段之间是否首尾连接
   - 自交
   - 极小轮廓

这些应在进入 CAM 编译前统一处理，而不能完全依赖上游。

---

# 二、当前已经实现但不完善的算法

## 1. 轮廓编译：基础功能可以，但还没有完整校验

主流程在：

- `CAM/CAM/Src/CamCore.cpp`

它已经具备：

1. 参数校验
2. 图层过滤
3. 按图层处理
4. Entity 排序
5. 割缝补偿
6. 共边处理
7. 引线/引出线
8. 功率曲线
9. 穿孔
10. 多遍展开
11. Rapid 段
12. 统计长度
13. 包围盒
14. 取消与进度

整体流程已经成形。

但是当前轮廓编译还存在以下问题。

### 1.1 `emitArcs` 参数没有真正生效

`ToolpathJobInput` 里有：

```cpp
bool emitArcs = true;
```

但是在 `CamCore.cpp` 中，输入的 Arc 会直接转成 `PlanPathSegment::Arc`。没有看到根据 `input.emitArcs` 将 Arc 离散成 Line 的逻辑。

这意味着：

- 用户关闭“输出圆弧”时，CAM 仍可能输出圆弧。
- 下游设备不支持 Arc 时会在发射阶段失败，或者设备错误处理。

### 完善步骤

新增内部函数：

```cpp
static std::vector<PlanPathSegment> normalizeSegmentsForMachine(
    const std::vector<PlanPathSegment>& source,
    bool emitArcs,
    double chordToleranceMm);
```

逻辑：

1. `emitArcs == true`
   - 保留 Arc。
2. `emitArcs == false`
   - 使用统一的弧长离散函数拆成 Line。
3. 对每个输出段重新计算长度。
4. 保留 `sourceEntityId` 和 `sourceLayerId`。
5. 在 `CamResult` 增加：
   - `arcCount`
   - `linearizedArcCount`
   - `linearizedSegmentCount`

建议文件：

- `CAM/CAM/Src/GeometryTransform/GeometryUtil.h`
- `CAM/CAM/Src/GeometryTransform/GeometryUtil.cpp`
- 或新增 `CAM/CAM/Src/GeometryTransform/SegmentNormalizer.{h,cpp}`

测试：

- Arc 输出开启时仍为 Arc。
- Arc 输出关闭时全部变成 Line。
- 离散后的最大弦高误差不超过 `chordToleranceMm`。
- 整圆、跨 0° 圆弧、顺时针圆弧均测试。

---

### 1.2 输入轮廓没有统一连续性校验

当前 `flattenContour()` 能处理部分连续性，但 `CamCore::compile()` 并没有在所有 Contour 进入工艺变换前做严格的：

- 前一段终点 = 后一段起点
- 闭合轮廓末点 = 首点
- 圆弧半径合法
- Arc 端点在圆上
- 段长度不为 NaN
- 轮廓至少有足够段数

因此存在一种风险：

```text
输入轮廓段 A 结束于 (10, 0)
输入轮廓段 B 开始于 (10.2, 0)
```

当前流程可能仍然把两段发给设备，设备会自行拉直或产生不可预期的运动。

### 完善步骤

新增：

```cpp
struct GeometryValidationIssue
{
    enum class Code : int32_t
    {
        EmptyContour,
        OpenContour,
        DisconnectedSegments,
        ZeroLengthSegment,
        InvalidArc,
        NonFiniteCoordinate,
        SelfIntersection,
        DegenerateArea,
    };

    Code code;
    uint64_t sourceEntityId;
    int32_t sourceLayerId;
    Ut::Vec2d location;
    double value;
};
```

新增内部接口：

```cpp
static bool validateContour(
    const Contour& contour,
    double toleranceMm,
    std::vector<GeometryValidationIssue>& issues);
```

建议位置：

- `CAM/CAM/Src/Validate/GeometryValidator.{h,cpp}`

当前 `Src/Validate/` 目录已经在规划中，应正式启用。

处理策略：

- `failOnUnmachinable = true`：阻止编译。
- `failOnUnmachinable = false`：跳过轮廓，但必须写入 `CamResult`。
- 不能只返回一个 `UnsupportedGeometry`，必须区分：
  - 开口
  - 断链
  - 自交
  - 无效圆弧
  - 非有限数值

---

### 1.3 `selectionOnly` 没有真正做 CAM 层校验

当前选择过滤主要在 `CamInputBridge.cpp` 完成。

但是如果出现：

```cpp
input.selectionOnly = true;
input.selectionCount = 0;
```

当前 `validateInput()` 不会明确返回“未选择对象”，而是最终可能得到 `EmptyJob`。

这会导致 UI 不能区分：

- 用户确实没有选择对象。
- 所有对象都被隐藏。
- 选择对象不支持加工。
- 选择对象几何为空。

### 完善步骤

新增错误码：

```cpp
SelectionEmpty = -1004,
```

并在 `validateInput()` 开头增加：

```cpp
if (input.selectionOnly && input.selectionCount <= 0)
{
    return makeResult(
        CamError::SelectionEmpty,
        "Selection-only mode requires at least one selected entity");
}
```

同时在桥接层统计：

- 请求选中数量
- 实际成功转换数量
- 被过滤数量
- 不可加工数量

---

## 2. 割缝补偿：已经有实现，但属于“基础可用版”

实现位置：

- `CAM/CAM/Src/GeometryTransform/KerfCompensator.cpp`
- `CAM/CAM/Src/GeometryTransform/KerfCompensator.h`

当前已经使用 Clipper2，支持：

- Outside
- Inside
- Center
- 闭合轮廓
- 开口轮廓跳过
- 偏移后塌陷
- 偏移后裂成多个环
- 自交归一化
- 尖角 miter 限制

这是目前 CAM 中较成熟的一块。

### 主要问题

#### 2.1 Arc 会被全部离散成 Line

`KerfCompensator` 先调用：

```cpp
Geom::flattenContour(...)
```

再使用 Clipper2 偏置，因此补偿后的圆弧会变成折线。

这会带来：

- 圆孔变成多边形。
- 大圆弧段数变多。
- 加工路径变长。
- 高速切割时拐角数量增加。
- 后续功率曲线和拐角处理误判。

### 完善方案

分两阶段：

#### 第一阶段：保持现有实现，但明确记录精度

增加：

```cpp
int32_t kerfLinearizedArcCount;
double kerfMaxDeviationMm;
```

每条补偿轮廓记录：

- 原始 Arc 数量
- 离散后的 Line 数量
- 最大估计误差

#### 第二阶段：恢复圆弧

对于只有：

- 直线
- 圆弧
- 简单闭合轮廓

的情况，可以实现：

1. 直线平移。
2. 圆弧半径增加或减少补偿量。
3. 相邻偏移线求交。
4. 相邻偏移弧与偏移线求交。
5. 尖角根据 JoinType 生成：
   - Miter
   - Round
   - Bevel

复杂曲线仍然使用 Clipper2 离散补偿。

建议不要直接重写 `KerfCompensator`，而是增加：

- `AnalyticKerfCompensator`
- `PolygonKerfCompensator`

由 `KerfCompensator::apply()` 根据轮廓类型选择。

---

#### 2.2 `side` 没有完整验证

当前 `KerfParams::side` 只在实现中按：

- 0：Outside
- 1：Inside
- 2：Center

处理。

如果传入 3、-1 等非法值，当前逻辑可能进入 `else`，被当作 Inside 使用。

### 修复步骤

在参数校验中加入：

```cpp
if (layer.kerf.side < 0 || layer.kerf.side > 2)
{
    return makeResult(
        CamError::InvalidParams,
        "Kerf side must be Outside, Inside, or Center");
}
```

同时校验：

- `widthMm` 必须有限。
- `arcToleranceMm` 必须大于 0。
- `widthMm` 不能超过轮廓最小特征尺寸，除非允许自动塌陷。
- `widthMm` 过大时应给出 Warning，而不是只在最后返回全部塌陷。

---

#### 2.3 割缝补偿后的 Hole 标记需要重新确认

当前输出轮廓尝试保持源轮廓的绕向，并保留 `contour.hole`。

但是当一个轮廓偏置后：

- 裂成多个轮廓
- 与其他轮廓发生嵌套关系变化
- 产生新的岛屿

仅保留原始 `hole` 可能不够。

### 完善步骤

割缝补偿完成后，在**同一图层全部轮廓生成完毕后**重新做一次：

1. 轮廓包围盒筛选。
2. 点包含关系。
3. 计算嵌套深度。
4. 奇数层为 Hole，偶数层为 Outer。
5. 对每个补偿输出轮廓重新设置 `hole`。

不要只在 `CamInputBridge` 里做一次。因为补偿改变了几何关系。

---

## 3. 引线/引出线：功能已经写出，但存在明显参数问题

实现位置：

- `CAM/CAM/Src/GeometryTransform/LeadInOut.cpp`
- `CAM/CAM/Src/GeometryTransform/LeadInOut.h`

当前支持：

- 切线引线
- 法向引线
- 带角度直线引线
- 圆弧引线
- 引出线
- 废料侧判断
- 镜像方向兜底
- 孔洞与外轮廓不同方向
- 与割缝后的轮廓配合

测试覆盖相对较好。

### 3.1 引出线复用了引入线的圆弧参数

在引出线逻辑中，圆弧半径和角度仍然读取：

```cpp
params.leadInArcRadiusMm
params.leadInArcAngleDeg
```

而不是独立的：

```cpp
leadOutArcRadiusMm
leadOutArcAngleDeg
```

这会导致：

- 用户设置了独立的引出线长度，但圆弧半径仍受引入线参数影响。
- 引入线和引出线不能独立配置。
- 引出线圆弧长度可能与界面显示不一致。

### 修复步骤

在 `LeadInOutParams` 末尾追加：

```cpp
double leadOutArcRadiusMm = 0.0;
double leadOutArcAngleDeg = 90.0;
```

必须追加在结构体尾部，因为当前是 C++ ABI。

然后：

- `LeadInOut.cpp` 引出线使用 `leadOutArcRadiusMm`
- 引出线使用 `leadOutArcAngleDeg`
- 加入 ABI 版本变化
- 添加独立测试

测试：

- 引入线半径 5，引出线半径 10。
- 两段输出必须不同。
- 只有引出线开启时，引入线参数不得影响结果。

---

### 3.2 `rampAngleDeg` 没有实现

`LeadInOutParams` 中有：

```cpp
double rampAngleDeg = 0.0;
```

但当前 `LeadInOut.cpp` 没有实现 Z 轴坡入或其它坡入逻辑。

如果 2D CAM 暂时不支持 Z，应当：

- 不要保留一个看似有效但实际无效的参数。
- 或者明确写入 `CamResult` 警告。

建议：

```cpp
struct CamWarning
{
    WarningCode code;
    char text[256];
};
```

至少在编译诊断中报告：

```text
rampAngleDeg is ignored because 2D CAM has no Z-axis toolpath
```

如果未来要做 2.5D，需要在 `PlanPathSegment` 中增加 Z 参数或单独的高度层，不应继续假装只靠 2D XY 段可以表达。

---

### 3.3 引线没有完整避让检查

当前主要检查引线点是否位于废料侧，但没有检查：

- 引线段是否穿过轮廓其他边。
- 引线是否与相邻轮廓相交。
- 引线是否穿过孔洞。
- 引出线是否碰撞相邻零件。
- 圆弧中点之外的整个圆弧是否始终位于废料侧。

### 完善顺序

1. 先检测候选引线端点。
2. 再检测整条引线与原轮廓边界的交点。
3. 再检测与同图层其他加工轮廓的交点。
4. 圆弧引线需要按弦高容差离散后检查每一小段。
5. 失败时：
   - 不生成引线。
   - 写入 `leadInOutSkippedContourCount`。
   - 写入 `unmachinableDetails` 或 warning details。

---

## 4. 穿孔：实现存在，但是真机语义需要重点修正

实现位置：

- `CAM/CAM/Src/Process/PiercePlanner.cpp`
- `CAM/CAM/Src/Process/PiercePlanner.h`

当前支持：

- 穿孔延时
- 多次延时
- 稳定延时
- 穿孔功率
- 穿孔速度
- 穿孔点偏移
- 预穿孔
- 穿孔点记录

### 4.1 最大问题：穿孔功率没有作用于穿孔 Dwell

当前穿孔逻辑是：

1. 先插入 Dwell。
2. 再把 `piercePowerPercent` 设置到后面的第一条切削段。
3. 发射器在遇到 Dwell 时不会重新设置穿孔功率。

也就是说，穿孔等待期间使用的很可能还是图层当前功率，而不是 `piercePowerPercent`。

这在激光设备上很危险，因为“穿孔延时”和“稳定延时”通常要求特定功率。

### 正确做法

当前 `PlanPathSegment` 的 Dwell 中只有：

```cpp
int64_t dwellUs;
```

需要在尾部追加或增加专用字段：

```cpp
double dwellPowerPercent = -1.0;
double dwellFrequencyHz = -1.0;
double dwellPulseWidthUs = -1.0;
bool dwellLaserOn = true;
```

或者更清晰地扩展 `PlanPathSegment`：

```cpp
struct PierceEvent
{
    int64_t durationUs = 0;
    double powerPercent = 0.0;
    double frequencyHz = 0.0;
    double pulseWidthUs = 0.0;
};
```

但由于 `PlanPathSegment` 已经是公开 ABI，推荐只在尾部追加字段。

然后修改 `CamPlanEmitter.cpp`：

```text
Dwell 开始前：
    设置穿孔功率
    设置穿孔频率
    设置脉宽
    LaserOn
    Dwell
    LaserOff 或恢复上一状态
```

具体是否需要 `LaserOn/LaserOff`，要依据 Hardware `MotionPlan` 的设备语义确认。

---

### 4.2 `pierceCount` 实际上只是重复 Dwell

当前 `pierceCount` 会重复插入多个 `Dwell`，但没有真正表达：

```text
LaserOn → Dwell → LaserOff → 再次 LaserOn → Dwell
```

如果设备要求多脉冲穿孔，这样的语义不完整。

### 完善步骤

在 `PlanPathSegment` 或 `MotionPlan` 中定义明确的穿孔事件：

- `PierceOn`
- `PierceDwell`
- `PierceOff`
- `PierceRepeat`

如果不想新增硬件 opcode，可以在发射器中用已有指令组合：

```text
SetLaserParams(pierce)
LaserOn
Dwell
LaserOff
```

然后 `pierceCount` 每次生成一组，而不是简单复制 Dwell。

---

### 4.3 穿孔偏移可能把穿孔点放到轮廓外过远

当前只检查：

- closed
- 无 lead-in
- `pierceOffsetMm > 0`

但是没有检查偏移后的穿孔点是否：

- 仍处于废料侧。
- 穿过另一个轮廓。
- 超出机器范围。
- 与相邻轮廓相撞。

需要复用引线的碰撞检查，不要只依赖法向。

---

## 5. 功率曲线：基础插值可用，但拐角和物理模型不完整

实现位置：

- `CAM/CAM/Src/Process/PowerCurvePlanner.cpp`
- `CAM/CAM/Src/Process/PowerCurvePlanner.h`

当前支持：

- 速度排序
- 重复速度覆盖
- 非法点过滤
- 线性插值
- 端点钳制
- 起刀过渡
- 收刀过渡
- 拐角减速策略

### 5.1 `CornerParams.strategy == 1` 圆角没有实现

当前只实际处理：

```cpp
corner.strategy == 2
```

也就是 slowdown。

但是参数中明确存在：

```cpp
strategy = 1
radiusMm
```

当前没有插入圆弧，也没有修改几何。

### 完善步骤

新增：

- `CornerPlanner.h`
- `CornerPlanner.cpp`

输入：

```cpp
const std::vector<PlanPathSegment>& segments;
const CornerParams& params;
```

输出：

```cpp
std::vector<PlanPathSegment>
```

对每个折线转角：

1. 计算前后切向。
2. 计算转角。
3. 计算圆角切入距离。
4. 如果相邻段太短，缩小圆角或跳过。
5. 插入：
   - 截短后的前段
   - Arc
   - 截短后的后段
6. 更新首尾连续性。
7. 重新计算功率曲线。

注意顺序：

```text
割缝
→ 共边
→ 圆角
→ 引线
→ 功率曲线
→ 穿孔
→ 排序
→ 发射
```

如果引线依赖原始首段切线，圆角应在引线之前还是之后，需要固定约定。建议：

- 引线先确定加工起点。
- 轮廓内部角点圆角随后处理。
- 穿孔最后插入。

---

### 5.2 `CornerParams.overcutMm` 没有实现

`overcutMm` 目前只是参数，没有真正追加过切。

需要明确它的语义：

- 是在尖角处继续沿当前切线？
- 是沿角平分线？
- 是沿原轮廓方向超过终点？
- 只对闭合轮廓有效，还是开口轮廓也有效？

建议定义为：

```text
在闭合轮廓末端，沿首段方向继续切削 overcutMm，
用于闭合接缝处完全断开。
```

实现时需要：

1. 找到最后一段和第一段。
2. 判断两者是否可连续。
3. 沿最后段切向延长。
4. 检查延长方向是否进入成品。
5. 只在废料侧安全时生成。
6. 记录 `overcutAppliedCount`。

---

### 5.3 功率曲线没有考虑设备动态功率能力

当前 CAM 默认可以为每段设置功率和速度，但设备可能不支持：

- 动态功率
- 高频率功率切换
- 每段速度变化
- Arc 段动态功率
- 激光连续模式下的功率更新

必须增加设备能力校验，位置不应放 CAM DLL，而应放：

- `Main/Src/Hardware/PreflightChecker`
- 或设备能力适配层

CAM 只输出工艺计划，Main 在发送前检查：

```text
CamPlan 包含动态功率
设备 supportsDynamicPower == false
→ 阻止加工或允许用户确认降级
```

不要在 CAM 中直接引用 Hardware，保持依赖方向。

---

## 6. 共边切割：目前是保守的精确匹配实现

实现位置：

- `CAM/CAM/Src/GeometryTransform/CommonEdgePlanner.cpp`
- `CAM/CAM/Src/GeometryTransform/CommonEdgePlanner.h`

当前只支持：

- 反向完全重合的直线段
- 反向完全重合的圆弧段
- 前面的轮廓删边
- 后面的轮廓保留
- 三方冲突拒绝
- 非连续组拒绝
- 部分重叠只计数不处理

这是一个“宁可不共边，也不误删”的安全版本。

### 缺失能力

1. 部分重叠直线：
   - A：0–100
   - B：50–150
2. 同向但实际同一条边。
3. 圆弧只部分重叠。
4. 一个轮廓包含多个共享边时的拓扑重组。
5. 割缝补偿后共边匹配。
6. 共边后每个零件的尺寸和割缝补偿语义。
7. 共边与微连接的冲突。
8. 共边与引线的冲突。

### 推荐完善顺序

#### 第一步：建立共享边标准化结构

```cpp
struct SharedEdge
{
    enum class Type : int32_t
    {
        Line,
        Arc
    };

    Type type;
    Ut::Vec2d start;
    Ut::Vec2d end;
    Ut::Vec2d center;
    double radius;
    double overlapStart;
    double overlapEnd;
    size_t contourA;
    size_t contourB;
};
```

#### 第二步：支持部分直线重叠

1. 判断共线。
2. 投影到一维参数。
3. 求交集区间。
4. 若重叠长度大于容差：
   - 把原段切成多个子段。
   - 只删除重叠区间。
   - 保留非重叠部分。

#### 第三步：解决补偿顺序

建议共边的正确生产顺序应该是：

```text
原始轮廓归一化
→ 共享边检测
→ 共享边拓扑决定
→ 每个输出切割边统一做一次割缝补偿
→ 引线/穿孔
```

当前实现是先割缝，再共边。割缝后两条共享边通常已经不再重合，因此共边收益会大幅降低。

但共边和割缝的物理语义复杂，正式实现前必须明确：

- 共享边是否只补偿一次？
- 两侧尺寸如何保证？
- 外轮廓和孔洞能否共边？
- 共边后边界归属是哪一个零件？

---

# 三、当前明确缺失的功能

## 1. 路径排序：目前没有最近邻优化

当前 `CamCore.cpp` 使用：

```text
按 sourceEntityId 升序
```

这不是 CAM 生产级排序。

缺失：

- 最近邻
- 起点可配置
- 轮廓方向翻转
- 2-opt
- 孔先于外轮廓
- 图层优先级
- 热量分散
- 避免相邻高热区域连续加工

### 推荐实现

新增：

- `CAM/CAM/Src/Order/ContourSorter.{h,cpp}`
- `CAM/CAM/Src/Order/TravelOptimizer.{h,cpp}`
- `CAM/CAM/Src/Order/HoleFirstResolver.{h,cpp}`

排序规则：

```text
图层顺序
→ 同层中按包含深度由内到外
→ 同一嵌套层按最近邻
→ 尝试轮廓反向
→ 局部 2-opt
→ 共边链保持拓扑顺序
```

排序不能只按距离，否则会出现先切外轮廓、后切孔洞，导致零件掉落。

输入参数建议新增到 `ToolpathJobInput` 末尾：

```cpp
bool optimizeOrder = false;
Ut::Vec2d startPosition;
bool allowContourReverse = true;
bool holeFirst = true;
```

测试指标：

- 关闭优化时输出与旧版本一致。
- 开启优化时空移总长下降。
- 带孔零件始终先孔后外。
- 轮廓方向翻转后几何不变。
- 共边链顺序不被破坏。

---

## 2. 填充加工完全缺失

当前 `EntitySnapshot::GeometryKind::Fillable` 仍然在 `CamCore.cpp` 被当作不可加工内容处理。

虽然 `FillRegion` 中有：

```cpp
std::vector<Contour> boundaries;
double scanAngleDeg;
double lineSpacing;
```

但没有真正生成：

- 平行线填充
- Zigzag 填充
- 单向填充
- 交叉填充
- 等距填充
- 轮廓填充
- 孔洞避让
- 端点延伸
- overscan
- 填充边界裁剪

### 需要新增

建议：

- `CAM/CAM/Src/Fill/FillPlanner.{h,cpp}`
- `CAM/CAM/Src/Fill/HatchGenerator.{h,cpp}`
- `CAM/CAM/Src/Fill/ScanlineClipper.{h,cpp}`

处理流程：

```text
FillRegion.boundaries
→ 归一化闭合轮廓
→ 计算外环/孔洞
→ 按 scanAngleDeg 旋转到局部坐标
→ 生成等距扫描线
→ 与外环求交
→ 删除孔洞内部段
→ 按蛇形方向排序
→ 添加 overscan
→ 转成 PlanPathSegment
→ 应用功率/速度/遍次
```

必须复用 Clipper2，不要重新实现复杂布尔求交。

填充功能完成后，`CamError::UnsupportedGeometry` 中的 Fillable 才可以移除。

---

## 3. Raster 生产链仍需要进一步统一

当前 Raster 有两条相关路径：

- `Main/Src/CAM/CamInputBridge.cpp`
- `Main/Src/CAM/RasterScanlineBridge.cpp`

当前已经能把图片变成 `ScanlineSegment`，CAM 再把扫描线变成 Plan 段。

这部分已经比单纯“把图片当矩形”正确很多，但仍需完善。

### 3.1 亮暗极性需要统一

当前灰度、透明度、功率映射必须统一定义：

```text
亮色是否加工？
暗色是否加工？
透明是否加工？
```

特别是 RGBA 合成到白色时，如果后续规则是“亮度越高功率越大”，透明区域反而可能得到高功率。

必须明确：

```cpp
enum class RasterPolarity : int32_t
{
    DarkEngrave = 0,
    LightEngrave = 1
};
```

加入 `ToolpathJobInput` 或 Raster 参数中。

### 3.2 DPI/像素尺寸需要统一

不要同时存在：

- DPI
- pixelPitchMm
- image width/height
- 96 DPI fallback

建议最终统一为：

```text
CAM 只接收毫米坐标。
图片解码阶段必须得到 pixelPitchMmX/pixelPitchMmY。
无法得到物理尺寸时，必须让用户确认，不允许静默使用 96 DPI。
```

需要检查：

- PNG `pHYs`
- JPEG EXIF/JFIF
- TIFF Resolution
- BMP PelsPerMeter

建议落点：

- `FileIO/FileIO/Include/FileIO/ImageMetadata.h`
- `FileIO/FileIO/Src/ImageMetadata.cpp`
- `Main/Src/CAM/CamInputBridge.cpp`

### 3.3 Raster 多遍语义需要固定

当前 Raster 的 `passIndex` 已经写入扫描线，CAM 又有图层 `passes`。

必须明确：

- Raster 自身扫描线 pass 是否已经展开。
- `ProcessParams::passes` 是否只对矢量生效。
- 不能出现 Raster 被重复乘遍数。

推荐统一：

```text
Vector：由 CAM 根据 layer.params.passes 展开。
Raster：由 Raster 生成阶段完整展开，CAM 不再重复展开。
```

并把规则写进 `ToolpathJobInput.h` 注释和测试。

---

## 4. 加工前检查完全缺失

当前 `Cam::compile()` 的检查主要是：

- 参数是否合法
- 是否有实体
- 是否有不支持实体
- 弦高容差是否合法

缺少生产必须的检查：

- 未闭合轮廓
- 轮廓断链
- 自交
- 零长度段
- 极小孔
- 极小线段
- 引线碰撞
- 穿孔点碰撞
- 超机器行程
- 设备不支持 Arc
- 设备不支持动态功率
- 设备不支持 Dwell
- 空移穿越危险区域
- 轮廓重叠
- 同一实体重复加工
- 机器坐标变换后越界

### 推荐落点

因为 CAM 不应该依赖 Hardware 和 MachineProfile，建议分两层：

#### CAM 内部几何检查

新增：

- `CAM/CAM/Src/Validate/GeometryValidator.{h,cpp}`
- `CAM/CAM/Src/Validate/PlanValidator.{h,cpp}`

检查：

- 几何有效性
- 轮廓闭合
- 段连续性
- 自交
- 重叠
- 轮廓退化

#### Main/Hardware 侧设备检查

新增：

- `Main/Src/Hardware/PreflightChecker.{h,cpp}`

检查：

- 机器行程
- 机器能力
- 动态功率
- Arc 能力
- 最大速度
- 最小孔径
- 机器坐标变换
- 设备安全状态

`CamResult` 只承载 CAM 侧结果，设备相关问题不要硬塞进 CAM DLL。

---

## 5. 刀路仿真缺失

当前已经有 `CamPlan`，也有 `MotionPlan`，因此缺仿真并不是因为缺少数据结构，而是缺消费端。

推荐新增到 Main：

- `Main/Src/Hardware/ToolpathPreviewModel.{h,cpp}`
- `Main/Src/UI/Workbench/ToolpathPreviewDialog.{h,cpp}`

功能：

- 显示切削路径
- 显示 Rapid
- 显示穿孔点
- 显示图层
- 显示当前执行段
- 播放/暂停/停止
- 显示弧段
- 显示机器坐标
- 显示包围盒
- 显示警告位置

不要把 Qt UI 放入 CAM DLL。

仿真输入建议使用：

```text
CamPlan：几何和工艺预览
MotionPlanView：设备指令级预览
```

两者用途不同：

- `CamPlan`：适合 CAM 调试和几何预览。
- `MotionPlanView`：适合最终设备行为预览。

---

## 6. 时间估算缺失

`CamPlan` 中有 `estimatedSeconds`，但当前没有实际计算。

不要简单使用：

```text
长度 / 速度
```

至少需要计算：

1. Cutting 线段时间。
2. Arc 长度时间。
3. Rapid 时间。
4. Dwell 时间。
5. 穿孔时间。
6. 多遍实际时间。
7. 加减速影响。
8. 每次速度切换耗时。
9. 激光开关延时。
10. 设备特定的缓冲与插补限制。

建议估算放在 Hardware，因为最终输入是 `MotionPlanView`：

- `Hardware/Hardware/Include/Hardware/Device/JobEstimator.h`
- `Hardware/Hardware/Src/Device/JobEstimator.cpp`

接口可以是：

```cpp
struct JobEstimate
{
    double totalSeconds = 0.0;
    double cuttingSeconds = 0.0;
    double rapidSeconds = 0.0;
    double dwellSeconds = 0.0;
    double cuttingLengthMm = 0.0;
    double rapidLengthMm = 0.0;
    int32_t pierceCount = 0;
    bool approximate = true;
};
```

注意：

- 不要重新启用已经标记 deprecated 的 `CamPlan::estimatedSeconds` 作为新的真值。
- 可以计算，但应该由新 `JobEstimate` 承载。
- 只有一个时间估算实现，CAM、Raster、UI 都调用同一个估算器。

---

# 四、当前 DLL / ABI 需要重点修正的问题

## 1. 当前 STL 跨 DLL 方案只能内部使用

以下公开类型都包含 STL：

- `std::vector`
- `std::string`

例如：

```cpp
struct CAM_API CamResult
{
    bool ok = false;
    CamError error = CamError::Ok;
    char errorText[256] = {};
    std::vector<std::string> unmachinableDetails;
};
```

这是可以接受的前提：

- CAM DLL 和 Main 使用同一个 MSVC 工具链。
- 同一个 CRT。
- 同一个 `_ITERATOR_DEBUG_LEVEL`。
- 同一 Debug/Release ABI。
- 不允许客户自行编译插件直接使用。

建议在启动时不仅打印 `sizeof`，还增加：

```cpp
struct CamAbiInfo
{
    int32_t version;
    int32_t compilerFamily;
    int32_t compilerVersion;
    int32_t runtimeMode;
    int32_t iteratorDebugLevel;
    size_t sizeofPathSegment;
    size_t sizeofCamResult;
};
```

如果宏无法可靠获取，就至少在 DLL 和宿主中输出：

- 编译配置
- `_ITERATOR_DEBUG_LEVEL`
- `_DLL`
- CAM ABI 版本
- `sizeof` 关键结构

---

## 2. 结构体字段只能尾部追加

当前代码注释已经规定只能尾部追加，这个规则必须严格执行。

需要追加字段的地方包括：

- `PlanPathSegment`
- `PlanContour`
- `PlanLayerResult`
- `CamPlan`
- `CamResult`
- `ProcessParams`
- `KerfParams`
- `LeadInOutParams`
- `PierceParams`
- `PowerCurve`
- `ToolpathJobInput`

不要：

- 改字段顺序。
- 修改枚举已有值。
- 删除旧字段。
- 把 `double` 改成 `float`。
- 把 `bool` 换成其他类型。
- 在中间插入字段。

---

## 3. `Progress` 的 volatile 不是严格线程安全

当前：

```cpp
volatile double fraction;
volatile int32_t cancelled;
```

`volatile` 不能保证跨线程原子性。

更安全的方向：

- `cancelled` 使用原子类型。
- `fraction` 通过回调或原子存储更新。
- 如果为了 ABI 不想修改结构，可以保留旧字段，同时新增内部线程安全包装器。

推荐：

```cpp
struct ProgressState
{
    std::atomic<bool> cancelled{false};
    std::atomic<double> fraction{0.0};
};
```

但不要直接随意把 `std::atomic` 加入已有公开 ABI 结构中，应该：

1. 增加新的 `ProgressContext`。
2. 保留旧 `Progress`。
3. 逐步迁移 Main 调用方。
4. 最后在 ABI 版本中切换。

---

## 4. 异常不能穿过 DLL 边界

当前 `compile()` 内部很多地方没有统一异常边界。

建议 `Cam::compile()` 入口使用最终保护：

```cpp
try
{
    // compile pipeline
}
catch (const std::bad_alloc&)
{
    // CamError::OutOfMemory
}
catch (...)
{
    // CamError::InternalError
}
```

同时增加错误码：

```cpp
InternalError = -9001,
OutOfMemory = -9002,
```

不能让 Clipper2 或 STL 异常直接穿越 CAM DLL 到 Main。

---

# 五、最关键的生产链问题

需要重点确认 `Cam::compile()` 是否已经被真正的加工入口调用。

当前目录中已经有：

- `CamInputBridge`
- `CamPlanEmitter`
- `CamPlanEmitterTests`

但从现有设计和文档状态看，很可能仍存在两条链：

```text
SceneManager
→ 旧 MotionPlanCompiler
→ MotionPlan
```

以及：

```text
SceneManager
→ CamInputBridge
→ Cam::compile
→ CamPlanEmitter
→ MotionPlan
```

如果两条链同时存在，就会出现：



必须先确认“开始加工”按钮最终调用哪条链路。建议全文搜索：

- `MotionPlanCompiler::compile`
- `Cam::compile`
- `CamPlanEmitter::emit`
- `ProcessingJobService::startPlan`

然后沿着 UI 命令入口一路追到设备执行入口，画出唯一的调用链。

目标链路应当是：

```text
UI 命令
→ ProcessingJobService
→ CamBridge::buildJobInput()
→ Cam::compile()
→ CamPlanEmitter::emit()
→ MotionPlanBuilder::view()
→ 设备预检
→ DeviceHost::loadPlan()
→ DeviceHost::startPlan()
```

如果旧 `MotionPlanCompiler` 仍然是正式加工链路，CAM DLL 中的算法可能只是独立测试通过，用户实际加工时完全不会使用。

### 建议统一步骤

1. 在 Main 加一项端到端集成测试，覆盖 `SceneManager → MotionPlanView`。
2. 测试中同时使用：
   - 闭合轮廓
   - 圆弧
   - 割缝
   - 引线
   - 穿孔
   - Raster
3. 断言 `MotionPlanView` 中存在预期的：
   - `RapidTo`
   - `LineTo`
   - `ArcTo`
   - `Dwell`
   - `SetLaserParams`
4. 让 `ProcessingJobService` 只调用这条新链路。
5. 旧 `MotionPlanCompiler` 在新链路通过测试后标记 deprecated。
6. 确认没有调用者后，再删除旧路径，避免长期维护双轨。
7. 失败时必须确保不会调用 `DeviceHost::loadPlan()` 或 `startPlan()`。

---

# 六、建议的具体实施顺序

## P0：先保证调用链和安全，不扩展算法

这是继续开发前的第一优先级。

1. 确认正式加工入口是否调用 `Cam::compile()`。
2. 若不是，切换 `ProcessingJobService` 到 `CamBridge + Cam::compile + CamPlanEmitter`。
3. 加端到端集成测试，确保最终 MotionPlan 中包含工艺变换结果。
4. 增加编译失败时不允许加载设备计划的测试。
5. 修复 `emitArcs` 没有生效的问题。
6. 修复穿孔期间功率没有正确下发的问题。
7. 增加 `selectionOnly && selectionCount == 0` 校验。
8. 加入 `NaN/Inf` 和几何连续性校验。

验收标准：

- 从 UI 发起加工，确认实际执行的是 CAM DLL 输出。
- 设置不同割缝、引线、功率参数，最终 MotionPlan 有对应变化。
- 故意输入无效轮廓时，设备不加载计划。
- 日志可以标识 CAM ABI 版本和编译配置。

---

## P1：先完善现有算法和错误报告

1. 引线与引出线参数独立。
2. 穿孔功率、频率、脉宽具有明确的 Dwell 语义。
3. 引线、引出线、穿孔点增加碰撞检测。
4. 割缝参数合法性完整验证。
5. 割缝后重新判定内外环。
6. 将功率曲线和拐角参数中未实现的字段标记为 unsupported/warning。
7. 将 NURBS 点数预算告警写入 `CamResult`。
8. 将文本缺字情况传递到用户可见的编译诊断。
9. 统一所有错误和 warning 的来源图元 ID、图层 ID、坐标。

建议新增诊断结构，而不是不断在 `CamResult` 里加零散字段：

```cpp
struct CamDiagnostic
{
    enum class Severity : int32_t
    {
        Info,
        Warning,
        Error
    };

    Severity severity;
    int32_t code;
    uint64_t sourceEntityId;
    int32_t sourceLayerId;
    Ut::Vec2d location;
    char message[256];
};
```

由于 CAM ABI 已经公开，新增字段应追加到相关结构体尾部，并递增 ABI 版本。

---

## P2：补齐基础生产 CAM 能力

推荐依次实现：

1. 内外环加工顺序。
2. 最近邻排序。
3. 轮廓方向优化。
4. 圆角处理。
5. 过切处理。
6. 填充区域扫描线。
7. 设备能力预检。
8. MotionPlan 时间估算。
9. 刀路预览与逐段仿真。

不要先做复杂的共边拓扑或热量优化。它们依赖基础轮廓合法性和排序能力，先实现反而会增加调试难度。

---

## P3：优化和高级工艺

在上述能力稳定之后，再考虑：

- 部分重叠共边
- 共边拓扑合并
- 微连接 / 桥接
- 热量分散排序
- 多区域排样后的加工顺序
- 自动起刀点优化
- 依据材料厚度的多层切割
- 速度/功率闭环优化
- 机器加减速模型
- 生产节拍估算
- 多作业队列

---

# 七、文件落点建议汇总

| 功能 | 建议位置 | 责任 |
|---|---|---|
| 几何校验 | `CAM/CAM/Src/Validate/GeometryValidator.{h,cpp}` | CAM 几何合法性 |
| 刀路校验 | `CAM/CAM/Src/Validate/PlanValidator.{h,cpp}` | CAM Plan 连续性、空计划 |
| 圆弧离散 | `CAM/CAM/Src/GeometryTransform/SegmentNormalizer.{h,cpp}` | `emitArcs=false` 时离散 |
| 最近邻排序 | `CAM/CAM/Src/Order/ContourSorter.{h,cpp}` | 轮廓排序 |
| 2-opt 优化 | `CAM/CAM/Src/Order/TravelOptimizer.{h,cpp}` | 缩短空移 |
| 孔先外后 | `CAM/CAM/Src/Order/HoleFirstResolver.{h,cpp}` | 拓扑加工顺序 |
| 圆角与过切 | `CAM/CAM/Src/GeometryTransform/CornerPlanner.{h,cpp}` | 拐角加工 |
| 填充扫描线 | `CAM/CAM/Src/Fill/FillPlanner.{h,cpp}` | `Fillable` 生成路径 |
| 机器预检 | `Main/Src/Hardware/PreflightChecker.{h,cpp}` | 行程和设备能力 |
| 时间估算 | `Hardware/Hardware/Include/Hardware/Device/JobEstimator.h`、对应 `Src` | MotionPlan 时间估算 |
| 走刀仿真 | `Main/Src/UI/Workbench/ToolpathPreviewModel.{h,cpp}` | 预览模型 |
| 走刀界面 | `Main/Src/UI/Workbench/ToolpathPreviewDialog.{h,cpp}` | UI 展示 |
| 端到端链路 | `Main/Src/CAM/Test/` 或 `Main` 集成测试目录 | 验证实际生产入口 |
| ABI 自检 | `CAM/CAM/Include/CAM/CAMExport.h`、`CamCore.cpp` | ABI 版本和布局诊断 |

职责边界：

- 几何变换与工艺编译在 `CAM`。
- 设备行程、设备能力、硬件限制在 `Main/Hardware`。
- UI 只消费预览模型，不直接依赖 CAM 内部算法类。
- `Engine2D` 负责将场景图元转换为 CAM 中性快照。
- 不要让 CAM 直接 include `SceneManager`、`LayerManager`、`AprEntity` 或 UI 头文件。

---

# 八、当前测试需要补充的场景

现有测试已经覆盖了部分：

- 割缝
- 引线/引出线
- 穿孔
- 功率曲线
- 共边
- CamCore
- CamPlanEmitter

建议至少补充以下测试。

## 几何输入

- `NaN` 和 `Inf` 坐标。
- 零半径圆弧。
- Arc 半径与端点不匹配。
- 不连续段。
- 自交闭合轮廓。
- 非闭合轮廓启用割缝。
- 极小孔洞。
- 只有重复点的轮廓。
- NURBS 触及离散点上限。
- 缺字文本。

## 加工顺序

- 带孔零件先加工内孔。
- 图层顺序按文档定义稳定。
- 最近邻减少空移距离。
- 允许反向时路径长度确实下降。
- 共边轮廓顺序不会被排序破坏。

## 工艺变换

- `emitArcs=false` 时 Arc 全部离散。
- 引出线参数独立于引入线参数。
- 穿孔功率实际出现在 MotionPlan 中。
- 多次穿孔生成完整的开关光序列。
- 引线碰撞时跳过并报告原因。
- 圆角插入后所有段保持连续。
- `overcutMm` 为零和非零时输出差异正确。

## Raster

- Gray8、RGB24、RGBA32。
- 透明像素与透明边缘。
- 横向与纵向像素间距不同。
- 旋转、斜切图像映射。
- 黑白反转模式。
- Raster pass 不重复展开。
- 扫描线读取中途失败时整个 Raster 图元被拒绝，而不是发出部分刀路。

## DLL 与链路

- `BUILD_CAM=ON/OFF` 都能正确配置。
- Debug/Release 导出宏正确。
- 动态库和宿主的 ABI 版本不一致时能拒绝或明确报错。
- 编译失败后不调用设备加载。
- 最终 `MotionPlanView` 与 CAM 工艺设置一致。
- CAM 调用可被取消，且取消后不保留部分计划。

---

# 最终建议

你目前的 CAM 代码已经具备**继续完善的架构基础**，不需要推倒重写。建议按这个次序继续：

1. **先确认并统一真实加工链路**，这是最重要的。
2. **修复 `emitArcs`、穿孔功率和选择校验等已存在但没有闭环的参数。**
3. **增加几何合法性校验和用户可见诊断。**
4. **完成内孔优先与最近邻排序。**
5. **实现 Fillable 填充刀路。**
6. **补设备预检、仿真和时间估算。**
7. 最后再扩展高级共边、热量优化等功能。

当前最需要避免的是：仅凭子模块单元测试通过，就认定实际加工链路已经完整。必须以“从 UI 启动后生成的最终 `MotionPlanView`”作为验收终点。