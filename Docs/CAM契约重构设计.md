# CAM 契约重构设计

> 范围：`CAM/CAM/` 公开契约、`Main/Src/CAM/` 桥接层、`FileIO` 工艺参数持久化、`CMake/CheckDependencies.cmake` 规则 4.5
> 性质：设计文档，不含任何代码改动
> 证据：以当前工作区代码为准，格式为 `文件:行号`

---

## 0. 结论摘要

| 编号 | 议题 | 核实结果 | 结论 |
|---|---|---|---|
| 1 | ABI 定位自相矛盾 | 属实，**已实施** | 选 **B：承认 C++ 内部 ABI**，删除仪式性 `structSize`，用同工具链构建守卫替代；**对外 C ABI 已否决**，若未来出现仓库外消费方需重新立项评估 |
| 2 | 工艺参数多份定义且漂移 | 属实（3 份完整定义 + 2 组平行字段集）；但"缺 `enabled`"这一子项不成立 | 抽 header-only 中立契约目标，`Eg` / `Cam` / `FileIO` 收敛为同一类型 |
| 3 | `CamPlan` 表达不了 M1/M2 工艺 | 属实 | 输出改为有序 op 流，预览与发射器消费同一份流；不引入设备耦合 |
| 4 | 段表示存在两套真值 | 属实，且已造成实际缺陷（角度单位矛盾） | 规范为 **起点 + 终点 + 圆心 + 方向**，半径/角度改派生；输入输出类型合并 |
| 5 | 桥接层吞掉不可加工信息 | 属实（CAM 链路上）；"reason 从未被读"全仓库不成立 | `EntitySnapshot`/`CamResult` 加枚举问题码与明细清单，桥接层禁止丢图元 |
| 6 | 深拷贝与固定开销 | 属实，**已实测** | 已补 `CamBench`：深拷贝占链路 45%~48%，据此把"统一类型 + move"列为 D2 优先项，池化按数据再定 |
| 7 | 文档与实现漂移 | 属实，并补充 8 条；"CMake/Config 注释陈旧"这一子项不成立 | 钉死 **CAM 全程用户坐标，变换只在 Main 发射器一步完成**；给出完整对照表 |
| 附 1 | 角度单位矛盾（新发现） | 属实 | 抽取器写弧度、`CamCore` 按度算长度，圆弧长度差约 57.3 倍；测试用度构造掩盖 |
| 附 2 | `BUILD_CAM=ON` 时桥接层无法编译（新发现） | 当时属实，**已修复** | 桥接层已改用 `Cam::Contour`/`Cam::PathSegment`（`CamInputBridge.cpp:80-96`）；`BUILD_CAM` 现已默认 ON（`Config.cmake:226`） |

---

## 1. ABI 定位：真 C ABI 还是 C++ 内部 ABI

### 1.1 现状与证据

- `CAM/CAM/Include/CAM/CAMExport.h:8-18` 写明 ABI 定位："C++ 内部 ABI（不是 C ABI）"；公开头暴露 `std::vector`/`std::string`，使用方必须与 CAM 同编译器、同 CRT、同 `_ITERATOR_DEBUG_LEVEL`、同运行时库构建；"对外 C ABI"路线已否决。
- 公开结构持有 STL 容器：`ToolpathJobInput.h:86`（`Contour.segments`）、`:109`（`FillRegion.boundaries`）、`:132`（`EntitySnapshot.contours`）、`:147`（`std::string unmachinableReason`）、`:235`（`PowerCurve.points`）、`:271`（`LayerBucket.entities`）、`:274`（`ToolpathJobInput.layers`）；`CamPlan.h:75`、`:84`、`:109`、`:118`。
- 版本标识：`CAMExport.h:34` 定义 `CAM_ABI_VERSION = 1`；`CAMExport.h:39-51` 声明 `Cam::CamAbiInfo` 与 `Cam::camAbiInfo()`，返回版本号与 6 个关键结构 `sizeof`（`PathSegment`/`Contour`/`EntitySnapshot`/`ToolpathJobInput`/`CamPlan`/`CamResult`）；实现见 `CamCore.cpp:48-58`。
- 公开结构没有 `structSize`：`CAM/CAM/Include/CAM/` 全文检索只命中注释中的"不保留 `structSize` 版本守卫字段"说明（`ToolpathJobInput.h:31`、`CamPlan.h:31`、`CamContext.h:9`），没有字段声明；`Main/Src/CAM/CamInputBridge.cpp` 也没有赋值。
- 自检挂载点：`CamInputBridge.cpp:36-56` 的 `logAbiSelfCheck()` 在 `buildJobInput` 首次调用时（`:133`）打印一次；应用启动路径尚未接入（见第 10 节）。
- 测试：`CamCoreTests.cpp:89-120`（`PublicStructs_DefaultConstructToEmpty`）、`:126-139`（`AbiSelfCheck_MatchesPublicStructSizes`）。
- Raster 的纯 C ABI 对照（否决参照）：`Raster/Raster/Include/Raster/RasterApi.h:26-36`（纯 C ABI、句柄、不抛异常、入参带 `structSize`）；`structSize` 在入口确实被校验，`RasterApi.h:455-458` 要求 `paramsSize` 必须是 `sizeof(RasterOpParams)`，错误码 `RASTER_E_STRUCT_SIZE`（`RasterTypes.h:99`）。
- 仓库已有"避免 STL 跨 DLL"的成熟做法：`Hardware/Hardware/Include/Hardware/Device/MotionPlan.h:188-203` 的 `MotionPlanBuilder` 用 pimpl 把 `std::vector` 全部藏在 `.cpp`（C4251、跨运行库释放、可换存储）。

### 1.2 关键约束（决策依据）

1. **`structSize` 保护不了容器布局**：消费方编译期用自己头文件的 `sizeof` 初始化默认成员，发送方的值会被覆盖，无法表达"我按哪个版本布局"；它只能防 POD 尾部追加，防不了 `std::vector`/`std::string` 的布局与堆归属差异。
2. **跨 DLL 传 STL 有隐性前提**：同编译器主版本、同 CRT（`/MD` 与 `/MT` 不可混）、同 `_ITERATOR_DEBUG_LEVEL`（Debug/Release 混编会崩）、同对齐与异常模型。这些前提必须写进公开头契约（见 `CAMExport.h:8-18`），并由 `camAbiInfo()` 日志提供运行期对照点。
3. **契约文本必须与实现同口径**：所有公开头只声明 C++ 内部 ABI，不再出现"POD"或"可对外 C ABI"的表述。

### 1.3 方案与取舍

#### 方案 A：真做成 C ABI（Raster 模式）——已否决

否决原因：CAM 只服务仓库内 `Main`，做 C ABI 属于过度设计。公开契约需几乎全部重写（POD + 不透明句柄 + 逐项 `structSize` 校验 + 入参/结果缓冲），失去 C++ 容器与 RAII，测试与桥接层全部要改；当前无仓库外消费方。

#### 方案 B：承认是 C++ 内部 ABI（已实施）

已实施做法：
1. `CAMExport.h:8-18` 写明"C++ 内部 ABI，仅限仓库内同工具链构建的消费方"，并明确"对外 C ABI"已否决。
2. 删除所有公开结构上的 `structSize`（共 21 处）；新增 `CAM_ABI_VERSION = 1` 与 `Cam::camAbiInfo()`，返回版本号与 6 个关键结构 `sizeof`（`PathSegment`/`Contour`/`EntitySnapshot`/`ToolpathJobInput`/`CamPlan`/`CamResult`）。
3. 运行期对照：`Main/Src/CAM/CamInputBridge.cpp:36-56` 的 `logAbiSelfCheck()` 在首次构造输入时打印版本号与 `sizeof`（调用点 `:133`）；完整的启动期比对（不一致即禁用 CAM）尚未接入，见第 10 节。
4. 各公开头（`ToolpathJobInput.h:28-34`、`CamPlan.h:29-32`、`CamContext.h:8-10`）统一写"CAM 的 C++ 内部 ABI，使用方必须与 CAM 同工具链构建"。

优点：改动集中在契约文本、删除 `structSize`、一处自检；保留 C++ 容器带来的开发效率与 M0 代码；与唯一消费方 `Main` 的构建方式天然一致。

代价：不面向仓库外消费方；Debug/Release 混编风险靠自检日志与 CI 统一配置拦截；**对外 C ABI 已否决**，若未来确需外部接入，需重新立项评估。

#### 被否方案

| 方案 | 否决原因 |
|---|---|
| 保留 `structSize` 并"假装 POD" | 对容器布局无保护作用，持续误导接入方 |
| 只保留 `CamResult`/`CamPlan` 顶层 `structSize` | 仍无法校验内部容器布局，害处不变 |
| 在 CAM 内部再包一层 pimpl（照搬 `MotionPlanBuilder`） | CAM 输入/输出本来就是数据快照，pimpl 会让预览与调试无法直接读取；`compile` 是纯函数，无跨 DLL 生命周期需要管理 |
| 同时提供 C ABI 与 C++ 接口 | 两套契约同时维护，M0/M1 成本翻倍；当前无外部消费方，属过度设计 |

### 1.4 对后续硬件厂商接入的影响

- 硬件厂商接入的是 **Hardware HAL**（`IMotionCard`/`IGalvoCard`/`ILaserSource`），消费的是 `Hw::MotionPlanView`（`MotionPlan.h:145-152`：header + 指令指针 + 数量），不是 CAM。
- 因此 CAM 采用 C++ 内部 ABI **不会**阻塞厂商接入；厂商永远不需要链接 CAM DLL。
- 对外 C ABI 已否决；只有在出现"厂商自带 CAM/工艺编译器"或"第三方前端直接调用 CAM"时，才需要重新立项评估（不得直接把本文档的方案 A 当作既定路线）。

### 1.5 改动范围（已实施）

- `CAM/CAM/Include/CAM/CAMExport.h`（契约文本 + `CAM_ABI_VERSION` + `Cam::CamAbiInfo`/`camAbiInfo()`）
- `ToolpathJobInput.h`（删 15 处 `structSize`，整体删除嵌套 `MachineFrameSnapshot`）、`CamPlan.h`（删 4 处）、`CamContext.h`（删 2 处），合计 21 处
- `CamCore.h`、`CamCore.cpp`（契约表述改为中性 C++ 值类型；实现 `camAbiInfo()`，删除 7 处 `structSize` 赋值）
- `CamCoreTests.cpp`（删除 `PodStructSizeGuards`，改为 `AbiSelfCheck_MatchesPublicStructSizes` + `PublicStructs_DefaultConstructToEmpty`，CAMTests 20 → 21）
- `Main/Src/CAM/CamInputBridge.h/.cpp`（删 7 处 `structSize`/`machineFrame` 赋值，新增 `logAbiSelfCheck()`，首次构造输入时打印一次）
- `Tools/PerfBenchmark/CamBench.cpp` + `Tools/PerfBenchmark/CMakeLists.txt` + `main.cpp`（D7 性能基线）

### 1.6 迁移步骤（D1 已实施，仅第 4 步未完成）

1. ✅ ABI 标识并入 `CAMExport.h`：`CAM_ABI_VERSION = 1` + `camAbiInfo()`（版本号 + 6 个关键结构 `sizeof`）；未新建 `CamAbi.h`。
2. ✅ 删除公开结构上的 `structSize`。
3. ✅ 修正 `CAMExport.h` 与各头文件顶部说明。
4. [ ] `Main` 启动路径接入 CAM 作业前比对 `camAbiInfo()`；目前只在桥接层首次构造输入时打日志（见第 10 节风险）。
5. ✅ 更新测试。

### 1.7 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 被外部误用 | 内部 ABI 一旦被外部包含，运行期随机崩溃 | 头文件禁止声明 + 桥接层自检日志 |
| Debug/Release 混编 | `_ITERATOR_DEBUG_LEVEL` 不一致 | 启动自检日志 + CI 只允许统一配置 |
| 自检挂载点缺失 | 当前只在 `CamInputBridge::buildJobInput` 首次构造输入时打印一次，启动路径尚未接入桥接层 | M2 接入作业时必须保留该调用 |
| 字段变更漏升版本 | 消费方按旧布局访问新结构，可能越界或读到错位数据 | 任何公开结构体字段增删改（含顺序调整）必须递增 `CAM_ABI_VERSION`，并同步 `camAbiInfo()` 与 `AbiSelfCheck_MatchesPublicStructSizes` |
| 对外 C ABI | 已否决，不再作为后续路线 | 若出现仓库外消费方，重新立项评估 |

---

## 2. 工艺参数：单一来源

### 2.1 现状与证据

现有 **3 份完整定义 + 2 组平行字段集**：

| 定义 | 位置 | 差异 |
|---|---|---|
| `Eg::ProcessParams` | `Engine/Common/Include/Engine/Process/ProcessParams.h:59-145` | 基准：power 50 / freq 20000 / pulseWidth 0 / waveform 0（`:62-65`）、speed 100（`:68`）、rapid 0（`:69`）、passes 1（`:70`）、fillEnabled/fillInterval/fillAngle/fillMode（`:73-76`）、kerfMm（`:82`）、focusOffsetMm（`:84`）、enabled（`:87`）；无 `chordToleranceMm`；校验走 `valid()`→`invalidReason()`（`:95-138`） |
| `Cam::ProcessParams` | `CAM/CAM/Include/CAM/ToolpathJobInput.h:151-186` | power 50 / freq 20000 / pulseWidth 10 / waveform 0（`:156-159`）、speed 200（`:162`）、rapid 300（`:165`）、passes 1（`:168`）、**额外 `chordToleranceMm`**（`:171`）、enabled（`:175`）；**缺** fill 四项、`kerfMm`、`focusOffsetMm`；`valid()` 只查 power/speed/rapid/passes/chord（`:178-185`） |
| `FileIO::AprProcessParamsInfo` | `FileIO/FileIO/Include/FileIO/AprDocument.h:58-73` | **缺** `focusOffsetMm`、`chordToleranceMm`；`fillMode` 为 `int32_t`（`:70`）；字段顺序不同；注释声称"与 `Eg::ProcessParams` 一一对应"（`:56`） |
| `Cam::FillRegion` | `ToolpathJobInput.h:104-111` | 自带 `scanAngleDeg`（`:109`）/`lineSpacing`（`:110`），与 `Eg` 的 `fillAngleDeg`/`fillIntervalMm` 平行 |
| `Cam::KerfParams` | `ToolpathJobInput.h:190-196` | 自带 `widthMm`/`enabled`/`side`（`:193-195`），与 `Eg` 的 `kerfMm`（`:82`）平行 |

漂移已在代码里发生：
- 桥接层只转换 9 个字段：`Main/Src/CAM/CamInputBridge.cpp:64-87`（power/freq/pulseWidth/waveform/speed/rapid/passes/enabled/chord），**fill 四项、kerf、focus 全部丢失**，且无任何提示。
- `Cam::ProcessParams::valid()`（`ToolpathJobInput.h:178-185`）不校验 fillInterval，而 `Eg::ProcessParams::invalidReason()`（`ProcessParams.h:107-138`）会校验——同一份参数在两套规则下可能"Engine 判非法、CAM 判合法"。
- FileIO 逐字段手抄：读 `AprDocument.cpp:300-313`、写 `:330-343`；`focusOffsetMm` 两个方向都不存在。
- 覆盖关系未定义：`LayerBucket` 同时持有 `params` 与 `kerf`/`leadInOut`/`pierce`/`corner`/`powerCurve`/`passStrategy`（`ToolpathJobInput.h:270-278`），但"`kerf.enabled=true` 与 `params.kerfMm` 谁覆盖谁"没有规则。

**复核修正**：审查意见中"`Cam::ProcessParams` 缺 `enabled`"不成立——它在 `ToolpathJobInput.h:175`，桥接层 `CamInputBridge.cpp:82` 已转换，`CamCore.cpp:144` 已按它跳过图层。

### 2.2 问题

1. 同一物理量在三套结构里默认值不同（speed 100 vs 200、pulseWidth 0 vs 10、rapid 0 vs 300），导致 v1 路径与 CAM 路径烧出不同速度/脉宽。
2. 新增字段需在 3 处手抄，漏一处即静默丢失（`focusOffsetMm` 已经丢了）。
3. `FillRegion`/`KerfParams` 与图层参数平行，覆盖语义无法从类型上读出。
4. 校验规则两套，UI 与编译器可能给出不同结论。

### 2.3 方案与取舍

#### 方案 1（推荐）：抽出 header-only 中立契约目标 `ProcessContract`

- 新目标 `Engine/ProcessContract/`（`INTERFACE` 库，无源文件、无 DLL、无 Qt），头文件 `Include/Process/ProcessParams.h`，类型放 `Proc::` 命名空间，字段与 `Eg::ProcessParams` 完全一致。
- `Engine/Common/.../ProcessParams.h` 改为 `#include "Process/ProcessParams.h"` + `namespace Eg { using ProcessParams = Proc::ProcessParams; }`，现有 `Eg::ProcessParams` 调用点零改动。
- `FileIO` 直接用 `Proc::ProcessParams` 替换 `AprProcessParamsInfo` 字面字段（DTO 退化为别名），删除 `AprDocument.cpp:300-313`、`:330-343` 手抄。
- `CAM` 公开输入使用 `Proc::ProcessParams`，删除 `Cam::ProcessParams`；`cam.kerf` 删除，割缝由 `ProcessParams.kerfMm` + 轮廓绕向决定；`FillRegion` 只保留边界环，扫描参数取 `fillAngleDeg`/`fillIntervalMm`。
- `chordToleranceMm` 不属于工艺参数，移到 `ToolpathJobInput` 的全局设置（它当前是 CAM 单方面新增字段，`ProcessParams.h` 没有）。
- M1 专属的 `LeadInOutParams`/`PierceParams`/`CornerParams`/`PowerCurve`/`PassStrategy` 合并为 `ProcessTransformParams` 一个聚合结构，避免与基础参数平行散落。

依赖关系：

```
EngineCommon ─┐
Engine2D     ─┼─▶ ProcessContract（header-only）
FileIO       ─┤
CAM          ─┘
CAM ──▶ Utility（PUBLIC）、Log（PRIVATE）（不变）
```

与规则 4.5 的相容性：
- `CMake/CheckDependencies.cmake:129-139` 用正则列出禁止目标（`EngineCommon|Engine2D|...`），`ProcessContract` 不在其中，**配置天然通过**；
- `INTERFACE_INCLUDE_DIRS` 泄漏检查正则为 `[/\\](Engine2D|EngineCommon|...)[/\\]Include`（`:144`），新路径 `ProcessContract/Include` 不匹配，**通过**；
- 只做规则注释层面的允许清单更新（明确"CAM 允许依赖 ProcessContract"），不新增逻辑分支。

优点：类型唯一；无 DLL 依赖；调用点零改动（别名）；规则 4.5 无需改逻辑。

代价：新增一个 CMake 目标并在 4 个模块加 include 依赖；类型真实命名空间从 `Eg` 变为 `Proc`，所有 DLL 需全量重编（符号名变化）；`.apr` 文件布局不变。

#### 方案 2：放进 `Utility`

`Utility/Utility/Include/Ut/` 已有 `SceneEnvironmentSettings.h`（含 `TableSettings`），CAM 已 PUBLIC 依赖 `Utility`（`CAM/CAM/CMakeLists.txt:47-48`），零规则改动。

否决原因：`Utility` 定位是数学/平台工具（`Vec`/`Mat`/`Color`/`FileUtils`），塞入工艺参数会让最低层承载业务语义，`Raster`/`Vision` 等也会被迫看到工艺概念。

#### 方案 3：保留多份 + 字段集合测试钉住

做法：保留三份，加 `static_assert(std::is_standard_layout_v<T>)`、`offsetof`/`sizeof` 断言，加"逐字段往返"测试。
否决原因：仍是三份定义，新增字段时"忘了加测试"就静默丢失；测试只能覆盖已写字段。

#### 方案 4：CAM 直接依赖 `EngineCommon`

否决原因：直接违反规则 4.5（`CheckDependencies.cmake:130-139`），并让 CAM 变成 Engine 下层，破坏"独立顶层模块"定位。

### 2.4 改动范围

- 新增 `Engine/ProcessContract/CMakeLists.txt` 与 `Include/Process/ProcessParams.h`
- `Engine/Common/CMakeLists.txt`、`ProcessParams.h` 改为别名
- `FileIO/FileIO/AprDocument.h`、`Src/AprDocument.cpp`
- `CAM/CAM/Include/CAM/ToolpathJobInput.h`
- `Main/Src/CAM/CamInputBridge.cpp`
- `CMake/CheckDependencies.cmake`（仅注释）

### 2.5 迁移步骤

1. 建 `ProcessContract`，按现有字段名/默认值复制。
2. `Eg` 侧改别名，全量编译（预期零调用点改动）。
3. `FileIO` 用同一类型替换 DTO，删除两份手抄。
4. `CAM` 输入改用 `Proc::ProcessParams`，删除 `KerfParams`，`FillRegion` 瘦身，`chordToleranceMm` 移到全局设置。
5. 桥接层删除参数转换函数。
6. 加测试：`Eg::ProcessParams` → `.apr` 往返 → `Proc::ProcessParams` 全字段相等（含 `focusOffsetMm`）。
7. 加门禁测试：显式 `visitFields` 遍历全部字段，防止新增字段后 `FileIO` 漏映射。

### 2.6 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 类型身份变化导致全量重编 | 别名后真实命名空间变化，mangled name 变化 | 一次性全量重建，不做增量发布 |
| `FileIO` 公开头变化 | 若有外部 `.apr` 插件 | 当前无外部插件，文档记录 |
| `FillRegion` 瘦身影响 M0 测试 | `CamCoreTests.cpp` 未使用填充参数 | 低风险，测试同步 |
| 校验规则合并后行为变化 | `Eg::invalidReason` 比 `Cam::valid` 严格 | 统一以 `Eg` 规则为准，UI 与 CAM 共用 |

---

## 3. `CamPlan` 改为有序 op 流

### 3.1 现状与证据

- `PlanPathSegment` 没有功率/速度覆盖字段：`CamPlan.h:37-61`（字段仅 `type`/三点/半径/两角度/方向/closed/isRapid，`:47-60`）。
- `PlanLayerResult` 只复制 5 个参数：`CamCore.cpp:353-357`（power/freq/pulseWidth/speed/rapid）；`PlanLayerResult` 声明 `CamPlan.h:90-108`，无 `waveformIndex`。
- 没有 `Dwell`/`LaserOn`/`LaserOff`：全头文件只有 `isRapid`（`CamPlan.h:60`）。
- `Hw::MotionPlan` 已有完整 opcode：`MotionPlan.h:44-74`（`RapidTo`/`LineTo`/`ArcTo`/`Dwell`/`LaserOn`/`LaserOff`/`SetLaserParams`/`SetFeedRate`/`SetRapidRate`/`SetDigitalOut`/`WaitDigitalIn`/`WaitMotionDone`/`BeginLayer`/`EndLayer`/`BeginPass`/`EndPass`/`Comment`），载荷 `:86-121`。
- `CamPlan` 当前无消费方：全仓库仅 `CamPlan.h`、`CamCore.cpp`、`CamCore.h`、`CamCoreTests.cpp` 命中，未实现预览或发射器。

### 3.2 问题

1. 功率曲线、尖角减速需要"逐段功率/速度覆盖"，现有结构无处安放。
2. 穿孔本质是 `Dwell` + `LaserOn/Off`，不能用"零长度出光直线"冒充；伪段把几何真值与指令真值混在一起。
3. `PlanLayerResult` 的参数副本是"部分复制"，新增参数必然漏（`waveformIndex` 已漏）。
4. 预览若消费 `CamPlan`、设备由发射器再翻译，会出现两套解释逻辑，预览与实际加工不同步。

### 3.3 方案与取舍

#### 方案 1（推荐）：`CamPlan` 输出为有序 op 流

CAM 自有、设备无关的 op：

```cpp
// 示意，最终以头文件为准；标识符英文，注释中文
enum class CamOpCode : int32_t
{
    RapidTo = 0,      // 空移定位，不出光
    LineTo = 1,       // 直线切削
    ArcTo = 2,        // 圆弧切削
    LaserOn = 3,
    LaserOff = 4,
    Dwell = 5,        // 停留（穿孔/起停延时）
    SetLaser = 6,     // 功率/频率/脉宽/波形
    SetFeed = 7,      // 进给速度
    SetRapid = 8,     // 空移速度
    BeginLayer = 9,
    EndLayer = 10,
    BeginPass = 11,
    EndPass = 12,
    Comment = 13,     // 调试/黄金文件
};

struct CamOp
{
    CamOpCode code = CamOpCode::RapidTo;
    CamSegment geometry;          // 仅运动类 op 有效（见第 4 条）
    LaserParams laser;            // SetLaser 使用
    double rate = 0.0;            // SetFeed/SetRapid 使用
    int64_t durationUs = 0;       // Dwell 使用
    int32_t arg = 0;              // BeginLayer/BeginPass 序号
    uint64_t sourceEntityId = 0;  // 追溯与预览高亮
    char text[64] = {};           // Comment
};

struct CamPlan
{
    std::vector<CamOp> ops;       // 唯一有序真值
    // 统计与包围盒（用户坐标）
    double totalCuttingLengthMm = 0.0;
    double totalRapidLengthMm = 0.0;
    double estimatedSeconds = 0.0;
    double boundsMinX = 0.0, boundsMinY = 0.0, boundsMaxX = 0.0, boundsMaxY = 0.0;
};
```

内部仍保留"图层 → 轮廓 → 段"的工作树（私有、不导出），供割缝/引线/排序/共边使用；**公开输出只有 op 流**，线性化在编译末尾一步完成。

- **预览**：按 op 画折线；`RapidTo` 灰色虚线、`LineTo/ArcTo` 按图层分色、`Dwell` 画停留标记；`sourceEntityId` 支持点选高亮。预览消费的就是即将下发的同一份 ops。
- **发射器**：在 `Main` 侧把 `CamOp` 1:1 映射到 `Hw::MotionCommand`，只增加两件事——用户坐标 → 机器坐标、设备能力降级（`ArcTo` → 直线）。CAM 只表达工艺意图，不认识任何厂商能力，因此不引入设备耦合。
- **功率曲线/尖角减速**：在对应位置插入 `SetLaser`/`SetFeed`，后续运动 op 继承当前参数，不需要给每段加覆盖字段。
- **穿孔/吹气/等待**：`Dwell` 与 `SetDigitalOut`/`WaitDigitalIn` 按需插入；后者属于设备 IO，可在 M2 再补，由发射器决定是否降级。

优点：单一有序真值，预览与执行同源；所有 M1/M2 工艺都是"往流里插 op"；`BeginLayer/BeginPass` 让发射器在正确位置下发参数，不再需要部分复制；统计在编译期累加。

代价：输出结构完全重写，`CamCore.cpp:267-530` 的输出段与测试用例需重写；预览需从 op 流重建折线（当前无预览实现，无迁移负担）；`CamOp` 与 `Hw::MotionOp` 概念重复，需要映射完整性测试钉住。

#### 方案 2：保留几何树，给 `PlanPathSegment` 追加覆盖字段与伪 op

否决原因：`Dwell` 无几何，只能做成"零长度线段"；`LaserOn/Off` 只能靠布尔；穿孔多次停留需要多条伪段。这会把几何真值与指令真值重新揉在一起，正是第 4 条要消除的问题。

#### 方案 3：CAM 直接输出 `Hw::MotionPlanBuilder`

否决原因：
- 公开头需 include `Hardware/Device/MotionPlan.h`，`Hw` 类型泄漏给所有 CAM 消费方，违反规则 4.5 的精神（`CheckDependencies.cmake:127-128` 明确"CAM 任何对 Hardware 头的 include 都说明耦合破了"）；
- 预览需链接 Hardware，UI 侧依赖变重；
- 设备能力降级挤进 CAM，新增一款卡就要改 CAM，违背"新增硬件 = 新写适配器"。

### 3.4 改动范围

- `CAM/CAM/Include/CAM/CamPlan.h`（整体替换）
- `CAM/CAM/Src/CamCore.cpp`（输出段重写，约 150 行）
- `CAM/CAM/Test/CamCoreTests.cpp`（改为断言 op 序列）
- `Main/Src/CAM/`（新增 `CamPlanEmitter`，M2 落地）

### 3.5 迁移步骤

1. 冻结"内部工作树 → op 流"的线性化接口（私有头，如 `Src/Emit/OpLinearizer.h`）。
2. `CamPlan` 替换为 op 流；`CamCore` 改为"处理完统一线性化"（便于 M1 变换）。
3. 重写测试：正方形 → `[RapidTo?, LineTo×4]`；圆弧 → `[ArcTo]`；M1 穿孔 → `[RapidTo, LaserOff, Dwell, LaserOn]`。
4. 黄金文件：新增 `opsToText()`（对齐 `motionPlanToText`），文本比对锁定顺序。

### 3.6 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| `CamOp` 与 `Hw::MotionOp` 漂移 | 两边独立演进 | `Main` 侧加枚举全覆盖映射测试 |
| op 流体积 | 与段数量同阶，不算额外爆炸 | 单 op 载荷瘦身（见第 6 条） |
| 丢失轮廓层级 | 公开输出不再有轮廓分组 | 这些只服务编译期变换；预览需要分组时补 `BeginContour/EndContour` |
| 变换在树上、输出在流上，两处逻辑不同步 | 变换结果必须最终线性化 | 规定"只有线性化器写 ops"，变换模块只改树 |

---

## 4. 段表示：消除两套真值

### 4.1 现状与证据

- 输入段同时存点、圆心、半径、起止角、方向、闭合：`ToolpathJobInput.h:48-76`（`ptStart`/`ptEnd` `:60-61`，`ptCenter`/`dRadius`/`dStartAngle`/`dEndAngle` `:64-67`，`counterClockwise` `:72`，`closed` `:75`）。
- `Contour.closed`（`:86`）与 `PathSegment.closed`（`:75`）重复；输出侧同样重复：`CamPlan.h:70` 与 `:56`。
- 输入/输出几乎同构，靠手写逐字段拷贝：`CamCore.cpp:95-111` 的 `toPlanSegment`；桥接层还有一次 9 字段拷贝：`CamInputBridge.cpp:33-62`（逐字段在 `:46-58`）。
- `PathSegment.closed` 在 CAM 内除被拷贝外没有用途：`CamCore.cpp:108` 仅赋值。

**角度单位矛盾（新发现，已造成实际缺陷）**：
- CAM 的段注释写"度"：`ToolpathJobInput.h:66-67`。
- 抽取器实际按**弧度**存储：`GeometryExtractor.cpp:140-147` 直接把角度喂给 `cos`/`sin`；整圆用 `M_PI`/`kTwoPi`（`:157-167`）；圆弧提取传入 `arc->dStartAngle`（`:453`）。
- `AprArc` 的角度也是弧度：`AprArc.cpp:99-100`（用 `2π` 判整圆）。
- `CamCore.cpp:63-72`（以及 `:82-91` 的另一重载）按**度**计算圆弧长度：`dRadius * delta * (M_PI / 180.0)`。
- 抽取器自己的 `contourLength` 按弧度计算：`GeometryExtractor.cpp:171-180`。
- 测试用"度"构造圆弧（`CamCoreTests.cpp:230-236`），因此测试通过；真实桥接数据是弧度，长度差约 57.3 倍。

### 4.2 问题

1. 冗余字段没有一致性校验，"烧反方向"无法在类型层排除。
2. 单位歧义已经造成真实错误（长度计算）。
3. 输入/输出/引擎几何三份相同字段，各自手抄，新增字段必然漏。

### 4.3 方案与取舍

#### 方案 1（推荐）：最小充分表示 + 派生函数 + 校验

```cpp
// 示意；标识符英文，注释中文
struct CamSegment
{
    enum class Type : int32_t { Line = 0, Arc = 1 };

    Type type = Type::Line;
    Ut::Vec2d ptStart;              // 行进起点（用户坐标，mm）
    Ut::Vec2d ptEnd;                // 行进终点
    Ut::Vec2d ptCenter;             // 仅 Arc：圆心
    bool counterClockwise = true;   // 仅 Arc：行进方向

    // 派生量，不存储，杜绝与点/圆心矛盾
    double radius() const;              // |ptStart - ptCenter|
    double startAngleRad() const;
    double endAngleRad() const;
    double sweepRad() const;            // 按方向归一化到 (0, 2π]
    double length() const;              // Line 两点距离；Arc = radius * |sweep|

    // 两端点到圆心距离必须相等；起终点重合的"整圆"判为非法
    bool validate(double epsMm, const char** whyNot) const;
};
```

规则：
- **弧度是唯一内部单位**；只在日志/UI 显示时转度。
- 整圆不表示为"起终点重合的 Arc"：沿用抽取器现有做法拆成两段 180° 圆弧（`GeometryExtractor.cpp:157-167`）。
- `CamSegment::closed` 删除；闭合只由 `Contour::closed` 表达。
- `dRadius`/`dStartAngle`/`dEndAngle` 不再存储，需要时调用派生函数。

优点：只有四个独立事实，其余全派生，矛盾不可表达；单位唯一；结构从约 88 字节降到约 56 字节（估算，实测见第 6 条）。

代价：抽取器、桥接层、`CamCore`、测试全部要改；椭圆/样条仍以折线输出（现状不变）。

#### 方案 2：点 + 圆心 + 方向 + 扫角（不存终点）

否决原因：终点是桥接层与预览最常直接读取的量，处处派生会重复 `sin/cos`；扫角在整圆处仍有 0/2π 歧义。

#### 方案 3：保留现有 7 字段 + 一致性校验

否决原因：冗余仍在，校验只能事后发现矛盾；单位歧义仍在。

### 4.4 输入/输出结构收敛

与第 2 条同一机制：把 `PathSegment`/`Contour` 下沉到中立契约目标（可与 `ProcessContract` 合并为同一 header-only 目标），`Eg::Cam::PathSegment`/`Contour` 改为别名。

收敛后：
- `GeometryExtractor` 直接产出共享类型（Engine2D 依赖契约，合法）；
- 桥接层 `convertContour`（`CamInputBridge.cpp:33-62`）删除，改为 `std::move(extracted.contours)`（顺带解决第 6 条拷贝）；
- `CamCore::toPlanSegment`（`CamCore.cpp:95-111`）删除：输入与输出使用同一段类型，op 流直接携带；
- Engine2D 加字段时 CAM 自动获得。

被否方案：CAM 直接 include `Engine2D/Cam/ToolpathTypes.h`（违反规则 4.5）；Engine2D 反向 include CAM（违反规则 1）。

### 4.5 改动范围

- `Engine/2D/Include/Engine2D/Cam/ToolpathTypes.h` → 移到契约目标 + 别名
- `Engine/2D/Src/Cam/GeometryExtractor.cpp`（单位显式化为弧度）
- `CAM/CAM/Include/CAM/ToolpathJobInput.h`、`CamPlan.h`
- `CAM/CAM/Src/CamCore.cpp`（删除手抄、修正长度计算）
- `Main/Src/CAM/CamInputBridge.cpp`
- `CAM/CAM/Test/CamCoreTests.cpp`

### 4.6 迁移步骤

1. 定义规范 `CamSegment` 与派生函数，加单元测试：顺/逆时针弧、整圆拆分、退化弧，断言 `validate` 与 `length`。
2. 抽取器改用规范类型，注释单位改为弧度。
3. 桥接层改为直接 move。
4. `CamCore` 删除 `toPlanSegment`，长度改用 `CamSegment::length()`。
5. 交叉验证：同一组圆弧，"抽取器 `Contour.length`"与"CAM 统计长度"必须一致。

### 4.7 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 弧度/度在周边模块混用 | `MachineFrame::transformArc` 用度（`MachineFrame.h:155`） | 只在输出边界转换；CAM 内部只认弧度并在头文件唯一声明 |
| 整圆拆分假设变化 | 某些卡支持整圆 | 由发射器降级处理，不在几何契约处理 |
| 别名迁移不彻底 | 旧代码仍按 `Eg` 命名 | 全量编译 + 搜索残留 `Eg::Cam::PathSegment` |

---

## 5. 不可加工信息：绝不静默

### 5.1 现状与证据

- `ExtractResult` 有明确原因：`GeometryExtractor.h:92-110`（`reason` 在 `:108-109`；`Unsupported = 5` 在 `:83`）。
- CAM 桥接层对 `Unsupported` 无几何直接 `continue`：`CamInputBridge.cpp:227-239`（注释"有些 Unsupported 也会带 contour"，实际抽取器对条码/网格/未知类型只设 `reason`、不产出轮廓，见 `GeometryExtractor.cpp:870-885`）。
- `Container` 直接 `continue`：`CamInputBridge.cpp:224-226`。
- `Raster` 只改 kind、不保留 `reason`：`CamInputBridge.cpp:219-223`。
- 无效图元也在桥接层直接丢弃：`CamInputBridge.cpp:168`；选集过滤同样静默丢弃：`:171-174`。
- `CamResult` 只有计数、没有清单：`CamContext.h:59-76`（`skippedEntityCount`/`unmachinableEntityCount` 在 `:74-75`）。
- `failOnUnmachinable` 只覆盖 `Fillable/Raster`：`CamCore.cpp:456-468`（`Container`/`Unsupported` 根本进不了 CAM）。
- 对比 v1 编译器：它保留明细清单，`MotionPlanCompiler.cpp:237-242`（带 `geometryKindName` 与 `extracted.reason`）；字段 `ToolpathJobSpec.h:133-138`（`QStringList unmachinableDetails`）。

**复核修正**："`ExtractResult.reason` 从未被读"就全仓库而言不成立——`MotionPlanCompiler.cpp:242` 读了；就 CAM 链路而言成立——`CamInputBridge.cpp` 全文未读 `reason`。

### 5.2 问题

1. 用户看到"编译成功"，实际少烧了条码/网格/未知图元；`failOnUnmachinable` 默认 true 也不触发，因为这些图元没进 CAM。
2. 只有计数，无法定位"哪个图元、为什么"，现场无法补救。
3. 图层隐藏/锁定/不输出只累加计数（`CamCore.cpp:144-160`、`:365`），同样无法区分原因。

### 5.3 方案与取舍

#### 方案 1（推荐）：`EntitySnapshot` 带问题码，`CamResult` 带明细清单

```cpp
// 示意；标识符英文，注释中文
enum class CamIssueCode : int32_t
{
    NotSelected = 0,             // 选集过滤
    HiddenLayer = 1,
    LockedLayer = 2,
    OutputDisabled = 3,
    ProcessDisabled = 4,         // 工艺停用
    InvalidEntity = 5,           // 图元无效
    UnsupportedKind = 6,         // 抽取器明确不支持
    EmptyGeometry = 7,           // 类别支持但无几何
    RasterNotImplemented = 8,
    ContainerNotApplicable = 9,  // 容器无几何，不算不可加工
    DegradedToLines = 10,        // 可加工但被降级
};

struct CamIssue
{
    CamIssueCode code = CamIssueCode::UnsupportedKind;
    int32_t layerId = 0;
    uint64_t entityId = 0;   // 0 = 图层级/作业级
    int32_t sourceType = 0;  // EType 原值，便于界面分类
    char detail[96] = {};    // 来自 ExtractResult::reason
};

struct CamResult
{
    bool ok = false;
    CamError error = CamError::Ok;
    char errorText[256] = {};

    int32_t layerCount = 0;
    int32_t entityCount = 0;
    int32_t contourCount = 0;
    int32_t skippedEntityCount = 0;
    int32_t unmachinableEntityCount = 0;

    // 明细上限 256 条；超出只累加 overflow
    std::vector<CamIssue> issues;
    int32_t issueOverflowCount = 0;
};
```

配套规则：
- `EntitySnapshot` 增加 `Unsupported` 类别、`CamIssueCode issue`、`detail[]`；桥接层**永不 `continue`**（`Container` 也记录为 `ContainerNotApplicable`，但不计入"不可加工"）。
- 图层级跳过：每层写一条 `CamIssue`（`entityId=0`），`detail` 写"该层 N 个图元"；不逐图元复制，避免百万级实体撑爆清单。
- `failOnUnmachinable=true` 时，任何 `UnsupportedKind`/`RasterNotImplemented` 都使编译失败，并完整给出清单（与 v1 对齐）。
- `failOnUnmachinable=false` 时跳过但回传清单，UI 在结束提示中展示。

被否方案：
- 只把计数换成 `QStringList`：CAM 不能出现 Qt 类型，且字符串不可测试。
- `CamResult` 放 `std::map<CamIssueCode,int>`：无法定位实体。
- 回调实时上报：后台回调 UI 复杂，编译失败时清单仍需保存。

### 5.4 改动范围

- `ToolpathJobInput.h`（`EntitySnapshot` 扩展、`GeometryKind` 增加 `Unsupported`）
- `CamContext.h`（`CamIssue`/`CamResult`）
- `CamCore.cpp`（分类与清单收集）
- `CamInputBridge.cpp`（不再丢弃，保留 reason）
- `CamCoreTests.cpp`（新增条码/网格/位图/容器/隐藏层五类清单断言）

### 5.5 迁移步骤

1. 扩展 `EntitySnapshot`，桥接层对所有类别都产出快照。
2. `CamCore` 在各分支写入 `CamIssue`。
3. `failOnUnmachinable` 改为基于 `CamIssueCode`，不再只基于 `Fillable/Raster`。
4. 测试锁定"绝不静默"：任意不可加工输入 → `ok=false` 且清单非空、`entityId` 正确。

### 5.6 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 清单膨胀 | 百万图元全不可加工 | 上限 256 + overflow 计数 + 图层聚合 |
| `detail` 96 字节截断 | `reason` 较长 | 当前最长约 60 字符；截断策略写入文档 |
| 与 v1 诊断语义不一致 | 两套错误文本 | 统一 `CamIssueCode`，`detail` 只作补充 |

---

## 6. 性能：先量化，再决定

### 6.1 现状与证据

- 桥接层对全部几何深拷贝：`CamInputBridge.cpp:43-59`（`reserve` + 逐段构造 + `push_back`），且 `convertContour` 返回值再 `push_back`（`:199`、`:215`、`:233`），产生额外移动/拷贝。
- `EntitySnapshot` 无论类型都带两个容器与一个填充结构：`ToolpathJobInput.h:132`、`:135`、`:138`（`std::vector<Contour>`、`Ut::Vec2d`、`FillRegion`）。空 `std::vector` 不分配堆内存，但每个占 24 字节；`FillRegion` 内嵌再占约 48 字节（估算）。
- 已有场景/选择链路基准（`Tools/PerfBenchmark/main.cpp:1-24`，默认 20 万），并已新增 CAM 专项基准 `Tools/PerfBenchmark/CamBench.cpp`（`BUILD_CAM` 时编入，`APR_CAM_BENCH_1M=1` 才跑 1M 档）。

### 6.2 已实施基线（D7：只补 bench、零优化）

`CamBench` 测量三个阶段：`input_build`（桥接构造输入）、`input_copy_ctor`（整份输入拷贝构造）、`cam_compile`（`Cam::compile`）；`deep_copy_share` = (build + copy) / (build + copy + compile)。

条件：Release x64，10 万/1 万各重复 5 次、1M 档 3 次，取 best / median / p95；下表为 **best**（毫秒）：

| 规模 | `input_build` | `input_copy_ctor` | `cam_compile` | `deep_copy_share` |
|---|---|---|---|---|
| 10k | 2.10 | 1.38 | 4.86 | **44.6%** |
| 100k | 26.20 | 27.08 | 53.40 | **48.4%** |
| 1M | 349.13 | 439.18 | 896.70 | **46.7%** |

内存为容量估算（`camEstimateInputBytes` / `camEstimatePlanBytes`）：10k 输入 3.75 MB / 计划 4.13 MB；100k 37.54 / 42.87 MB；1M 375.37 / 398.24 MB，另有一次同量级临时拷贝，估算峰值约 1.1~1.2 GB（**估算值，进程实际峰值未测**）。

结论：深拷贝（构造 + 拷贝构造）占链路约 **45%~48%**，是 D2（统一类型 + move）的依据；bench 不接入生产路径，本轮不做任何优化。单结构 `sizeof` 以 `camAbiInfo()` 实际输出为准，本 bench 未单独记录。

### 6.3 方案与代价对比

| 方案 | 做法 | 收益 | 代价 | 结论 |
|---|---|---|---|---|
| **B. 统一类型 + move（D2 优先项）** | 第 4 条契约类型下沉后，桥接层 `std::move(extracted.contours)`，删除二次手抄 | 消除一次全量字段拷贝；删除 3 处手抄漏字段风险 | 依赖第 2/4 条契约目标；类型身份变化需全量重编 | bench 显示深拷贝约占 45%~48%，优先做 |
| **A. 池化 + 索引** | `EntitySnapshot` 只存 `int32_t firstContour, contourCount` 指向共享 `std::vector<Contour>`；`FillRegion` 同理 | 降低每实体固定开销；消除每实体两个空容器 | 间接层，调试与生命周期更复杂；排序/移动后索引需重映射 | bench 显示元数据占比 > 30% 再做 |
| **C. SoA + span** | 段与实体分离为并行数组，编译按区间读取 | 缓存友好、峰值内存最低 | 全部消费点改写；预览/调试困难 | 仅 500 万以上实体考虑 |
| **D. 分块提交** | 每次只让 CAM 持有 1 个图层快照，编译完丢弃 | 峰值内存下降 | 跨层排序/全局统计需额外累积；预览仍需全量 ops；接口变流式 | 预览改为按需生成再做 |
| **E. 保持现状** | 不动 | 无 | 内存与拷贝随规模线性增长；仍有两套类型 | 不推荐，需先补齐 D2 |

### 6.4 改动范围与迁移步骤

1. ✅ 写 bench，产出耗时与内存基线（`Tools/PerfBenchmark/CamBench.cpp`）。
2. [ ] 落地第 2/4 条契约目标，桥接层改 move（D2）。
3. [ ] 复测：若深拷贝占比仍 > 50% 或内存不可接受，再评估方案 A。
4. [ ] 方案 A 实施前先加"实体→轮廓"生命周期不变量测试。

### 6.5 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 过早优化 | 没有真实规模数据就改结构 | 强制"先 bench 再改" |
| move 后源对象被误用 | move 了 `extracted` 又读 `reason` | 先取 `reason` 再 move |
| 池化索引在排序后失效 | 方案 A | 排序只重排实体引用，不重排池 |

---

## 7. 坐标变换与文档漂移

### 7.1 坐标：钉死"用户坐标 → 机器坐标"的位置

现状证据：
- `CamPlan.h:19-22` 写"所有坐标（段端点、圆心、穿孔点、包围盒）都是用户坐标……CAM 全程只处理用户坐标"，变换由 Main 侧发射器在 M2 一步完成。
- `ToolpathJobInput.h:24-26` 写"坐标系：用户空间……CAM 全程只处理用户坐标，不做任何变换；'用户坐标 → 机器坐标'由 Main 侧发射器在 M2 一步完成"。
- `Docs/CAM层架构规划.md:375` 的包围盒注释已改为用户坐标（本轮同步）。
- `MachineFrameSnapshot` 与 `ToolpathJobInput::machineFrame` 已整体删除：全仓检索确认无生产读取；当前 `ToolpathJobInput.h` 与 `CamInputBridge.cpp` 均无 `machineFrame`。
- 机器坐标系参数化差异决定了 CAM 不应持有第二套坐标系模型：`Eg::MachineFrameConfig`（`MachineFrame.h:32-42`：`corner` + `tableW`/`tableH` + `enabled`）需要台面尺寸才能表达镜像平移，CAM 快照无法等价复刻。

**决定：CAM 全程只处理用户坐标；机器坐标变换只发生在 Main 的发射器一步。**

| 阶段 | 坐标系 | 责任方 |
|---|---|---|
| 场景 / 预览 | 用户坐标 | Engine2D / UI |
| `ToolpathJobInput` | 用户坐标 | `CamInputBridge`（不做任何变换） |
| `CamPlan.ops` / 统计 / 包围盒 | 用户坐标 | CAM |
| `CamOp → Hw::MotionCommand` | **在发射器内一次性变换** | `Main`（`Eg::MachineFrame` 唯一实现，`toMachine` 见 `MachineFrame.h:100-110`） |
| `Hw::MotionPlan` / 设备 | 机器坐标 | Hardware |

理由：
- 预览必须与场景同坐标系，否则要走反向变换，多一次且容易错；
- CAM 依赖 `Engine2D` 的 `MachineFrame` 会违反规则 4.5；
- 反射会反转圆弧方向（`MachineFrame.h:155`），应与"离散化/降级"一起做，放在发射器最合适——与 v1 既有做法一致（`MotionPlanCompiler.cpp:74-81` 的 `FrameTransform`、`:409` 建帧）。

配套动作：
- ✅ 已删除 `ToolpathJobInput` 的 `MachineFrameSnapshot` 与 `machineFrame` 字段；若未来 CAM 需要台面约束，以"用户可选约束"的形式加入，不要再造第四个坐标系模型。
- `CamPlan` 包围盒保持用户坐标，两份头文件已写实；`Docs/CAM层架构规划.md:386` 待改。
- 发射器在变换后重算 `MotionPlanHeader` 包围盒（`MotionPlan.h:126-137`），保证软限位预检用机器坐标（M2 落地）。

### 7.2 完整漂移对照表

| # | 文档/注释位置 | 文档表述 | 代码现状 | 处理建议 |
|---|---|---|---|---|
| 1 | `Docs/CAM层架构规划.md:501` | `CamCompileResult` | `Cam::CamResult`（`CamContext.h:59-76`） | 改文档名 |
| 2 | `Docs/CAM层架构规划.md:514` | `compile(input, progress) -> CamCompileResult` | `compile(input, plan, progress, result)`（`CamCore.h:43-46`） | 改文档签名 |
| 3 | `Docs/CAM层架构规划.md:360-382` | `CamPlan` 内嵌 `Contour`/`LayerResult` | `Cam::PlanContour`/`PlanLayerResult`（`CamPlan.h:64-108`） | 改文档 |
| 4 | `Docs/CAM层架构规划.md:375` | 包围盒（机器坐标） | 用户坐标（`CamPlan.h:19-22`），且 CAM 不做变换 | 已改为用户坐标（本轮同步） |
| 5 | `Docs/CAM层架构规划.md:324-331` | `MachineFrameSnapshot` 作坐标快照 | 代码已整体删除（`ToolpathJobInput.h` 全文无 `machineFrame`，`CamInputBridge.cpp` 无赋值） | 已从文档示例中删除（本轮同步） |
| 6 | `Docs/CAM层架构规划.md:210-232` | `ProcessParams` 独立定义，无 `enabled` | 代码有 `enabled`（`ToolpathJobInput.h:175`），且与 `Eg` 漂移 | 单一来源后改文档 |
| 7 | `Docs/CAM层架构规划.md:289-295` | `RasterEngraveParams` | 代码中不存在 | 删除或标注未实现 |
| 8 | `Docs/CAM层架构规划.md:299-331` | 输入结构无 `structSize`、无 `emitTrailingLaserOff` | 代码无 `structSize`；有 `emitTrailingLaserOff`（`ToolpathJobInput.h:282`） | 补 `emitTrailingLaserOff` |
| 9 | `Docs/CAM层架构规划.md:416-418` | `CamBridge::extractEntity` | 不存在（桥接层只有 `buildJobInput`） | 删除或实现 |
| 10 | `Docs/CAM层架构规划.md:437`、`:496`、`:554` | `Progress.h` 文件 | 实际在 `CamContext.h:39-56` | 改文档 |
| 11 | `Docs/CAM层架构规划.md:69-90` | CMake 示例用 `apr_add_shared_library(... TARGET ... DIRECTORY ...)` 与 `PRIVATE_LINK_LIBRARIES Engine2D` | 实际签名 `apr_add_shared_library(target)`（`CMake/Utils.cmake:202-212`）；实际链 `Utility`(PUBLIC)+`Log`(PRIVATE)（`CAM/CAM/CMakeLists.txt:47-51`）；链 `Engine2D` 会被规则 4.5 判违规 | 改文档示例 |
| 12 | `Docs/CAM层架构规划.md:776`、`:802-820` | `CAMTests` 链接 Engine2D/Engraving/Hardware | 实际只链 `CAM` + gtest（`CAM/CAM/Test/CMakeLists.txt:14-19`） | 改文档 |
| 13 | `Docs/CAM层架构规划.md:874` | 明确"ToolpathJobInput 必须定义自己的 Contour/PathSegment 副本" | 与第 2/4 条单一来源方案冲突 | 改为共享契约类型 |
| 14 | `Docs/CAM层架构规划.md:511-513` | 已定案 C++ 内部 ABI，对外 C ABI 已否决 | `CAMExport.h:8-18` 同口径 | 已统一（本轮同步） |
| 15 | `Docs/CAM层架构规划.md:633-636` | 错误文本带 `%d`/`%f` 占位 | 实际为固定英文短句（`CamCore.cpp:197-207` 的 `makeResult`） | 改文档 |
| 16 | `CAM/CAM/Include/CAM/CAMExport.h:8-18` | ABI 定位与跨 DLL 前提 | 已写明 C++ 内部 ABI、四项同工具链前提，并明确公开头暴露 STL 容器 | 已修正 |
| 17 | `CAM/CAM/Include/CAM/ToolpathJobInput.h:20` | 角度单位为"度" | 抽取器写弧度（`GeometryExtractor.cpp:140-147`、`:157-167`、`:453`） | 见第 4 条 |
| 18 | `Main/Src/CAM/CamInputBridge.cpp:33-35`、`:46-59` | `Cam::ToolpathJobInput::Contour/PathSegment` | 头文件中不存在该嵌套类型（`Contour`/`PathSegment` 是 `Cam::` 级） | **`BUILD_CAM=ON` 时无法编译**，必须修复 |
| 19 | `CAM/CAM/Include/CAM/CamCore.h:15-16`、`CAM/CAM/Src/CamCore.cpp` | 头文件称"输入校验包含 `selectionOnly` 边界"；并暗示 `emitArcs`/`emitTrailingLaserOff` 参与编译 | `selectionOnly`/`emitArcs`/`emitTrailingLaserOff` 从未被读取；M1 参数同样未读取（`enabled`、隐藏/锁定、`failOnUnmachinable` 已生效，见 `CamCore.cpp:144`、`:152`、`:156`、`:167-168`、`:460`） | 未实现字段显式拒绝或删除 |
| 20 | `CAM/CAM/Include/CAM/ToolpathJobInput.h:28-34` | C++ 内部 ABI 与版本自检约定 | 与 `CAMExport.h:8-18` 同口径；字段变更递增 `CAM_ABI_VERSION` | 已按 D1 统一 |
| 21 | `Docs/CAM层架构规划.md:862` | CAM 的 `PUBLIC_LINK_LIBRARIES` "只能有 Log" | 实际 `Utility`(PUBLIC) + `Log`(PRIVATE)（`CAM/CAM/CMakeLists.txt:47-51`）；规则 4.5 明确允许 Utility/Log（`CheckDependencies.cmake:136`） | 改文档 |
| 22 | `Docs/CAM层架构规划.md:867` | "失败时静默跳过图元"是必须避免的反模式 | 桥接层有 4 处 `continue` 丢弃（`CamInputBridge.cpp:168`、`:224-226`、`:227-239`、`:219-223`） | 见第 5 条 |
| 23 | `CAM/CAM/Include/CAM/ToolpathJobInput.h:6`、`:147` | 称"中性 C++ 值类型，自持有数据""与 Engine ProcessParams 无关" | 结构仍含 `std::vector`/`std::string`，字段与 `Eg` 仍漂移 | ABI 表述已修正，参数单一来源见第 2 条 |

**复核修正**：审查意见中"根 `CMakeLists.txt` / `Config.cmake` 有陈旧 BUILD_CAM 注释""CAM CMakeLists 文档路径错误"不成立——`CMakeLists.txt:200-201`、`:252-262`、`Config.cmake:226`、`CAM/CAM/CMakeLists.txt:10` 均与现状一致。

### 7.3 改动范围

- 文档：`Docs/CAM层架构规划.md`（按 7.2 表）
- 代码：`ToolpathJobInput.h`（删 `MachineFrameSnapshot`）、`CamInputBridge.cpp`、`CamPlan.h` 注释
- 不涉及 CMake 逻辑

### 7.4 迁移步骤

1. ✅ 删除 `MachineFrameSnapshot` 与 `machineFrame`，桥接层不再填。
2. [ ] 发射器实现坐标变换（M2 落地），并写黄金测试：镜像台面下圆弧方向翻转、包围盒正确。
3. 修正文档 1-20 项。

### 7.5 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 预览用机器坐标导致与场景错位 | 若按旧文档实现 | 文档与头文件双处钉死"用户坐标" |
| 删除 `machineFrame` 影响既有调用 | 已确认无生产消费方 | 全局检索确认后删除（已完成） |
| 变换在发射器遗漏 | 多入口（预览/导出/加工） | 只允许一个 `CamPlanEmitter` 入口 |

---

## 8. 需要用户拍板的选项

| 编号 | 选项 | 备选 | 推荐 | 状态 | 影响面 |
|---|---|---|---|---|---|
| D1 | CAM ABI 定位 | ① 真 C ABI（Raster 模式）<br>② 承认 C++ 内部 ABI | **②**，加同工具链启动自检；对外 C ABI 已否决 | **已实施** | 公开头、测试、启动流程 |
| D2 | 工艺参数单一来源落点 | ① 新 header-only `ProcessContract`<br>② 放进 `Utility`<br>③ 保留多份 + 字段测试<br>④ CAM 依赖 `EngineCommon` | **①**；③仅作过渡 | 待实施 | EngineCommon/FileIO/CAM/CMake |
| D3 | `CamPlan` 输出形态 | ① 有序 op 流<br>② 几何树 + 追加覆盖字段<br>③ 直接输出 `Hw::MotionPlanBuilder` | **①** | 待实施 | CamPlan.h、CamCore、预览、发射器 |
| D4 | 段规范表示 | ① 起点+终点+圆心+方向（派生半径/角度）<br>② 起点+圆心+方向+扫角<br>③ 保留现状 + 校验 | **①** | 待实施 | 抽取器、桥接、CAM、测试 |
| D5 | 坐标变换层 | ① 仅 Main 发射器（CAM 全程用户坐标）<br>② CAM 内部变换 | **①** | **已实施**（契约与字段已收口；M2 发射器变换待接入） | 桥接、发射器、文档 |
| D6 | 不可加工清单形式 | ① `std::vector<CamIssue>`（内部 C++ ABI）<br>② 固定数组 + 计数（可 POD 化） | **①**，字段设计保留 POD 化可能 | 待实施 | `CamResult`、UI 展示 |
| D7 | 性能优化顺序 | ① 先 bench → 统一类型+move → 按数据决定池化<br>② 直接上池化/SoA | **①** | **已实施（仅 bench，零优化）** | bench、桥接、结构 |
| D8 | `BUILD_CAM` 何时可默认开启 | ① 桥接层修复 + 契约重构落地后<br>② 立即开启并接受编译失败 | **①** | **已实施**（D2/D3/D4/D6 不阻塞默认开启） | 根 CMake、CI |

### 8.1 D1/D5/D7 已实施落点

**D1（C++ 内部 ABI）**
- `CAMExport.h:8-18` 写明 C++ 内部 ABI 的四项同工具链前提，并声明对外 C ABI 已否决。
- `CAMExport.h:34` 定义 `CAM_ABI_VERSION = 1`；`:39-51` 定义 `Cam::CamAbiInfo` 与 `Cam::camAbiInfo()`（6 个关键结构 `sizeof`），实现见 `CamCore.cpp:48-58`。
- 公开结构 `structSize` 删除 21 处（`ToolpathJobInput.h` 15、`CamPlan.h` 4、`CamContext.h` 2），实现侧 `CamCore.cpp` 7 处、`CamInputBridge.cpp` 7 处赋值同步删除。
- 原 `PodStructSizeGuards` 是空断言，已换成 `AbiSelfCheck_MatchesPublicStructSizes`（`CamCoreTests.cpp:126-139`，校验版本号与每个 `sizeof`）与 `PublicStructs_DefaultConstructToEmpty`（`:89-120`）；CAMTests 20 → 21。
- 自检日志：`CamInputBridge.cpp:48-68` 的 `logAbiSelfCheck()`，在 `buildJobInput` 首次构造输入时（`:307`）打印一次。

**D5（坐标收口）**
- `MachineFrameSnapshot` 与 `ToolpathJobInput::machineFrame` 整体删除；全仓检索确认无生产读取。
- `CamPlan.h:19-22` 与 `ToolpathJobInput.h:24-26` 写实"CAM 全程用户坐标，用户坐标 → 机器坐标由 Main 侧发射器在 M2 一步完成"。

**D7（只补 bench、零优化）**
- `Tools/PerfBenchmark/CamBench.cpp` 在 `BUILD_CAM` 时编入（`Tools/PerfBenchmark/CMakeLists.txt:36`），`APR_CAM_BENCH_1M=1` 才跑 1M 档。
- Release 实测 best：10k → `input_build` 2.10 / `input_copy_ctor` 1.38 / `cam_compile` 4.86 ms，`deep_copy_share` 44.6%；100k → 26.20 / 27.08 / 53.40 ms，48.4%；1M → 349.13 / 439.18 / 896.70 ms，46.7%。
- 内存容量估算：10k 输入 3.75 MB / 计划 4.13 MB；100k 37.54 / 42.87 MB；1M 375.37 / 398.24 MB，另有一次同量级临时拷贝，估算峰值约 1.1~1.2 GB（估算值，进程实际峰值未测）。
- 结论：深拷贝占链路约 45%~48%，是 D2（统一类型 + move）的依据。

**验证**：`BUILD_CAM=ON`（`build-cam-contract`）全部目标构建成功，CAMTests 21/21、MainTests 832/832；`BUILD_CAM=OFF` configure 通过且 `CamBench.cpp` 未参与编译；零新增告警。

### 8.2 D8 已实施落点

**D8（`BUILD_CAM` 默认开启）**
- 开关与接入：`Config.cmake` 中 `option(BUILD_CAM ... ON)`；根脚本按该开关接入 `CAM/CAM`。CI 预设通过全工程唯一测试开关 `BUILD_TESTS` 控制测试，未覆盖 `BUILD_CAM` 默认值。
- 前置条件 1（桥接层可编译，原附 2）：`CamInputBridge.cpp:80-107` 已改用 `Cam::Contour`/`Cam::PathSegment`，不再引用不存在的嵌套类型。
- 前置条件 2（D1 ABI 契约与自检）：`CAMExport.h:8-18` 写明同工具链前提，`Cam::camAbiInfo()` 实现见 `CamCore.cpp:48-58`，桥接层在 `CamInputBridge.cpp:48-68` 打印一次 ABI 自检（首次调用 `:307`）。
- 前置条件 3（D5 坐标收口）：CAM 全程用户坐标，机器坐标变换只留在 Main 发射器（`CamPlan.h:19-22`、`ToolpathJobInput.h:24-26`）。
- 构建验证：见上节 8.1 末尾的 `BUILD_CAM=ON` 全目标构建记录。
- 不阻塞默认开启、仍待实施的项：D2（工艺参数单一来源）、D3（`CamPlan` 有序 op 流）、D4（段规范表示）、D6（不可加工清单形式）。

---

## 9. 待同步清单（本文档之外的改动，供后续任务执行）

### 9.1 `Docs/CAM层架构规划.md`

- 按 7.2 表第 1-23 项修正：`CamCompileResult`→`CamResult`、`compile` 四参签名、`CamPlan` 内嵌类型、包围盒坐标系、`MachineFrameSnapshot`、`ProcessParams`、`RasterEngraveParams`、输入字段、`extractEntity`、`Progress.h`、CMake 示例、测试链接、"自己定义副本"的旧决策、C ABI 口径、错误文本示例。
- 本轮已同步坐标与 ABI 相关表述（第 4 项包围盒坐标系、第 14 项对外 C ABI）；其余仍待改。

### 9.2 代码内注释

- 已完成（D1）：`CAMExport.h:8-18` ABI 定位与 `CAM_ABI_VERSION`/`camAbiInfo()` 说明；`ToolpathJobInput.h:6`、`:28-34`；`CamPlan.h:29-32`；`CamContext.h:8-10`；`CamCore.h:11`。
- 已完成（D5）：`ToolpathJobInput.h:24-26`、`CamPlan.h:19-22` 写实用户坐标。
- 原"`structSize` 约定"这一条已删除：公开结构不再有该字段，版本一致性改由两个自检用例钉住——`AbiSelfCheck_MatchesPublicStructSizes`（`CamCoreTests.cpp:126-139`）与 `PublicStructs_DefaultConstructToEmpty`（`:89-120`）。
- 待 D4：`ToolpathJobInput.h:20` 仍写角度单位为"度"，与抽取器的弧度不一致。

### 9.3 CMake / 门禁

- `CMake/CheckDependencies.cmake:126-150` 规则 4.5 的允许清单文本追加 `ProcessContract`（逻辑无需改）；
- 根 `CMakeLists.txt` 增补 CAM 与宿主工具链一致性校验（D1 已定案为 C++ 内部 ABI，启动期比对尚未接入）。

### 9.4 测试

- `CAM/CAM/Test/CMakeLists.txt` 增加契约测试入口（bench 不进 CTest）；
- 新增：段一致性、角度单位、参数单一来源往返、问题清单、op 流黄金文件。
- 已完成：ABI 自检与公开结构默认构造两个用例（CAMTests 21）。

---

## 10. 未做项与风险

- D2/D3/D4/D6 未实施；本轮代码改动仅覆盖 D1/D5/D7。
- D1 第 4 步未完成：`Main` 启动路径未接入 `camAbiInfo()` 比对，自检只在 `CamInputBridge::buildJobInput` 首次构造输入时打印一次（`CamInputBridge.cpp:36-56`、`:133`）；M2 接入作业时必须保留该调用。
- 后续任何公开结构体字段增删改（含顺序调整）必须递增 `CAM_ABI_VERSION`，并同步 `camAbiInfo()` 与 `AbiSelfCheck_MatchesPublicStructSizes`；否则消费方按旧布局访问可能越界。
- D7 只补 bench、零优化：未实施"统一类型 + move"、池化、SoA、分块提交。
- 内存数字为容量估算（`camEstimateInputBytes`/`camEstimatePlanBytes`），进程实际峰值未测；1M 档 1.1~1.2 GB 峰值是加入同量级临时拷贝后的估算。
- `Docs/CAM层架构规划.md` 仍有 ABI / 坐标之外的实现漂移（见 §7.2 表其余项、§9.1），本轮只同步 ABI 与坐标相关表述。
- 未执行 `git commit` / `push`。
