# CAM 层架构规划

> 状态：规划阶段 | 起草日期：2026-11-03
> 前置文档：[激光加工软件功能缺口梳理.md](./激光加工软件功能缺口梳理.md)
> 目标：填补"几何处理→实际加工"之间的工艺编译层，对标 LightBurn / EZCAD 级别的 2D 切割/雕刻能力

---

## 0. 当前真实状态（基线）

### 0.1 已有的 CAM 相关资产

| 模块 | 位置 | 内容 | 局限 |
|------|------|------|------|
| 加工几何 POD | `Engine/2D/Include/Engine2D/Cam/ToolpathTypes.h` | `Eg::Cam::PathSegment`（Line/Arc）、`Contour`（含孔洞/绕向/sourceEntityId/sourceLayerId）、`Toolpath`（contours + processId + passIndex） | 类型完美是中性 POD，但只表达了"段"这一层，上层图元仍然耦合 Engine2D |
| 几何提取器 | `Engine/2D/Include/Engine2D/Cam/GeometryExtractor.h` | 图元→加工几何的**唯一入口**，返回 `ExtractResult{kind, contours, reason}`，支持矩阵 `kindOf(EType)` 防止遗漏 | 只做 Extract，不做任何工艺变换 |
| 运动计划 IR | `Hardware/Hardware/Include/Hardware/Device/MotionPlan.h` | `Hw::MotionCommand`（opcode + POD 字段）、`MotionPlanHeader`（planId/bounds/estimatedSeconds）、`MotionPlanView`（跨 DLL 只读视图）、`MotionPlanBuilder`（宿主侧构建器） | IR 设计完备，但**没有谁能完整地把 Toolpath 编译成 MotionPlan** |
| 现有编译器（薄壳） | `Main/Src/Hardware/MotionPlanCompiler.{h,cpp}` | 直接读 `Eg::SceneManager` + `LayerManager` → 分图层 → 调 `GeometryExtractor` → 发 `MotionPlanBuilder` | **直接 include Engine2D**；图层分桶是硬编码的；没有工艺变换（割缝补偿/引线/排序/穿孔/功率曲线）；Raster 类图元直接打回 Unsupported |
| 作业执行器 | `Main/Src/Hardware/ProcessingJobService.{h,cpp}` | 安全门控 → 调用 `MotionPlanCompiler` → `DeviceHost.loadPlan/startPlan` → 进度轮询 | 作业生命周期管理完整，但编译出来的计划没有工艺 |
| Engraving 算法资产 | `Engraving/Engraving/Include/`（15 个公开头） | LeadInOut、ToolpathOrder、PolygonOffset、LaserStrategy、ToolpathExporter、LayeredProcessing、MeshSlicer、HeightMap、VolumeProcessing | 各自独立，**没有被 MotionPlanCompiler 调用**，与 CAM 层未贯通 |
| 设备 HAL | `Hardware/Hardware/Include/Hardware/Device/` | `IMotionCard`、`IGalvoCard`、`ILaserSource`、`IIoModule`、`DeviceHost`、`SafetyManager` | HAL 接口完备，但真机适配器（雷赛/正运动/Scanlab）全为桩，只有 SimulatedDevice |

### 0.2 链路断点（当前端到端缺什么）

```
SyEntity / SceneManager
    ↓ GeometryExtractor（✅ 有）
Eg::Cam::Contour[] / Toolpath[]（✅ 有）
    ↓ 工艺变换（❌ 全空：割缝补偿、共边、引线、穿孔、功率曲线、排序、时间预估）
??? 
    ↓ 编译
Hw::MotionPlan（✅ 有 IR，但只有 MotionPlanCompiler 直接从 SceneManager 编译的薄壳路径）
    ↓ 
ProcessingJobService → DeviceHost（✅ 有）
    ↓
真机（❌ 真机适配器为桩）
```

---

## 1. 目录与归属

### 1.1 建议：独立顶层模块 `CAM/`

**方案**：在仓库根下新建 `CAM/CAM/`（参考 Raster/Raster/、Hardware/Hardware/ 的双层目录约定）。

**权衡**：

| 方案 | 优点 | 缺点 |
|------|------|------|
| **A. 独立顶层 `CAM/`** ✅ 推荐 | ① 依赖方向天然正确（CAM 不被任何人反向 include）；② 未来可能独立打包/子模块仓库，无需重构目录；③ 与 Raster/Vision/Nesting 并列，定位清晰 | 多一个顶层目录，需要根 CMakeLists 新增 add_subdirectory |
| B. 挂 `Engine/2D/Cam/` 下 | 少一个目录层级 | ① CAM 会依赖 Engraving、ProcessParams（已在 Engine/Common），Engine2D 反向依赖这些是 OK 的，但 CAM 还会被 UI3D 的 Relief 雕刻复用，挂在 Engine2D 下会让 UI3D 也 include Engine2D —— 假依赖；② 未来 CAM 要独立成子模块仓库时需要整体搬迁 |
| C. 挂 `Hardware/` 下 | 靠近 HAL | CAM 依赖 Engine2D/Engraving/ProcessParams，Hardware 不应反向依赖上层 |

### 1.2 CMake 接入

```cmake
# Config.cmake 模块开关（与 BUILD_RASTER / BUILD_VISION 同层级）
option(BUILD_CAM "Build CAM layer (toolpath planning, process compilation)" ON)
```

根 CMakeLists.txt 在 Config.cmake 之后加：
```cmake
if(BUILD_CAM)
    add_subdirectory(CAM)
endif()
```

CAM/CAM/CMakeLists.txt 采用与 Raster/Raster/ 相同的动态库写法（CAM 对外是 C++ 内部 ABI，不是 C ABI）：
```cmake
sanyi_add_shared_library(CAM
    TARGET          CAM
    DIRECTORY       CAM
    DESCRIPTION     "CAM layer: process compilation from toolpath geometry to device-independent motion plan"
    EXPORT_MACRO    CAM_EXPORTS
    PUBLIC_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
    PRIVATE_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/Src
    PUBLIC_LINK_LIBRARIES
        Log::Log                        # 日志
    PRIVATE_LINK_LIBRARIES
        Engraving::Engraving            # 引线/排序/偏移算法
        Engine2D::Engine2D              # 只用于 GeometryExtractor 依赖的类型（ProcessParams 等）
)

# 注意：CAM 的 PUBLIC_INCLUDE_DIRS 不能包含 Engine2D/Include
# 消费方不能通过 CAM 的 PUBLIC_INCLUDE_DIRS 间接看到 Engine2D
# Engine2D 只能是 PRIVATE_LINK_LIBRARIES
```

### 1.3 CheckDependencies 门禁扩展

`CMake/CheckDependencies.cmake` 已存在，需要新增一条规则：**CAM 的 INTERFACE_INCLUDE_DIRS 不得包含 Engine2D/Core、UICommon、RenderBridge、Hardware/Device（除 MotionPlan IR）**。规则应写死在 CheckDependencies 里，configure 阶段自动校验。

---

## 2. 分层与数据契约（低耦合核心）

### 2.1 整体分层

```
┌───────────────────────────────────────────────────────────┐
│  UI（Workbench / 工艺面板 / 菜单 / 走刀预览）                │
│  通过命令 + ProcessingService 调用，不持有 CAM 内部对象      │
└──────────────────────┬────────────────────────────────────┘
                       │ ToolpathJobSpec（参数快照）
                       │ + EntitySnapshot（中性几何快照）
                       ▼
┌───────────────────────────────────────────────────────────┐
│  桥接层（Main/Src/Hardware/ 或 Main/Src/CAM/）              │
│  ① SceneManager → EntitySnapshot（遍历 + GeometryExtractor）│
│  ② LayerManager → LayerProcessParams 映射                   │
│  ③ 不做坐标变换（CAM 全程用户坐标）                        │
│  职责：把 Engine2D/UI 的类型转换成 CAM 能接受的中性 C++ 值类型     │
└──────────────────────┬────────────────────────────────────┘
                       │ ToolpathJobInput（中性 C++ 值类型，C++ 内部 ABI）
                       ▼
┌───────────────────────────────────────────────────────────┐
│  CAM 层（独立顶层模块）                                      │
│  输入：ToolpathJobInput —— 只有中性 C++ 值类型，无 Engine2D/UI 类型 │
│  ① 几何预处理（割缝补偿、共边、引线、拐角、穿孔、功率曲线）     │
│  ② 排序与空移优化                                           │
│  ③ 验证（越界、干涉、空计划检测）                             │
│  ④ 时间预估                                                 │
│  ⑤ 输出：CamPlan（工具函数）或直接调 MotionPlanBuilder        │
└──────────────────────┬────────────────────────────────────┘
                       │ Hw::MotionPlanView（已经是中性 IR）
                       ▼
┌───────────────────────────────────────────────────────────┐
│  设备层（已有）                                              │
│  ProcessingJobService → DeviceHost → IMotionCard/IGalvoCard │
└───────────────────────────────────────────────────────────┘
```

### 2.2 输入契约：`ToolpathJobInput`（CAM 唯一输入）

CAM 不 include Engine2D/Core/SceneManager.h，不 include Engine2D/Interaction/LayerManager.h，不 include SyEntity.h。桥接层负责做转换。

```cpp
// CAM/CAM/Include/CAM/ToolpathJobInput.h

#pragma once
#include "CAM/CAMExport.h"
#include "Ut/Vec.h"          // 现有中性向量类型

#include <cstdint>
#include <vector>

namespace Cam
{
    // ---- 中性几何快照：桥接层从 SceneManager + GeometryExtractor 转换而来 ----

    /// 一条段（与 Eg::Cam::PathSegment 同语义，但 CAM 自己持有副本，不依赖 Engine2D 头）
    struct PathSegment
    {
        enum class Type : int32_t { Line = 0, Arc = 1 };
        Type type = Type::Line;
        Ut::Vec2d ptStart;
        Ut::Vec2d ptEnd;
        Ut::Vec2d ptCenter;       ///< Arc 用
        double dRadius = 0.0;     ///< Arc 用
        double dStartAngle = 0.0; ///< Arc 用
        double dEndAngle = 0.0;   ///< Arc 用
        bool counterClockwise = true;
        bool closed = false;
    };

    /// 一条轮廓（与 Eg::Cam::Contour 同语义）
    struct Contour
    {
        std::vector<PathSegment> segments;
        bool closed = false;
        bool hole = false;            ///< 后续 Transform 阶段填写
        uint64_t sourceEntityId = 0;  ///< 追溯：预览高亮、报错定位
        int32_t sourceLayerId = 0;
        double length = 0.0;
    };

    /// 一条填充区域（边界环 + 扫描参数）
    struct FillRegion
    {
        std::vector<Contour> boundaries;  ///< 外环（含孔洞）
        double scanAngleDeg = 0.0;
        double lineSpacing = 0.1;          ///< mm
    };

    /// 一个图元快照：桥接层把 SyEntity 转成这个
    struct EntitySnapshot
    {
        enum class GeometryKind : int32_t
        {
            Contour = 0,
            Point = 1,
            Fillable = 2,  ///< boundaries 里有环，需要填充
            Raster = 3,    ///< 位图路径（暂不实现，但骨架先留）
        };
        GeometryKind kind = GeometryKind::Contour;
        uint64_t entityId = 0;
        int32_t layerId = 0;

        // kind == Contour
        std::vector<Contour> contours;
        // kind == Point
        Ut::Vec2d point;
        // kind == Fillable
        FillRegion fillRegion;
    };

    // ---- 工艺参数（完全中性，不 include Engine/Process/ProcessParams.h）----

    struct ProcessParams
    {
        // -- 激光 --
        double powerPercent = 50.0;      ///< 0..100
        double frequencyHz = 20000.0;
        double pulseWidthUs = 10.0;
        int32_t waveformIndex = 0;

        // -- 运动 --
        double speedMmPerSec = 200.0;
        double rapidSpeedMmPerSec = 300.0;

        // -- 遍次 --
        int32_t passes = 1;

        // -- 曲线离散 --
        double chordToleranceMm = 0.01;

        bool valid() const { return powerPercent >= 0 && powerPercent <= 100.0
                              && speedMmPerSec > 0 && passes >= 1; }
    };

    // ---- 工艺变换参数 ----

    struct KerfParams
    {
        double widthMm = 0.0;
        bool enabled = false;
        int32_t side = 0;  ///< 0=Outside, 1=Inside, 2=Center
    };

    struct LeadInOutParams
    {
        bool enabled = false;
        double leadInLengthMm = 0.0;
        double leadOutLengthMm = 0.0;
        double rampAngleDeg = 0.0;
        int32_t leadInMode = 0;  ///< 0=切线, 1=法线, 2=直线
    };

    struct PierceParams
    {
        bool enabled = false;
        uint32_t pierceDelayUs = 500;
        double piercePowerPercent = 30.0;
        double pierceSpeedMmPerSec = 50.0;
        int32_t pierceCount = 1;   ///< 多次穿孔
        uint32_t dwellUs = 200;
    };

    struct CornerParams
    {
        int32_t strategy = 0;    ///< 0=保持尖角, 1=圆角过渡, 2=尖角减速
        double radiusMm = 0.0;   ///< strategy=1 时生效
        double overcutMm = 0.0;  ///< strategy=2 时过切补偿
        double slowdownFactor = 0.5;  ///< strategy=2 时尖角处速度乘数
    };

    struct PowerCurvePoint
    {
        double speedMmPerSec;
        double powerPercent;
    };

    struct PowerCurve
    {
        bool enabled = false;
        std::vector<PowerCurvePoint> points;  ///< 至少两点，按 speed 升序
    };

    struct PassStrategy
    {
        bool adaptiveThickness = false;
        int32_t totalDepthLayers = 1;     ///< 渐进切割
        double totalDepthMm = 1.0;
    };

    struct RasterEngraveParams
    {
        double dpi = 300.0;
        double scanAngleDeg = 0.0;
        double lineSpacingMm = 0.05;
        int32_t mode = 0;  ///< 0=Binary, 1=Dither, 2=Halftone, 3=PowerMap, 4=HeightMap
    };

    // ---- 作业输入总结构 ----

    struct ToolpathJobInput
    {
        char jobId[128] = {};

        // 几何快照（按图层分桶）
        struct LayerBucket
        {
            int32_t layerId = 0;
            bool visible = true;
            bool locked = false;
            bool outputEnabled = true;
            ProcessParams params;

            // 工艺变换参数（可覆盖默认）
            KerfParams kerf;
            LeadInOutParams leadInOut;
            PierceParams pierce;
            CornerParams corner;
            PowerCurve powerCurve;
            PassStrategy passStrategy;

            std::vector<EntitySnapshot> entities;
        };
        std::vector<LayerBucket> layers;

        // 全局设置
        bool selectionOnly = false;
        int32_t selectionCount = 0;
        bool emitArcs = true;
        bool skipHiddenLayers = true;
        bool skipLockedLayers = true;
        bool failOnUnmachinable = true;
    };

}  // namespace Cam
```

### 2.3 输出契约

CAM 有两个输出通道：

**通道 A：`CamPlan`（工具级输出，供走刀预览/统计）**

```cpp
// CAM/CAM/Include/CAM/CamPlan.h

namespace Cam
{
    /// CAM 编译结果：完整的工艺刀路（还没翻译 MotionPlan IR）
    /// 用于预览、统计、走刀模拟。
    struct CamPlan
    {
        struct Contour
        {
            std::vector<PathSegment> segments;
            uint64_t sourceEntityId = 0;
            int32_t sourceLayerId = 0;
            // 穿孔点列表（在 contour 起点/或轮廓内）
            std::vector<Ut::Vec2d> piercePoints;
            double lengthMm = 0.0;
            double cuttingLengthMm = 0.0;
            double rapidLengthMm = 0.0;
        };

        struct LayerResult
        {
            int32_t layerId = 0;
            ProcessParams params;
            int32_t passCount = 1;
            std::vector<Contour> contours;  ///< 已排序、已引线、已穿孔
        };

        std::vector<LayerResult> layers;
        double estimatedSeconds = 0.0;
        double totalCuttingLengthMm = 0.0;
        double totalRapidLengthMm = 0.0;
        // 所有轮廓的包围盒（用户坐标；机器坐标变换由 Main 发射器在 M2 一步完成）
        double boundsMinX = 0.0;
        double boundsMinY = 0.0;
        double boundsMaxX = 0.0;
        double boundsMaxY = 0.0;
    };
}  // namespace Cam
```

**通道 B：`Hw::MotionPlanBuilder`（直接调用，供设备执行）**

CAM 层直接持有 `#include "Hardware/Device/MotionPlan.h"` 并 emit，**但 Hardware 只能是 CAM 的 PRIVATE_LINK_LIBRARIES**——消费 CAM 的模块不能通过 CAM 看到 Hardware 类型。如果需要中间层，桥接层自己持有 MotionPlanBuilder。

### 2.4 谁负责桥接

**桥接层在 Main/Src/Hardware/ 或 Main/Src/CAM/（新建）**，职责：

```cpp
// Main/Src/CAM/CamInputBridge.h —— 桥接层，只在 Main 侧

namespace CamBridge
{
    /// 把 SceneManager + LayerManager + ToolpathJobSpec 转成 CAM 的 ToolpathJobInput
    /// 这是 Engine2D 类型 → CAM 中性 C++ 值类型的唯一入口
    Cam::ToolpathJobInput buildJobInput(
        const Eg::SceneManager& scene,
        const LayerManager* layers,
        const ToolpathJobSpec& spec);

    /// 内部调 Eg::Cam::GeometryExtractor::extract()，把 ExtractResult 转成 EntitySnapshot
    Cam::EntitySnapshot extractEntity(
        const Eg::SyEntity* entity,
        const Eg::Cam::ExtractOptions& options);
}
```

**为什么桥接不能在 CAM 里反向做**：CAM 反向 include Engine2D 会让 CAM 变成 Engine2D 的下层依赖（CMake 上 PUBLIC_LINK_LIBRARIES Engine2D），这意味着消费 CAM 的任何模块（包括 UI3D、Vision）都会被迫间接 include Engine2D——这是一个假依赖，破坏边界。

---

## 3. 内部子模块划分

CAM/CAM/Src/ 下的目录结构：

```
CAM/CAM/
├── Include/CAM/
│   ├── CAMExport.h
│   ├── CamCore.h              // 编译器主入口
│   ├── ToolpathJobInput.h     // 输入契约（§2.2）
│   ├── CamPlan.h              // 工具输出契约（§2.3 通道A）
│   ├── Progress.h             // 进度/取消/上下文
│   └── Public/                // 未来可能独立的子模块公开头
└── Src/
    ├── CamCore.cpp            // 编译器主流程（Pipeline 编排）
    ├── GeometryTransform/     // 几何预处理
    │   ├── KerfCompensator.cpp
    │   ├── LeadInOut.cpp      // 可复用 Engraving::LeadInOut
    │   ├── CornerProcessor.cpp
    │   ├── PiercePlanner.cpp
    │   ├── CoEdgeFinder.cpp   // 共边切割（新实现）
    │   └── PassExpander.cpp   // 分层/渐进切割展开
    ├── Order/
    │   ├── ContourSorter.cpp  // 可复用 Engraving::ToolpathOrder
    │   ├── TravelOptimizer.cpp // 2-opt / 最近邻（可复用 Raster_OptimizeTravel）
    │   ├── HoleFirstResolver.cpp
    │   └── LayerOrderResolver.cpp
    ├── Process/               // 工艺参数处理
    │   ├── PowerCurveEvaluator.cpp  // 速度→功率映射
    │   ├── ParamIntegrator.cpp      // 把 LayerBucket.params + layer 覆盖 + pass 展开合并成逐 contour 的参数
    │   └── RampDownPlanner.cpp      // 斜坡下降/起停延时
    ├── Raster/                 // 位图雕刻（骨架）
    │   └── RasterEngraveCompiler.cpp  // 暂不实现
    ├── Emit/
    │   └── MotionPlanEmitter.cpp  // CamPlan → Hw::MotionPlanBuilder
    ├── Validate/
    │   ├── BoundsValidator.cpp      // 用户坐标包围盒预检（机器坐标越界由发射器复检）
    │   └── EmptyPlanDetector.cpp
    └── Estimate/
        └── TimeEstimator.cpp
```

### 各子模块输入输出

| 子模块 | 输入 | 输出 | 依赖 |
|--------|------|------|------|
| **KerfCompensator** | `vector<Contour>`, `KerfParams` | 补过 kerf 的 `vector<Contour>`（绕向反转、偏移） | Engraving::PolygonOffset（PRIVATE） |
| **LeadInOut** | `Contour`, `LeadInOutParams` | 补过引线/引出线的 `Contour`（segment 前缀/后缀追加） | Engraving::LeadInOut（PRIVATE） |
| **PiercePlanner** | `vector<Contour>`, `PierceParams` | 每个 contour 上附着 pierce point + pierce 前的额外 Rapid 段 | 纯数学 |
| **CornerProcessor** | `Contour`, `CornerParams` | 圆角插入 / 尖角减速标记 | 纯数学 |
| **CoEdgeFinder** | `vector<Contour>` | 标记共享边的 ContourPair + 去重后的共享 Contour（只编译一次） | 纯几何 + 线段求交 |
| **ContourSorter** | `vector<Contour>`（按图层分组） | 排序后的 `vector<vector<Contour>>`（内层最近邻，外层先内后外） | Engraving::ToolpathOrder（PRIVATE） |
| **TravelOptimizer** | 已排序的轮廓 + 起始点位 | 优化后的轮廓序列 | 纯算法（2-opt） |
| **PassExpander** | `vector<Contour>`, `PassStrategy` | 展开后的 `vector<Contour>`（每份带 passIndex） | 纯容器操作 |
| **PowerCurveEvaluator** | `ProcessParams`, `PowerCurve`, 段速度 | 段功率（逐 Segment 调整 LaserParams） | 纯查找表插值 |
| **MotionPlanEmitter** | `vector<Contour> + ProcessParams per layer` | `Hw::MotionPlanBuilder`（via `#include "Hardware/Device/MotionPlan.h"`） | Hardware/Device/MotionPlan（PRIVATE） |
| **BoundsValidator** | CamPlan | bool ok + 越界轴/坐标 | 纯比较 |
| **TimeEstimator** | CamPlan + ProcessParams | 秒数（切削时间 + 空移时间 + 穿孔时间 + 停留时间） | 纯累加 |

---

## 4. 关键接口草案

### 4.1 编译器主入口（C++ 内部接口）

```cpp
// CAM/CAM/Include/CAM/CamCore.h
#pragma once
#include "CAM/ToolpathJobInput.h"
#include "CAM/CamPlan.h"
#include "CAM/Progress.h"

namespace Cam
{
    /// 编译结果（内部用；对外 C ABI 另行定义）
    struct CamCompileResult
    {
        bool ok = false;
        char error[256] = {};    ///< 可读错误文本，UTF-8
        CamPlan plan;
        int32_t layerCount = 0;
        int32_t entityCount = 0;
        int32_t contourCount = 0;
    };

    /// 编译输入 JobInput → 输出 CamPlan
    /// 纯函数，可在后台线程调用（不依赖 GUI / Qt / OpenGL）
    /// 通过 Progress 结构体上报进度和取消请求
    CamCompileResult compile(const ToolpathJobInput& input, Progress* progress = nullptr);

    /// 可选：直接编译成设备 IR（跳过 CamPlan 中间层，走优化路径）
    /// 这是给"设备直接执行"场景用的快捷入口
    // 暂不暴露，等 M1 阶段 MotionPlanEmitter 完成后再加
}
```

### 4.2 对外 C ABI（已否决）

CAM 当前只服务 SanYi CAD 主工程，公开契约定为 C++ 内部 ABI（见 `CAM/CAM/Include/CAM/CAMExport.h`）：结构体含 STL 容器，使用方必须与 CAM 同编译器、同 CRT、同 `_ITERATOR_DEBUG_LEVEL`、同运行时库构建；对外 C ABI 路线已否决。若未来出现仓库外消费方（例如 C#/Delphi 客户二次开发），需重新立项评估，不属于本文档既定路线。

### 4.3 进度/取消上下文

```cpp
// CAM/CAM/Include/CAM/Progress.h
#pragma once
#include <cstdint>

namespace Cam
{
    /// 长耗时任务的进度与取消机制
    /// 线程安全：caller 在主线程设置 cancelled=true；worker 在后台轮询
    struct Progress
    {
        /// 0.0 ~ 1.0，worker 在每个可取消的阶段入口更新
        volatile double fraction = 0.0;

        /// 阶段描述（UTF-8，静态字符串或调用方持有生命周期）
        const char* stage = "";

        /// 如果置 true，worker 应尽快从 compile 返回（返回 ok=false + error="Cancelled"）
        volatile int32_t cancelled = 0;

        /// 可选回调（如果需要跨线程事件）
        /// 只在后台线程调用，callback 内部必须自己做线程转发
        void (*onProgress)(double fraction, const char* stage, void* userData) = nullptr;
        void* userData = nullptr;
    };
}
```

---

## 5. UI 交互

### 5.1 UI 形态

| 形态 | 对应功能 | 责任边界 |
|------|----------|----------|
| **菜单命令** | 开始加工 / 暂停 / 恢复 / 急停 / 红光预览 | UI 只发命令，不碰 CAM 对象 |
| **工艺参数面板** | 每层独立参数（功率/速度/遍次/模式/引线/穿孔/功率曲线/拐角） | UI 编辑 → 同步到 LayerProcessParams → 编进 ToolpathJobInput |
| **走刀预览** | 视口内彩色渲染 CamPlan（按图层分色），走刀动画 | UI 消费 CamPlan（只读），不持有 CAM 内部对象 |
| **排版预览** | 套料结果在视口内可视化 | 套料模块自己的预览，CAM 不参与 |
| **作业队列（未来）** | 批量提交加工任务 | 暂不实现 |

### 5.2 交互边界（谁调用谁）

```
UI (ProcessDialog 点击"开始加工")
    │
    │① 收集参数（图层工艺 + 全局）
    │② 触发命令 Laser_StartProcess
    ▼
CommandRegistry → LaserOperationRegistry → 命令处理器
    │
    │③ 安全门控（SafetyManager::canStartProcessing()）
    │④ 调 CamBridge::buildJobInput(scene, layers, spec)  ← 桥接层在 Main
    │⑤ Cam::compile(jobInput, &progress)                ← CAM 主编译
    │⑥ 调 MotionPlanBuilder.emit* 或 Cam 内部 MotionPlanEmitter
    │⑦ 调 ProcessingJobService::startPlan(motionPlanView) ← 设备执行
    ▼
ProcessingJobService（进度轮询 + 信号）
    │
    │⑧ jobProgress(double fraction) → UI 进度条
    │⑨ jobFinished(bool success, msg) → UI 结束态
```

**核心约束**：UI **永远不持有** CAM 内部对象。UI 只通过**命令（Command）** 和 **服务（ProcessingJobService + 未来的 CamCompileService）** 与 CAM 交互。UI 层以后换成别的前端（Web/小程序/远程控制），只要能发命令就能跑，CAM 一行不改。

### 5.3 异步与进度

编译 CAM 计划在后台线程执行。复用现有 ImportService/ExportService 的**五段式异步模式**（主线程收集快照 → 后台编译 → 主线程回写结果），但 CAM 编译通常只需几百毫秒（非十万图元），可以先做同步路径，后续根据实际耗时再升级。

进度机制：`Progress` 结构体 + 阶段回调。M0 阶段只在关键阶段更新 fraction：
```
编译流程 → Extract几何（0%~20%）→ 工艺变换（20%~60%）→ 排序优化（60%~80%）→ 验证与预估（80%~100%）
```

### 5.4 错误呈现

所有错误必须走**可枚举的返回值 + 可读文本**，禁止静默：

```cpp
// CamCompileResult.error 覆盖的错误场景：
"Invalid process params for layer %d (speed %f <= 0)"
"No processable entities (all skipped / unmachinable)"
"Chord tolerance %f exceeded maximum %f"
"Selected-only mode but no entities selected"
"Unsupported entity kind: %s"
"Compilation cancelled by user"
```

错误由命令处理器直接展示给 UI（MessageBox 或状态栏），CAM 层不持有 UI 引用。

---

## 6. 硬件关联

### 6.1 真实状况（按之前取证）

| 组件 | 现状 | 真机配合需求 |
|------|------|-------------|
| `IMotionCard` / `IGalvoCard` 接口 | ✅ 完备 | 100% 已有，无需改 |
| `MotionPlan` IR | ✅ 完备 | 100% 已有，无需改 |
| `MotionPlanBuilder` | ✅ 完备 | 100% 已有 |
| `DeviceHost` | ✅ 设备装配逻辑 | 需要真机来验证 open() 的参数是否正确 |
| **真机适配器**（雷赛/正运动/Scanlab） | ❌ 基本为桩 | **必须真机**，每个协议 1~2 周 |
| `SimulatedDevice` | ✅ 可用 | 0% 真机，纯软件可验证 |
| `SafetyManager` | ✅ 安全门控逻辑 | 可在 SimulatedDevice 上验证 |

### 6.2 CAM → 设备的解耦机制

CAM 输出 `CamPlan` → MotionPlanEmitter → `Hw::MotionPlanBuilder`（IR）→ 设备适配器。适配器只认 IR opcode（RapidTo/LineTo/ArcTo/LaserOn/...），不认任何 CAM 内部类型。

**驱动注册**：由 DeviceHost 在启动时按配置实例化对应适配器，CAM 不参与驱动选择。新增一种设备 = 写一个适配器 DLL，CAM 零改动。

### 6.3 没有真机时的可验证范围

| 能力 | 是否需要真机 | 验证方式 |
|------|-------------|----------|
| 几何预处理（割缝/引线/排序/穿孔/拐角） | ❌ 纯软件 | 单元测试 + 黄金文件比对 |
| MotionPlan IR 输出 | ❌ | motionPlanToText() 导出文本，人工核对 |
| 软限位预检 / 越界检测 | ❌ | 构造边界场景，断言报错 |
| 时间预估 | ❌ | 给定段长度 + 速度，手算 expectedSeconds |
| ProcessingJobService 状态机 | ❌ | SimulatedDevice 跑完整计划 |
| 真机协议适配 | ✅ 必须真机 | 先做 SimulatedDevice 覆盖功能，真机联调 |

---

## 7. 线程与生命周期

### 7.1 任务长耗时特性

CAM 编译（几何 → CamPlan → MotionPlan）的耗时与图元数量正相关：
- 100 图元：< 100ms
- 1000 图元：< 1s
- 10000 图元：可能 3~5s（割缝补偿是 O(n²)）

M0 阶段先同步执行（主线程等 1~2s 是可接受的），如果实测确实卡 UI 再升级。

### 7.2 五段式异步（可选升级）

```
阶段 1（主线程）：CamBridge::buildJobInput(scene, layers, spec) → ToolpathJobInput
阶段 2（后台线程）：Cam::compile(jobInput, &progress) → CamPlan
阶段 3（后台线程）：MotionPlanEmitter.emit(camPlan, params, &motionPlanBuilder)
阶段 4（主线程）：ProcessingJobService::startPlan(motionPlanView, jobId)
阶段 5（主线程）：设备执行 + 进度回传
```

**取消**：Progress.cancelled = 1 → CAM 在每个阶段入口检查 → 从 compile 返回 ok=false。已执行的阶段不产生副作用（ToolpathJobInput 是快照，不影响原始场景）。

**防重入**：ProcessingJobService 已有 isRunning() 检查；CAM 层 compile() 是纯函数，天然可重入。

**失败回滚**：编译失败 = 设备侧什么都没 load = 零回滚需求。

---

## 8. 分阶段落地路线

### M0 —— 几何预处理 + CamPlan 输出（核心骨架，2~3 周）

| 范围 | 可验证产出 | 验收标准 |
|------|-----------|----------|
| CAM/CAM 模块骨架（CMake + 导出宏 + ToolpathJobInput.h） | 模块能 add_subdirectory，BUILD_CAM=ON/OFF 切换无错 | configure + build 成功 |
| CamBridge::buildJobInput（桥接层） | SceneManager + GeometryExtractor → ToolpathJobInput | 构造简单场景，断言 jobInput.layers.size > 0 |
| CamCore::compile（pipeline 骨架） | 输入 JobInput → 输出 CamPlan（只走 Extract + LayerOrder + Emit 骨架） | 单元测试：给定 ToolpathJobInput → 断言 CamPlan.layers.size > 0 |
| KerfCompensator（割缝补偿） | 调 Engraving::PolygonOffset | 单元测试：正方形轮廓外扩 2mm → 断言边长增长 4mm |
| LeadInOut（引入引出线） | 调 Engraving::LeadInOut | 单元测试：轮廓起点前插入切线引线 |
| PiercePlanner（穿孔点） | 在 contour 起点前加 Rapid + Dwell | 单元测试：断言 contour 有 pierce point |
| ContourSorter（排序） | 调 Engraving::ToolpathOrder（最近邻 + 先内后外） | 黄金文件比对 |
| MotionPlanEmitter | CamPlan → MotionPlanBuilder | motionPlanToText() 文本导出，人工核对 opcode |

**依赖前置**：无。只依赖 Engine2D（GeometryExtractor 类型）、Engraving（偏移/引线/排序算法）、Hardware（MotionPlan IR）、Log。

### M1 —— 工艺深度 + 完整编译链 + 时间预估（3~4 周）

| 范围 | 验收标准 |
|------|----------|
| CornerProcessor（拐角处理：圆角/尖角减速） | 单元测试：尖角处自动插圆角段 |
| CoEdgeFinder（共边切割） | 单元测试：两个相邻矩形的共享边只编译一次 |
| PassExpander（分层渐进切割） | 单元测试：3 层渐进 → 每个 contour 重复 3 次，速度递减 |
| PowerCurveEvaluator（功率曲线） | 单元测试：speed=300 → 根据曲线插值 power=45% |
| RampDownPlanner（斜坡下降） | 在 RapidTo 前插入 ramp 段（逐步提速） |
| HoleFirstResolver（内孔先于外轮廓） | 单元测试：含孔洞的轮廓对，hole 的 contour 排在 outer 前面 |
| TimeEstimator（时间预估） | 手算 expectedSeconds，误差 < 5% |
| BoundsValidator（用户坐标预检） | 给超出可选台面范围的 contour → ok=false + error；变换后的机器坐标越界由发射器复检 |
| **同步路径端到端测试** | SceneManager → CamBridge → Cam::compile → MotionPlan → motionPlanToText() → 人工核对 |

### M2 —— UI 集成 + 走刀预览（3~4 周）

| 范围 | 验收标准 |
|------|----------|
| CAM 工作台工艺参数面板（每层 Kerf/LeadInOut/Pierce/Corner/PowerCurve） | UI 编辑 → compile 出来的 MotionPlan 能看到对应 opcode |
| 走刀预览（视口里渲染 CamPlan，按图层分色 + 动画） | 拖动时间轴 → 看到走刀顺序动画 |
| ProcessingJobService 接入完整 CAM 编译链（替换 MotionPlanCompiler） | 点"开始加工" → 走完 M0/M1 完整编译 → SimulatedDevice 执行 |
| 错误面板（unmachinable entities 清单 + 跳过策略开关） | 故意给一个 Raster 图元 → 显示"暂不可加工"的详细清单 |
| 取消（Progress.cancelled） | 编译过程中按取消按钮 → 立即返回，设备侧无动作 |

### M3 —— 真机联调 + 模拟器（2~4 周，**必须真机配合**）

| 范围 | 验收标准 |
|------|----------|
| SimulatedDevice 覆盖完整 MotionPlan opcode（含穿孔 Dwell、SetLaserParams、SetFeedRate） | 跑 1000 图元的完整计划 → SimulatedDevice 状态正确推进 |
| 首款真机协议适配（雷赛 DMC 系列或 Scanlab RTC5，二选一） | 真机实际切割一个 100mm 正方形 → 尺寸误差 < 0.1mm |
| SafetyManager 端到端（急停 → 停光 → 停轴 → 安全事件上报） | 运行中按急停 → 真机响应 |
| 回零 + 工件原点 | 回零成功 → 设置工件零点 → 绝对定位正确 |
| **视觉引导（可选）** | Vision 模块的 mark detection → 坐标系校正 → CAM 编译时自动偏移原点 |

### 工作量汇总

| 阶段 | 预估人天 | 主要风险 |
|------|----------|----------|
| M0 | 15~20 | 桥接层类型转换中绕向/孔洞判定可能出错（但 GeometryExtractor 已有测试） |
| M1 | 20~25 | 共边切割的共享边判定是 O(n²) + 线段求交，可能需要调优 |
| M2 | 20~25 | UI 交互细节（参数面板的实时预览反馈）可能反复 |
| M3 | 10~20（含真机联调） | 真机协议的细节（脉冲当量、回零时序）可能需要反复试 |

---

## 9. 测试策略

### 9.1 无硬件依赖的测试目标组织

```
CAM/CAM/Test/
├── CMakeLists.txt         # CAMTests target，只 link CAM::CAM + Engine2D::Engine2D + Engraving::Engraving + Hardware::Hardware + 仿真
├── CamCoreTests.cpp       # 主编译流程：给定 ToolpathJobInput → 断言 CamPlan 字段
├── KerfCompensatorTests.cpp
├── LeadInOutTests.cpp
├── PiercePlannerTests.cpp
├── ContourSorterTests.cpp
├── CornerProcessorTests.cpp
├── CoEdgeFinderTests.cpp
├── PassExpanderTests.cpp
├── PowerCurveEvaluatorTests.cpp
├── MotionPlanEmitterTests.cpp  # 黄金文件：emit 文本 vs expected .golden
├── TimeEstimatorTests.cpp      # 手算 expected 断言
├── BoundsValidatorTests.cpp
├── CamBridgeTests.cpp          # Main 侧桥接层测试（只在 BUILD_CAM 开时编）
└── golden/                     # motionPlanToText() 的黄金文件
    ├── simple_contour.golden
    ├── kerfed_square.golden
    ├── with_lead_in_out.golden
    └── common_edge_pair.golden
```

### 9.2 CMake 目标声明要点

```cmake
# CAM/CAM/Test/CMakeLists.txt
if(BUILD_ALL_TESTS OR BUILD_CAM)
    add_executable(CAMTests
        # 所有测试 .cpp
    )
    # 注意：CAMTests 只能 link CAM::CAM（公开）+ 测试桩
    # 不能直接 include Main/Src/CAM/CamBridge.h —— 桥接层的测试在 MainTests 里
    target_link_libraries(CAMTests
        PRIVATE
            CAM::CAM
            Engine2D::Engine2D    # 只用于 GeometryExtractor 的枚举值（kindOf）
            Engraving::Engraving
            Hardware::Hardware    # MotionPlan / MotionPlanBuilder / SimulatedDevice
            GTest::gtest
    )
    # 测试目标不需要 AUTOMOC（CAM 是纯 C++，无 Q_OBJECT）
    target_include_directories(CAMTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..  # 让测试可以 #include "Src/KerfCompensator.h"
    )
    add_test(NAME CAMTests COMMAND CAMTests)
endif()
```

### 9.3 模拟器端到端测试

利用 Hardware 模块已有的 SimulatedDevice：
```cpp
TEST(CAMTests, FullPipeline_SimulatedDevice)
{
    // 1. 造 ToolpathJobInput（正方形轮廓 + 割缝 + 引线 + 穿孔）
    Cam::ToolpathJobInput input = makeSquareWithKerf();
    // 2. 编译
    auto result = Cam::compile(input);
    ASSERT_TRUE(result.ok);
    // 3. Emit 成 MotionPlan
    Hw::MotionPlanBuilder builder;
    Cam::MotionPlanEmitter::emit(result.plan, params, builder);
    Hw::MotionPlanView view = builder.view();
    // 4. SimulatedDevice load + start
    Hw::SimulatedDevice device;
    ASSERT_EQ(device.loadPlan(view), HwError::Ok);
    device.startPlan();
    // 5. 轮询进度直到完成
    while (device.planProgress().state != Hw::PlanState::Completed) {
        ASSERT_NE(device.planProgress().state, Hw::PlanState::Error);
        // ...
    }
    // 6. 断言最终位置
    auto pos = device.readPositions();
    EXPECT_NEAR(pos.x, expectedFinalX, 0.01);
}
```

---

## 10. 风险与反模式

### 🔴 高风险反模式（必须防）

| # | 反模式 | 后果 | 约束写法 |
|---|--------|------|----------|
| 1 | **CAM 反向 include SceneManager / LayerManager / SyEntity** | CAM 变成 Engine2D 下层依赖，消费 CAM 的模块被迫间接 include Engine2D，破坏边界 | CAM 的 PUBLIC_LINK_LIBRARIES 只能有 Log；PRIVATE 可有 Engraving + Engine2D + Hardware，但 CMake/CheckDependencies.cmake 必须显式校验 CAM 的 INTERFACE_INCLUDE_DIRS 不泄漏 Engine2D/UICommon/RenderBridge 的头路径 |
| 2 | **MotionPlanCompiler 继续直接从 SceneManager 编译** | ToolpathJobInput 中性边界被绕过，旧代码路径与新路径双轨维护 | M2 阶段 ProcessingJobService 必须切换到 CamBridge + Cam::compile，原 MotionPlanCompiler 标记 deprecated，两阶段后删除 |
| 3 | **后处理器（MotionPlanEmitter）写死某款设备协议** | 新增一种设备就要改 CAM | MotionPlanEmitter **只输出** `Hw::MotionPlanBuilder`，绝不出现厂商 SDK 的任何类型或函数调用 |
| 4 | **UI 层直接持有/调用 CAM 内部类** | UI 换前端要改 CAM | CAM 的 PUBLIC API 只有 `CamCore::compile`（C++ 内部 ABI，对外 C ABI 已否决）。UI 侧只有 Service 层（ProcessingJobService + 未来的 CamCompileService）持有 CAM 引用 |
| 5 | **PowerCurveEvaluator 直接写 QVariant/QTableWidget 从 UI 传参数** | CAM 被 UI 类型污染 | PowerCurve 在 ToolpathJobInput 里是纯 `std::vector<PowerCurvePoint>`，UI 负责把 QTableWidget 转成纯结构 |
| 6 | **失败时静默跳过图元** | 用户以为烧了、其实没烧 → 废件 | compile 的 ok=false 必须有明确 error，failOnUnmachinable 默认 true；ProcessingJobService 在 ok=false 时**绝不**调用 startPlan |

### 🟡 中风险

| # | 反模式 | 后果 | 约束 |
|---|--------|------|------|
| 7 | 割缝补偿 / 共边 / 引线全部在 MotionPlanEmitter 层做 | 丢失轮廓的包含/绕向信息，孔边外扩方向搞反 | 这些几何变换**必须在 CamPlan 生成阶段做**，MotionPlanEmitter 拿到的是已经变换好的 CamPlan |
| 8 | 直接用 std::vector<Eg::Cam::Contour> 作为 ToolpathJobInput.contours | CAM 间接依赖 Engine2D 头 | ToolpathJobInput 必须定义**自己的** Contour/PathSegment 副本（拷贝构造开销可忽略，通常 < 1ms） |
| 9 | 穿孔参数写成固定常量，不从 ProcessParams 里读 | 不同材料/板厚需要不同穿孔参数 | PiercePlanner 的输入必须是 ToolpathJobInput::layers[i].pierce，不是模块内 constexpr |

---

## 附录 A：需要用户拍板的关键决策点

| # | 决策点 | 选项 | 推荐 |
|---|--------|------|------|
| D1 | CAM 是否独立顶层目录 | ① 独立 CAM/ ② 挂 Engine2D/Cam/ 下 | ① 独立，理由见 §1.1 |
| D2 | 是否引入对外 C ABI | ① 只做 C++ 内部接口 ② 同步做 C ABI | ① C++ 内部 ABI；对外 C ABI 已否决，若出现仓库外消费方再重新立项 |
| D3 | 首款真机协议选谁 | ① 雷赛 DMC 系列 ② Scanlab RTC5/6 振镜卡 ③ 正运动 ZMC | 取决于目标客户群；运动卡覆盖面最广，振镜卡精度要求低 |
| D4 | ProcessingJobService 是否废弃 MotionPlanCompiler | ① M2 切换 + 标记 deprecated，M3 删除 ② 保留双轨 | ① 彻底切换，避免双轨维护 |
| D5 | 走刀预览的实现方式 | ① UI 侧遍历 CamPlan 自己渲染 ② RenderBridge 加 ToolpathRenderer | ① UI 侧遍历 + 现有 Viewport 画线，改动最小 |
| D6 | 进度/取消是否做独立线程 | ① 先同步 ② 直接做五段式异步 | ① 先同步，实测耗时再升级 |
