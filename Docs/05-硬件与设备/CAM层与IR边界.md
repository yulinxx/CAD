# CAM 层与 IR 边界

> 适用范围：`Engine/2D/Cam/`（加工几何）与 `Main/Src/Hardware/MotionPlanCompiler.*`（设备指令编译）。
> 本文档描述**为什么这样分层**以及**每层的硬约束**；具体函数签名以头文件注释为准，两者冲突时以代码为准并回来修文档。
> 建立日期：2026-10-22

---

## 0. 一句话

> **几何只从 `Eg::Cam::GeometryExtractor` 出，坐标只换算一次，能力降级只发生在编译器。**

---

## 1. 为什么要多这一层

原来的链路是「文档 → `MotionPlanCompiler` → `Hw::MotionPlan`」，中间没有加工几何这一层。
看起来少一层更简单，实际上把三件不同性质的知识挤进了同一个函数：

| 知识 | 例子 | 需要的最小上下文 |
|---|---|---|
| **几何** | 这条轮廓是闭合的还是开口的？孔洞在谁里面？ | 图元 + 空间关系 |
| **工艺** | 割缝补偿往外还是往内？孔洞先切还是外框先切？ | 轮廓集合 + 绕向 + 包含关系 |
| **设备** | 这张卡支持圆弧插补吗？坐标单位是 mm 还是 pulse？ | 设备能力位 |

三者挤在一起的后果是：每加一种工艺（填充、补偿、引线、排序）都要动那个函数，
而那个函数同时还负责设备降级 —— 于是「加工策略」和「设备适配」互相污染。

现在链路是：

```
SyEntity ──Extract──▶ Cam::Contour ──Build/Transform/Order──▶ Cam::Toolpath
                     （加工几何）                              （加工几何）
                                                                      │
                                                                      ▼
                                                              PlanEmit（编译器）
                                                                      │
                                                                      ▼
                                                        Hw::MotionPlan（设备指令）
                                                                      │
                                                                      ▼
                                                                 适配器 → 真机
```

`Cam::Toolpath` 就是 v1 缺的那一层。

---

## 2. 三层的职责边界

### 2.1 加工几何层（`Engine/2D/Cam/`）

**它回答**：「这些图元在机器上应该被烧成什么形状」。

| 职责 | 说明 |
|---|---|
| 提取 | `GeometryExtractor`：图元 → 轮廓（唯一入口） |
| 构建 | 轮廓 → 轮廓集（填充、扫描线、引线） |
| 变换 | 补偿、偏置、共边处理 |
| 排序 | 内孔先于外轮廓、最短空移、跳刀顺序 |
| 校验 | 自交、零长、超程、闭合性 |

**它不回答**：「设备能不能执行」「坐标在机器上是什么位置」。

硬约束：
- 纯 C++17，不依赖 Qt、不依赖 Hardware、不依赖 UI。
- 长度单位恒为 **mm**，角度恒为 **弧度**（与 HAL 一致）。
- 坐标系恒为**用户空间**（X 右、Y 上、左下原点）—— 机器空间的换算不在这里做。

### 2.2 指令编译层（`Main/Src/Hardware/MotionPlanCompiler.cpp`）

**它回答**：「这台机器怎么执行」。

| 职责 | 说明 |
|---|---|
| 能力降级 | 卡不支持圆弧 → 离散成直线（在这里做，不在适配器里做） |
| 坐标换算 | 用户空间 → 机器空间（`Eg::MachineFrame`） |
| 遍次展开 | pass 循环（当前仍在编译器里，见 §5 待收敛项） |
| 指令编排 | BeginLayer / SetLaserParams / RapidTo / LineTo / ArcTo / LaserOff … |

**它不认识几何**：输入是 `Cam::Contour`，不是 `SyEntity`。

### 2.3 设备适配层（`Hardware/Hardware/Src/Device/`）

**它回答**：「这个厂商的 SDK 怎么调用」。

- 唯一的职责是翻译 `Hw::MotionPlan` → 厂商 API。
- 不做几何降级、不做坐标换算、不做工艺判断。
- 翻译不了时**报错**，不静默丢弃指令。

---

## 3. 五条硬约束（写进 CI / 单测）

1. **几何唯一入口**
   加工链路的任何代码都不得直接调用 `Eg::Geo2DPath::decompose` / `toPolyline`。
   `Geo2DPath` 是**拾取用**几何，位图返回四个角点、二维码返回包围盒、色块返回三角剖分的每一条边 ——
   用它做加工会让图片被烧成矩形框。

2. **坐标换算唯一**
   `Eg::MachineFrame::toMachine` 只在 `MotionPlanCompiler` 里调用一次。
   适配器与作业服务都不换算。
   （旧 UI2D 链路在 `HardwareLaserService` 里换算过，新 HAL 链路不换算 ——
   两套语义并存，同一份文档走不同路径会烧到不同位置。）

3. **能力降级唯一**
   圆弧离散成直线发生在编译器（它知道 `chordToleranceMm`），不在适配器里。
   适配器只做「厂商 API 没有这个能力 → 报错」。

4. **失败必须显式**
   「这类图元暂时无法加工」必须走 `GeometryKind::Unsupported` + 原因，
   由 `ToolpathJobSpec::failOnUnmachinableEntity` 决定是**整份作业失败**（默认）还是跳过并报告。
   绝不静默产出空几何 —— 静默跳过在激光加工里等于废件。

5. **派生数据不落盘**
   `Cam::Toolpath` 是派生结果，不写进 `.sy`。文档里只存**意图**（工艺对象、图层输出开关、单位声明）。

---

## 4. 几何提取器：支持矩阵

`Eg::Cam::GeometryExtractor::kindOf(EType)` 是**一张纯函数表**，是「这类图元怎么加工」的契约。
加工链路只认类别，不认 `EType`。

| EType | 类别 | 产出 | 说明 |
|---|---|---|---|
| `LINE` | Contour | 折线 | 闭合与否取图元自身的 `bClosed` |
| `POLYGON` | Contour | 折线 | **不**沿用拾取路径的「无条件闭合」 |
| `CIRCLE` | Contour | 两段 180° 圆弧 | 整圆拆半：不少卡不接受起止点重合的单条圆弧 |
| `ARC` | Contour | 一段圆弧 | **带行进方向**（见 §5.1） |
| `ELLIPSE` | Contour | 折线 | 按弦高容差离散 |
| `BEZIER2` / `BEZIER` / `SPLINE` | Contour | 折线 | 自适应细分 |
| `NURBS` | Contour | 折线 | 均匀采样（已知局限：未按弦高容差自适应） |
| `SMARTLINE` | Contour | 合并子段 | 递归提取 |
| `TEXT` | Contour | 字形轮廓 | 走 `TextConverter` |
| `QR_CODE` | Contour | 模块轮廓 | 走 `QrCodeGeometry::buildContours`，**不是包围盒** |
| `POINT` | Point | 单点 | 原地打点 |
| `FILL_REGION` | Fillable | 边界环 | 真正的扫描线由填充阶段生成 |
| `IMAGE` | Raster | — | 光栅雕刻引擎未实现，**显式报不支持** |
| `BAR_CODE` | Unsupported | — | 条码模块几何未实现 |
| `GROUP` | Container | — | 本身无几何，成员独立提取 |
| `MESH` | Unsupported | — | 3D 走切片管线 |

新增 `EType` 时忘记归类 → `CamSupportMatrixTests` 直接失败。

---

## 5. 已知待收敛项

### 5.1 圆弧行进方向（已修正，需要回归验证）

`Eg::PathSegment`（几何算法用）的圆弧约定是「`dStartAngle → dEndAngle` 恒为 CCW 参数化」。
那是**几何算法**需要的约定（只关心弧占哪些点）。

加工链路多要一件事：**往哪边走**。

- 用户可以逆时针画弧（`dEndAngle < dStartAngle`）。
- 按几何约定理解，本该 90° 的小弧会变成 270° 的大弧。
- 真机走错边不是「差一点」，是**烧掉一整块料**。

v1 的编译代码写着「Geo2DPath 约定分解出的圆弧一律为 CCW 扫掠，因此这里恒传 true」——
那是**错的**。

现在：`Eg::Cam::PathSegment` 自带 `counterClockwise` 字段，提取器用
`Geo2DPrimitives::arcSweepAngle` 的**带符号**结果判定方向，编译器照传。

### 5.2 pass 展开目前在编译器里

按计划，pass 应在 `Cam::Toolpath` 层展开（因为「每遍用不同角度填充」要求每遍是独立的段序列）。
当前实现仍在编译器的 pass 循环里 —— 功能正确，但每遍的段序列无法独立优化。

**收敛动作**：把 pass 循环下移到 Cam 层，编译器只接受一条已经展开好的 `Toolpath`。

### 5.3 镜像后的绕向反转

`MachineFrame` 反射会反转圆弧绕向。编译器在发射 `ArcTo` 时按 `frame.isReflected()` 取反
`counterClockwise`。这条路径**尚无单测覆盖**，真机联调前必须补。

### 5.4 割缝补偿与填充阶段尚未实现

`Cam` 层目前只做到 Extract。Build / Transform / Order / Validate 四个阶段还没有代码。
补缝补偿时需要 `Contour::hole`（孔洞判定），它由包含关系解析填写 —— 提取阶段一律为 false。

### 5.5 光栅雕刻（位图）未实现

`IMAGE` 归为 `Raster` 并显式报不支持。真正的光栅引擎需要位图 → 灰度 → 抖动 → 扫描线，
与 `Engraving` 模块的位图能力有重叠，届时需要决定复用哪一份。

---

## 6. 相关文档

- [数据到硬件全链路.md](数据到硬件全链路.md)：从图元到真机的完整数据流，本文的上游。
- [硬件抽象层架构.md](硬件抽象层架构.md)：HAL 契约层与厂商适配，本文的下游。
- [../路径/刀路系统设计草案.md](../路径/刀路系统设计草案.md)：刀路系统的原始设计草案。
