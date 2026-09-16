# LOD 多级细节：跨层设计

> 定位：解决《百万级矢量剔除》§14.7 遗留的 zoom out 全图 25\~34ms。
> 这是**应用层主导**的改动——几何离散化本就在应用层，LOD 是离散化的精度策略，
> RenderX 基本不需要动。本文档把「缩放级别如何影响离散化、如何不破坏增量渲染」
> 这条跨层链路定义清楚。

***

## 0. 背景：zoom out 为什么慢

空间索引已经解决了「剔除」——局部视图只遍历可见子集。但 zoom out 全图时
**100 万条全部可见**，剔除失效，退回到 O(n) 线性遍历。

然而真正的问题不是「遍历 100 万条」，而是**这 100 万条图元在被冗余渲染**：

* 一个圆，无论屏幕上缩成 2 像素还是放大到 2000 像素，都用**固定的
  `kCircleSegments`** **段**离散化（[RenderSceneBuilder.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/RenderSceneBuilder.cpp#L549) 的 `tessellateCircleFixed`）。

* 全图时，屏幕上这个圆只有 2 像素，2 段就够；但它还在渲染 128 段。

* 100 万个圆 × 128 段 = 1.28 亿顶点，实际需要 100 万 × 2 段 = 200 万顶点。

**顶点量差 64 倍。** 这才是 zoom out 慢的根因——不是遍历慢，是**在渲染大量
用户看不见的顶点**。

***

## 1. 现状链路（已核实）

```
SyEntity（业务图元：圆/弧/椭圆/折线）
   │ gatherGeometry()
   ▼
ISceneGeometrySink::emitCircle/emitArc/emitEllipse（参数化原语，世界坐标 double）
   │
   ▼
RenderSceneBuilder（应用层，每窗口一个）
   │  实现 ISceneGeometrySink，调用 Eg::Tessellator::tessellate*Fixed()
   │  ── 离散化精度由固定段数常量决定（kCircleSegments / kArcMinSegments / ...）
   │  ── 段位哈希：内容没变的段跳过，实现增量
   ▼
RenderBridge::PersistentGeometryStore（槽位 + 块管理）
   ▼
RenderX：GeometryStore（常驻几何）+ DrawList（空间索引 + 合批）
```

关键事实：

1. **离散化在应用层**（[RenderSceneBuilder](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Include/Render/RenderSceneBuilder.h#L60-L62)），
   参数化原语（圆/弧/椭圆）的 tessellate 精度完全由它决定。
2. **离散化是固定段数**：`tessellateCircleFixed` / `tessellateArcFixed` /
   `tessellateEllipseFixed`，段数常量在 `Eg::tess::` 下
   （[RenderTypes.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/Render/RenderTypes.h#L281-L292)）。
3. **RenderSceneBuilder 拿不到缩放级别**：`emitCircle(center, radius, color)`
   只有几何参数，没有视图信息。缩放是视图状态，与场景装配解耦。

***

## 2. 核心矛盾

LOD 需要「根据缩放级别决定 tessellation 精度」，但：

| 问题                         | 现状                              |
| -------------------------- | ------------------------------- |
| 缩放级别是**视图状态**（随 zoom 连续变化） | 在 `RenderWidget` / `Camera2D` 里 |
| tessellation 在**场景装配层**    | 在 `RenderSceneBuilder` 里        |
| 两者完全解耦                     | 没有通道                            |

更棘手的是：**zoom 操作本身不改业务图元**。增量渲染靠「段位哈希」判断
「几何有没有变」，而 zoom 时业务图元没变，哈希不变——如果 LOD 处理不当，
要么「zoom 了但精度不变」（LOD 失效），要么「zoom 一次就全场景重 tessellate」
（比现状更糟，因为 tessellate 是 CPU 密集操作）。

***

## 3. 设计决策

### D1 LOD 的度量：屏幕空间误差，不是固定段数

圆/弧/椭圆的段数应由「曲线在屏幕上的投影大小」决定，目标是**屏幕弦高误差
≤ 1 像素**。

* 段数 = f(世界尺寸, 缩放比例)。世界尺寸在 emit 参数里（radius），缩放比例
  需要传进来。

* 与现状的「固定段数」对比：固定段数等价于「误差随缩放变化」——zoom in 时
  折线可见（误差大），zoom out 时浪费段数（误差过小）。LOD 把它翻正。

### D2 分级 + 滞后（hysteresis）

zoom 是连续高频的，不能每变一点就重 tessellate。做法：

* LOD 级别是**离散的**（如 0\~4 五级，按缩放比例的对数分档）。

* 只在**跨越级别阈值**时才触发重建。

* 相邻两级的段数差建议 ≥ 2 倍（如 128 / 64 / 32 / 16 / 8），保证跳级时有
  明显收益，也减少来回抖动（抖动在阈值附近来回跳）。

### D3 LOD 状态放 RenderSceneBuilder

`RenderSceneBuilder` 是「每窗口一个」（对应一个视口），天然持有该视口的缩放
状态。LOD 级别作为它的一个字段，`emit*` 时用它决定段数。

### D4 触发通道：视图层主动推

`RenderWidget`（或 `SceneRefreshCoordinator`）在 zoom 变化时计算 LOD 级别，
跨越阈值时调 `RenderSceneBuilder::setLODLevel(level)`。

### D5 代价管理：LOD 切换是批量重建，必须限流

一次 LOD 切换要重 tessellate 场景里**所有曲线图元**（圆/弧/椭圆）。若场景有
10 万个圆，一次切换就是 10 万次 tessellate，明显卡顿。必须：

* **只重建曲线类图元**（折线/点/文本不受 LOD 影响）。

* **按预算分批**：每帧只重建 N 个图元（如 2000），跨多帧完成一次 LOD 切换。
  切换期间旧精度仍可显示，用户几乎无感。

* 或**阈值滞后加大**：让切换本身更罕见。

***

## 4. 分层与接口

### 4.1 RenderX：不改

LOD 完全在应用层。RenderX 接收的始终是「离散化后的折线」，LOD 对它透明。
`GeometryStore` / `DrawList` / 空间索引全部无感。

这符合 RenderX 职责边界：「不做几何离散化」。LOD 是离散化的精度策略，自然
也归应用层。

### 4.2 离散化层：新增自适应段数

`Eg::Tessellator` 增加按「屏幕误差」求段数的函数（与现有 `*Fixed` 并列，或
给 `*Fixed` 加一个「精度」参数）：

```cpp
// 新增：按「世界尺寸 + 缩放比例」决定段数，保证屏幕弦高误差 <= 1px
int circleSegmentsForScreen(double worldRadius, double worldToScreenScale, double chordErrorPixels);
int arcSegmentsForScreen(double worldRadius, double worldToScreenScale, double sweepAngleRad, ...);
int ellipseSegmentsForScreen(double radiusX, double radiusY, double worldToScreenScale, ...);
```

现有 `*Fixed` 保留（其他调用方），`RenderSceneBuilder` 切到自适应版本。

> **已落地（第一步）**：纯函数加在 `Engine/Common/Include/Engine/Render/TessParams.h`，
> 命名 `circleSegmentsForScreen` / `arcSegmentsForScreen` / `ellipseSegmentsForScreen`。
> 单元测试 `TessParamsTest.*` 5/5 通过。

**弦高误差公式**（务必用这个，不要用「每段 N 像素」）：

```
段数 n = π · √( screenRadius / (2 · ε) )
其中 screenRadius = worldRadius × worldToScreenScale，ε = 目标弦高误差（像素）
```

> ⚠️ 旧 `Tessellator::computeLODSegments`（仅测试在用）用的是「每段 2 像素」
> = 周长/2，会把半径 100px 的圆算成 314 段——比固定 64 段还多，方向反了。
> 弦高误差标准才是 CAD 惯例：同样半径 100px、误差 1px 只需约 22 段。
> **不要复用旧公式。**

**缩放比例的来源**（关键事实，已核实）：正交相机下，
`世界单位 → 屏幕像素 = Camera2D::zoomX`（`scaleX × vpW/2 = zoomX`，
见 `Camera2D.cpp` 的 `computeViewMatrix`）。因此 `worldToScreenScale` 直接取
`Camera2D::zoomX`，无需额外换算。

### 4.3 装配层：RenderSceneBuilder 持有 LOD 级别

```cpp
class RenderSceneBuilder {
    // 视图层跨越 LOD 阈值时调用
    void setLODLevel(int level);
    int lodLevel() const;

    // emit* 内部：
    //   int segs = circleSegmentsForScale(radius, lodScale());
};
```

关键：**内容哈希要把 LOD 级别算进去**——否则「同一图元、不同精度」会哈希
相同而被错误跳过。即：LOD 级别变化时，曲线图元的哈希必须失效，触发重建。

### 4.4 视图层：缩放 → LOD 级别 → 通知

```cpp
// RenderWidget / SceneRefreshCoordinator 内：
int newLevel = lodLevelFromScale(currentScale);
if (newLevel != builder.lodLevel()) {
    builder.setLODLevel(newLevel);   // 触发曲线图元分批重建
}
```

### 4.5 数据层：ISceneGeometrySink 不改

`emit*` 的签名保持纯几何。精度是**渲染层内部的决策**，数据层（业务图元）
不该也不需关心「当前缩放级别下该用多少段」。这保持「数据层零渲染语义」的
既有边界。

### 4.3.1 SmartLine 细分输出 LineStrip 优化（2026-09-16）

文件：`Engine/2D/Src/Render/Tessellator.cpp` (`tessellateSmartLine`)

SVG 导入的复合曲线以 `SmartLine` 存储，每段原独立细分为 `GL_LINES`（成对顶点）。N 段产生 2N 个顶点，连接处顶点无复用。

**优化**：`tessellateSmartLine()` 改为输出 `LineStrip`，首段保留起点，后续段跳过重复连接点。N 段仅产生 N+1 个顶点（原 2N），顶点数减少 ~50%，显存带宽减半。

**收益**：复杂 SVG 渲染顶点大幅减少，GPU 提交负载降低。

***

## 5. 关键难点与对策

| 难点                      | 对策                                        |
| ----------------------- | ----------------------------------------- |
| zoom 不改业务图元，增量哈希判断不出    | 内容哈希纳入 LOD 级别（§4.3）；只对曲线图元                |
| LOD 切换重 tessellate 大量曲线 | 分批限流（§D5），跨帧完成，切换期间旧精度可显示                 |
| 阈值附近来回抖动                | 滞后区间：升级阈值 < 降级阈值，中间留死区                    |
| 屏幕弦高误差公式要准              | 用「缩放比例」而非「视口尺寸」，避免窗口 resize 误触发 LOD       |
| 相机相对精度                  | 段数计算用世界坐标，离散化仍走已有的 double 相机相对减法，不引入新精度问题 |

***

## 6. 分阶段实施计划

1. **离散化层**：`Eg::Tessellator` 新增 `*ForScale` 段数函数，配单元测试
   （验证「屏幕误差 ≤ 1px」）。
2. **装配层**：`RenderSceneBuilder` 持有 LOD 级别 + 哈希纳入级别；`emit*`
   切到自适应段数。曲线图元重建仍走现有 `upsertEntity`。
3. **视图层**：缩放 → LOD 级别 → `setLODLevel` 通知通道。
4. **代价管理**：分批限流重建（若第一步就卡顿明显）。
5. **验证**：复用百万级基准，测「LOD 切换后 zoom out 全图的每帧耗时」，
   预期从 25\~34ms 显著下降（顶点量降 1\~2 个数量级）。

***

## 7. 验收标准

* **正确性**：同一图元在各级 LOD 下视觉形状一致（屏幕误差 ≤ 1px）；现有
  2D 渲染测试全绿。

* **性能**：百万级场景 zoom out 全图，LOD 生效后单帧耗时应显著低于 25ms
  （目标量级 < 10ms，实测为准）。

* **无回归**：常规编辑（局部缩放/平移）的增量路径不受影响——非曲线图元
  哈希不变，不触发多余重建。

* **交互无感**：LOD 切换分批完成，不出现单帧卡顿尖峰。

***

## 8. 范围外

* 不做 3D LOD（3D 已有 `DisplayCache3D` 基于距离的 LOD，属另一条线，且当前
  无生产调用者）。

* 不做折线 Douglas-Peucker 抽稀：折线本身已是离散数据，zoom out 时其顶点量
  通常不构成瓶颈；若后续实测需要，再加。

* 不改 RenderX 公共 ABI。

