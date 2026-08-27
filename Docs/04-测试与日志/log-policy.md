# 日志规范

## 概述

本文档定义项目的日志规范，包括日志级别、日志格式、日志分类和使用规则。

---

## 1. 日志级别

### 1.1 级别定义

| 级别 | 符号 | 用途 | 使用场景 |
|------|------|------|----------|
| **DEBUG** | SY_DEBUG | 调试信息 | 详细的执行流程、变量值、函数调用 |
| **INFO** | SY_INFO | 一般信息 | 正常的操作日志、状态变化、事件通知 |
| **WARNING** | SY_WARN | 警告信息 | 潜在问题、异常情况、性能警告 |
| **ERROR** | SY_ERROR | 错误信息 | 可恢复的错误、操作失败、数据异常 |
| **CRITICAL** | SY_CRITICAL | 严重错误 | 不可恢复的错误、系统崩溃、数据丢失 |

### 1.2 级别使用规则

| 场景 | 推荐级别 | 示例 |
|------|----------|------|
| 函数入口/出口 | DEBUG | `SY_DEBUG("Entering render()")` |
| 命令执行 | INFO | `SY_INFO("Command executed: Tool_Line")` |
| 参数校验失败 | WARNING | `SY_WARN("Invalid parameter: negative radius")` |
| 操作失败 | ERROR | `SY_ERROR("Failed to compile scene")` |
| 系统崩溃 | CRITICAL | `SY_CRITICAL("Memory allocation failed")` |

---

## 2. 日志格式

### 2.1 统一格式

所有日志必须遵循以下格式：

```
[模块名] [事件] [操作ID] [来源] [详情]
```

### 2.2 字段定义

| 字段 | 说明 | 示例 |
|------|------|------|
| **模块名** | 日志来源模块 | `OperationBus`, `SceneRefreshCoordinator`, `ToolManager` |
| **事件** | 事件类型 | `execute`, `compile`, `render`, `select` |
| **操作ID** | 操作标识 | `Tool_Line`, `Edit_Delete`, `View_ZoomFit` |
| **来源** | 触发来源 | `Menu`, `Toolbar`, `Shortcut`, `Gizmo` |
| **详情** | 详细信息 | 参数、结果、错误原因 |

### 2.3 格式示例

```cpp
// 命令执行（OperationBus）
SY_INFO("[OperationBus] execute op=%s source=%s result=%s",
    operationIdToString(id), operationSourceToString(source), result.message);

// 场景提交（SceneRefreshCoordinator）
SY_INFO("[SceneRefreshCoordinator] submit level=%d entities=%d",
    refreshLevel, entityCount);

// 视口刷新（RenderViewport2D）
SY_DEBUG("[RenderViewport2D] frame=%d entities=%d time=%dms",
    frameNumber, entityCount, renderTime);

// 错误处理
SY_ERROR("[OperationBus] execute failed op=%s error=%s",
    operationIdToString(id), errorMessage.c_str());
```

---

## 3. 日志分类

### 3.1 按模块分类（与当前代码日志标签一致）

| 模块 | 日志标签 | 主要内容 |
|------|----------|----------|
| **CADApplicationRuntime** | `[CADApplicationRuntime]` | 应用启动、初始化、关闭 |
| **AppBootstrapper** | `[AppBootstrapper]` | 组合根初始化、工作台创建、启动序列 |
| **ApplicationCompositionRoot** | `[ApplicationCompositionRoot]` | 服务组装、操作注册 |
| **InteractionDispatcher** | `[InteractionDispatcher]` | 命令执行、提交、取消、撤销、重做 |
| **SceneDocument2D** | `[SceneDocument2D]` | 文档操作（创建、删除、清空） |
| **WorkbenchWindow** | `[WorkbenchWindow]` | 主窗口创建、工作台切换、菜单构建 |
| **Workbench2D / Workbench3D** | `[Workbench2D]` / `[Workbench3D]` | 工作台装配与 3D 视图链路 |
| **OperationBus** | `[OperationBus]` | 命令执行、结果、状态变化 |
| **ToolManager** | `[ToolManager]` | 工具切换、激活/失活、状态变化 |
| **RenderViewport2D** | `[RenderViewport2D]` | 2D 视口宿主、相机、刷新 |
| **SceneRefreshCoordinator** | `[SceneRefreshCoordinator]` | 刷新调度、增量/全量提交 |
| **RenderWidget3D / RenderWidget3DAdapter** | `[RenderWidget3D]` / `[RenderWidget3DAdapter]` | 3D 渲染控件与适配层 |
| **Viewport3D / Renderer3DFactory** | `[Viewport3D]` / `[Renderer3DFactory]` | 3D 视口宿主与渲染器创建 |
| **ExportService / ImportService** | `[ExportService]` / `[ImportService]` | 导入导出主链路 |
| **ImportDispatcher / ExportDispatcher** | `[ImportDispatcher]` / `[ExportDispatcher]` | 格式路由与分发 |
| **导入读取器（基类统一）** | `[ImportReader:<格式名>]` | IR 解析耗时、图元/图层/群组统计、转换丢弃差额 |
| **FileIO DLL 入口** | `[FileIO]` | `importToIR` / `importFile` 的成败与统计 |
| **FileIO IR 投影层** | `[IrProjector]` | 解析结果 → IR 的守恒口径（`N parsed -> M entities`）与降级分类计数 |
| **FileIO 解析器** | `[DxfParser]` / `[SvgParser]` / `[PltParser]` / `[StepParser]` / `[UgParser]` / `[ObjParser]` / `[StlParser]` / `[NativeParser]` / `[PdfBasedParser:<格式名>]` | 单格式解析细节 |
| **FileIO 解析器工厂** | `[FileParserFactory]` / `[IFileParser]` | 解析器创建失败、格式未接入 IR |
| **FileManager** | `[FileManager]` | 文件管理、格式识别 |
| **SceneEditService3D** | `[SceneEditService3D]` | 3D 编辑事务 |
| **UiShellHost** | `[UiShellHost]` | Shell 宿主、工作台切换 |
| **UiWorkbench** | `[UiWorkbench]` | 工作台生命周期 |
| **SettingsService** | `[SettingsService]` | 设置存取 |
| **SelectionService** | `[SelectionService]` | 选择状态 |
| **ToolManager3D 相关** | `[OperationBus3D]` / `[CommandRegistry3D]` / `[ServiceLocator3D]` | 3D 操作总线、命令注册、服务定位 |

### 3.2 日志覆盖范围

#### 启动流程
- ✅ `CADApplicationRuntime`：应用启动、初始化、关闭
- ✅ `AppBootstrapper`：组合根初始化、工作台创建、启动序列

#### 命令生命周期
- ✅ `InteractionDispatcher` / `OperationBus`：命令执行、提交、取消、撤销、重做
- ✅ `OperationRegistry` / `PendingOp`：操作注册与占位操作
- ✅ `CommandActionHub`：工具激活快速路径

#### 文档操作
- ✅ `SceneDocument2D`：图元创建、删除、清空
- ✅ `SceneEditService3D`：3D 编辑事务

#### 工作台与 UI
- ✅ `WorkbenchWindow`：主窗口创建、工作台切换、菜单构建
- ✅ `UiShellHost`：Shell 与工作台切换
- ✅ `Workbench2D` / `Workbench3D`：工作台装配

#### 渲染管线
- ✅ `RenderViewport2D`：视口刷新、相机变化
- ✅ `SceneRefreshCoordinator`：增量 / 全量刷新调度
- ✅ `RenderWidget3D` / `RenderWidget3DAdapter`：3D 控件与适配
- ✅ `Viewport3D` / `Renderer3DFactory`：3D 视口与渲染器工厂

#### 工具系统
- ✅ `ToolManager`（UI/2D）：工具切换、激活/失活、状态变化
- ✅ `TextEditTool` / `TextInputTool` / `SelectTool`：具体工具行为
- ✅ `OperationBus3D` / `CommandRegistry3D`：3D 操作与命令注册

### 3.2 按流程分类

| 流程 | 日志内容 |
|------|----------|
| **命令流** | 命令注册、执行、结果、撤销 |
| **渲染流** | 编译、批次管理、绘制、刷新 |
| **交互流** | 工具切换、鼠标事件、键盘事件 |
| **选择流** | 选中、取消选中、高亮更新 |
| **文档流** | 增删改、事务、Undo/Redo |

---

## 4. 日志使用规则

### 4.1 必须记录的事件

| 事件 | 级别 | 必须包含 |
|------|------|----------|
| **命令执行** | INFO | operationId, source, result |
| **命令失败** | ERROR | operationId, source, error |
| **场景编译** | INFO | entityCount, batchCount, time |
| **渲染帧** | DEBUG | frameNumber, batchCount, time |
| **工具切换** | INFO | fromTool, toTool |
| **选择变化** | INFO | selectedCount |
| **文档保存** | INFO | path, size |
| **文档加载** | INFO | path, entityCount |

### 4.2 禁止记录的内容

| 禁止内容 | 原因 |
|----------|------|
| **敏感信息** | 密码、密钥、个人数据 |
| **大量重复** | 循环内高频日志 |
| **未格式化** | 难以 grep 和分析 |
| **无意义信息** | "entering function" 等无价值内容 |
| **硬编码字符串** | 应使用统一的字符串转换函数 |
| **本地化 / 用户可见文案** | 排查关键字会随部署语言变化，详见 §4.4 |

### 4.3 性能考虑

| 规则 | 说明 |
|------|------|
| **DEBUG 级别** | 生产环境默认关闭 |
| **频率限制** | 高频操作（如鼠标移动）只记录关键事件 |
| **日志轮转** | 自动轮转，保留最近 7 天 |
| **异步写入** | 日志写入不阻塞主线程 |

### 4.4 日志里禁止出现本地化文案，只打机器可读 id

日志文本一律英文（项目既有约定）。这条约定还有一个更强的推论：**凡是会被本地化、
或本来就是给用户看的文案，都不得进日志；日志只能打机器可读的稳定 id。**

原因有两条，都不是风格问题：

1. 本地化文案会随部署语言变化。中文部署里 `first='安全门未关闭'`，英文部署里同一条
   记录会变成另一串字符，于是同一个故障在不同现场无法用同一套关键字检索，
   `grep` / 日志聚合 / 告警规则全部失效。
2. 界面文案是可以随时改的（措辞调整、翻译返工）。一旦日志依赖它，改一句提示语
   就悄悄废掉了一条排查手段，而且没有任何编译期信号。

做法：凡是「同一件事既要给人看又要给机器看」的地方，就在数据结构里放两个字段，
一个进界面，一个进日志，并在注释里写明分工，避免后来人图省事直接打那个好看的。

**范例：安全裁决（`Hw::SafetyVerdict`）**

| 字段 | 内容 | 去处 |
|------|------|------|
| `firstViolation` | 条件的 `description`，给用户看的本地化文案（中文部署里就是中文） | 只进界面（状态栏单行提示、告警弹窗） |
| `firstViolationPoint` | 逻辑点位 id，恒为 ASCII，如 `safety.door_closed` | 只进日志与遥测 |

对应的日志形态：

```cpp
// Hardware/Hardware/Src/Device/SafetyMonitor.cpp
SY_ERRORF("[Hw::SafetyMonitor] State -> %s, actions=0x%02X, violations=%d, first=%s",
    safetyStateName(v.state), v.actions, v.violationCount, v.firstViolationPoint);
// 后续违反项同样只打点位名
SY_ERRORF("[Hw::SafetyMonitor]   also: %s", m_impl->violationPoints[i].data());
```

```cpp
// Main/Src/Hardware/DeviceHost.cpp
SY_WARNF("[DeviceHost] Safety state -> %s (violations=%d, first=%s)",
    Hw::safetyStateName(verdict.state), verdict.violationCount, verdict.firstViolationPoint);

// Main/Src/Hardware/ProcessingJobService.cpp
SY_ERRORF("[ProcessingJob] Safety violated during job '%s' (point=%s), pausing",
    m_impl->jobId.toUtf8().constData(), firstViolationPoint.toUtf8().constData());
```

不是所有违反都对应一个真实 IO 点位，这类情况填**稳定的合成 id**，不留空：

| 场景 | 合成 id |
|------|---------|
| 尚未做过任何一次 `evaluate`（构造后的初值） | `safety.not_evaluated` |
| IO 点位表缺失（设备未挂载） | `io.point_map` |
| 软件急停 | `safety.software_estop` |

留空会让 `first=` 变成空串，等于把「没有原因」和「原因是软件急停」两种情况打成同一行。

同类约定的其他落点：操作 ID 用 `Cmd::operationIdToString()`，安全等级用
`Hw::safetyStateName()` —— 都是「枚举 → 固定 ASCII 名」，不要在日志里拼界面文本。

---

## 5. 日志工具

### 5.1 日志宏

```cpp
// 基础宏
SY_DEBUG(format, ...)
SY_INFO(format, ...)
SY_WARN(format, ...)
SY_ERROR(format, ...)
SY_CRITICAL(format, ...)

// 带级别控制的宏
SY_DEBUGF(format, ...)  // 带函数名
SY_INFOF(format, ...)
SY_WARNF(format, ...)
SY_ERRORF(format, ...)
SY_CRITICALF(format, ...)
```

### 5.2 字符串转换

```cpp
// 操作 ID → 字符串
const char* Cmd::operationIdToString(OperationId id);

// 操作来源 → 字符串
const char* Cmd::operationSourceToString(OperationSource source);
```

### 5.3 日志配置

```cpp
// 设置日志级别
SyLogger::setLevel(SyLogger::Level::INFO);

// 设置日志回调
SyLogger::setCallback([](SyLogger::Level level, const char* msg) {
    // 自定义处理
});

// 输出到文件
SyLogger::setFile("app.log");
```

---

## 6. 排查流程

### 6.1 问题定位

```
1. 根据日志级别筛选
   grep "ERROR" app.log

2. 根据模块筛选
   grep "\[OperationBus\]" app.log

3. 根据操作 ID 筛选
   grep "op=Tool_Line" app.log

4. 根据时间范围筛选
   grep "2026-07-10 14:30" app.log

5. 追踪完整流程
   grep "traceId=xxx" app.log
```

### 6.2 日志分析工具

| 工具 | 用途 |
|------|------|
| **grep** | 文本搜索 |
| **awk** | 字段提取 |
| **sed** | 文本替换 |
| **tail** | 实时查看 |
| **logcat** | Android 日志 |

### 6.3 导入一个文件失败/结果不对时怎么查

导入链路跨了三层（Main → FileIO DLL → 具体 parser），日志按层分前缀，从外到内逐层收敛：

```
[ImportService]          五阶段主流程：格式识别 → 解析 → 构建文档 → 刷新显示 → 回写状态
[ImportDispatcher]       格式路由：命中哪个读取器、读取器耗时与结果
[ImportReader:DXF]       IR 解析统计、转换层丢弃差额、旧路径回退
[FileIO]                 DLL 入口：parser 是否存在、异常、零实体
[IrProjector]            投影层：解析结果 → IR 的守恒口径与降级分类计数
[DxfParser] 等           单格式解析细节：块展开、越界、上限截断
```

按现象定位：

- **提示导入成功但画布是空的**：先看 `[ImportService] Entity split`（网格与非网格各多少），
  再看 `Entity validation dropped`（有多少图元没通过合法性校验）。
- **图元比源文件少**：分两段查，**先看内层**。
  `[IrProjector] Projected <格式>: N parsed -> M entities` 是解析结果进 IR 的守恒口径，
  `parsed != entities` 说明图元在投影层就没了；紧跟的
  `[IrProjector] Degraded while projecting ...` 给出分类计数
  （`unknownType` 是整条丢弃，`blobOverflow`/`smartLineSkipped` 是退化成空壳图元），
  再往下一条 `First unknown geometry type: raw=.. (..), sourceId=..` 给首个样本，
  拿 `sourceId` 可以回原文件定位。
  外层 `[ImportReader:*] Converter dropped N of M IR entity(ies)` 里的 **M 是 IR 的
  `entityCount`**，也就是已经扣过投影层损耗了，所以只看这一行会漏掉一半原因。
- **导入后图元全挤在默认图层上**：看 `[IrProjector] Degraded ...` 的
  `missingLayerRef` / `missingGroupRef` —— 源文件引用了不存在的图层/群组，
  投影层落到「未分配 / 无群组」哨兵。
- **图层/群组没还原**：看 `[ImportService] Layer restore` / `Group restore` 两行的
  `created` 与 `entity mapping` 数；若打的是 `skipped`，行尾会写明原因
  （`LayerManager not set` / `preserveLayers=false` / `no 2D entity imported`）。
- **导入很慢**：`[ImportDispatcher] Reader '<格式>' succeeded: ... N ms` 是读取器总耗时，
  `[ImportReader:*] IR parsed in N ms` 是纯解析耗时，两者之差是转换层开销。
- **卡住不动**：把级别降到 DEBUG，看 `[ImportService] Progress: phase=..` 停在哪个阶段。

约定：每个 parser 的收尾日志文案统一为 `parseToIR END`，
所以 `grep "parseToIR END"` 能一次性捞出所有格式的解析统计。

`[IrProjector]` 的降级一律**按类别聚合计数 + 首个样本**，不逐条打：畸形文件里这类问题
每个图元都会触发一次，几万条同样的 WARN 会把日志冲爆。同理每个类别只往 `warnings` 塞
一条，所以 `FioParseResult::warningCount` 是**类别数**而不是受影响的图元数。
分类含义见 `FileIO/README.md` §6.3。

### 6.4 崩溃排查：日志尾部不可信，看调用栈

日志走 spdlog **async_logger** + `flush_on(warn)`（`Log/Log/Src/SyLogger.cpp`）。
硬崩溃时队列里尚未落盘的记录会**直接丢失**，因此：

> **"日志最后一行"不等于崩溃位置。** 最后一行之后通常还有若干条已产生但未写出的记录。

**已缓解（2026-08-26）**：新增 `SyLogger::Flush()`，崩溃回调
（`Main/Src/Common/CrashHandlerBootstrap.cpp`）**第一件事**就是调它，把队列冲出去。
该函数**刻意不加锁** —— 崩溃时其他线程可能正持着 logger 的互斥量且永远不会再释放
（比如崩在临界区里），加锁等于把崩溃现场变成死锁；`spdlog::logger::flush()` 自身线程安全，
最坏情况是与并发写入交错，远好过丢掉整段日志。

所以现在日志尾部**可信度提高了，但仍不是权威**：flush 之后崩溃处理器本身还会继续产生日志，
且 flush 只覆盖到"崩溃回调进入的那一刻"。取证顺序不变：

1. **看 stderr 的符号化调用栈。** 崩溃回调
   会用 dbghelp 打出 `模块!函数 + 偏移 (文件:行号)` 的调用栈，前几帧是崩溃处理器自身的噪声，往下看。
   现场机器没有 WinDbg/cdb 也能直接读到栈，不必依赖 `.dmp`。
2. **看 GL 驱动日志。** Renderx 已接入 KHR_debug 且开启 `GL_DEBUG_OUTPUT_SYNCHRONOUS`，
   回调在**产生问题的那一次 GL 调用内部**触发，日志形如
   `[gl][driver] HIGH type=0x824C id=1281: ...`。非法 GL 用法会自己报出位置，不要靠猜。
   注意 `GL_DEBUG_TYPE_OTHER`（驱动闲聊，如 "driver allocated storage for renderbuffer"）
   已降级到 debug —— warning 级别必须条条值得看，否则真正的问题会被冲掉。
3. **再看业务日志。** 用它确认崩溃前的最后一个已完成动作，而不是用它定位崩溃点。

判断"进程是否还活着"永远比读日志尾部可靠。

---

## 7. 边界检查清单

### 添加日志时检查
- [ ] 是否使用了正确的级别？
- [ ] 是否遵循了统一格式？
- [ ] 是否包含了必要字段？
- [ ] 是否会影响性能？

### 修改日志时检查
- [ ] 是否破坏了现有日志分析？
- [ ] 是否更新了相关文档？

### 排查问题时检查
- [ ] 是否有足够的日志信息？
- [ ] 是否能定位到问题根源？

---

## 8. 日志标签与代码同步说明

本文的模块标签表以当前代码中的实际 `SY_*` 日志前缀为准。规则：

1. 新增模块日志时使用 `[ModuleName]` 统一前缀。
2. 删除或重命名模块时同步更新本节。
3. 已删除组件（`SceneCompiler`、`RenderCoreRenderer`、`SceneTraverser`、`CreateCommands`/`TransformCommands`/`SelectCommands` 等）不再出现在本节。
