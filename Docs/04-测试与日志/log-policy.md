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
| **模块名** | 日志来源模块 | `OperationBus`, `SceneCompiler`, `ToolManager` |
| **事件** | 事件类型 | `execute`, `compile`, `render`, `select` |
| **操作ID** | 操作标识 | `Tool_Line`, `Edit_Delete`, `View_ZoomFit` |
| **来源** | 触发来源 | `Menu`, `Toolbar`, `Shortcut`, `Gizmo` |
| **详情** | 详细信息 | 参数、结果、错误原因 |

### 2.3 格式示例

```cpp
// 命令执行
SY_INFO("[%s] %s op=%s source=%s result=%s", 
    busTag(), eventName, operationIdToString(id), 
    operationSourceToString(source), result.message);

// 场景编译
SY_INFO("[SceneCompiler] compile sceneType=%s entities=%d batches=%d",
    sceneType.c_str(), entityCount, batchCount);

// 渲染帧
SY_DEBUG("[RenderCoreRenderer] render frame=%d batches=%d time=%dms",
    frameNumber, batchCount, renderTime);

// 错误处理
SY_ERROR("[OperationBus] execute failed op=%s error=%s",
    operationIdToString(id), errorMessage.c_str());
```

---

## 3. 日志分类

### 3.1 按模块分类

| 模块 | 日志标签 | 主要内容 | 状态 |
|------|----------|----------|------|
| **CADApplicationRuntime** | `[CADApplicationRuntime]` | 应用启动、初始化、关闭 | ✅ |
| **AppBootstrapper** | `[AppBootstrapper]` | 组合根初始化、工作台创建、启动序列 | ✅ |
| **IInteractionDispatcher** | `[InteractionDispatcher]` | 命令执行、提交、取消、撤销、重做 | ✅ |
| **CreateCommands** | `[CreateCommands]` | 绘制命令激活、提交、取消（线、圆、弧、多段线、多边形） | ✅ |
| **TransformCommands** | `[TransformCommands]` | 变换命令激活、提交、取消（移动、旋转、复制、删除、镜像） | ✅ |
| **SelectCommands** | `[SelectCommands]` | 选择命令激活、提交、取消 | ✅ |
| **SceneDocument2D** | `[SceneDocument2D]` | 文档操作（创建、删除、清空） | ✅ |
| **WorkbenchWindow** | `[WorkbenchWindow]` | 主窗口创建、工作台切换、菜单构建 | ✅ |
| **OperationBus** | `[OperationBus]` | 命令执行、结果、状态变化 | ✅ |
| **SceneCompiler** | `[SceneCompiler]` | 编译过程、图元统计、批次信息 | ✅ |
| **ToolManager** | `[ToolManager]` | 工具切换、激活/失活、状态变化 | ✅ |
| **RenderCoreRenderer** | `[RenderCoreRenderer]` | 渲染调度、帧统计、错误处理 | ✅ |
| **SelectionSet** | `[SelectionSet]` | 选择变化、选中图元、高亮更新 | ✅ |
| **ViewWidget** | `[ViewWidget]` | 事件处理、相机变化、视图操作 | ✅ |
| **ToolManager3D** | `[ToolManager3D]` | 3D 工具注册、激活、切换、事件分发 | ✅ |
| **SelectionTool3D** | `[SelectionTool3D]` | 3D 选择模式、拾取结果、框选操作 | ✅ |
| **TransformTool3D** | `[TransformTool3D]` | 3D 变换模式切换、操作开始/结束 | ✅ |
| **NavigationTool3D** | `[NavigationTool3D]` | 3D 导航操作、相机状态变化 | ✅ |

### 3.2 日志覆盖范围（2026-07-11 更新）

#### 启动流程
- ✅ `CADApplicationRuntime`：应用启动、初始化、关闭
- ✅ `AppBootstrapper`：组合根初始化、工作台创建、启动序列

#### 命令生命周期
- ✅ `IInteractionDispatcher`：命令执行、提交、取消、撤销、重做
- ✅ `CreateCommands`：DrawLine、Circle、Arc、Polyline、Polygon 命令激活/提交/取消
- ✅ `TransformCommands`：Move、Rotate、Copy、Delete、Mirror 命令激活/提交/取消
- ✅ `SelectCommands`：Select 命令激活/提交/取消

#### 文档操作
- ✅ `SceneDocument2D`：图元创建、删除、清空

#### 工作台与UI
- ✅ `WorkbenchWindow`：主窗口创建、工作台切换、菜单构建

#### 渲染管线
- ✅ `RenderCoreRenderer`：编译开始/完成、渲染状态、错误处理
- ✅ `SceneCompiler`：编译过程、图元统计、批次信息
- ✅ `SceneTraverser`：场景遍历、图元访问

#### 工具系统
- ✅ `ToolManager`（UI/2D）：工具切换、激活/失活、状态变化
- ✅ `ToolManager3D`：3D 工具注册、激活、切换、事件分发
- ✅ `SelectionTool3D`：3D 选择模式、拾取结果、框选操作
- ✅ `TransformTool3D`：3D 变换模式切换、操作开始/结束
- ✅ `NavigationTool3D`：3D 导航操作、相机状态变化

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

### 4.3 性能考虑

| 规则 | 说明 |
|------|------|
| **DEBUG 级别** | 生产环境默认关闭 |
| **频率限制** | 高频操作（如鼠标移动）只记录关键事件 |
| **日志轮转** | 自动轮转，保留最近 7 天 |
| **异步写入** | 日志写入不阻塞主线程 |

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
   grep "\[SceneCompiler\]" app.log

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

## 8. 变更记录

| 日期 | 版本 | 变更内容 | 作者 |
|------|------|----------|------|
| 2026-07-11 | 1.2.0 | 补充完整日志覆盖范围：渲染管线、3D工具系统、SelectionSet、ViewWidget | 开发组 |
| 2026-07-11 | 1.1.0 | 全生命周期日志落地：启动流程、命令生命周期、文档操作、工作台切换 | 开发组 |
| 2026-07-10 | 1.0.0 | 初版：基于当前日志实现编写 | 架构组 |
