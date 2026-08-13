# UI 架构清理与接口抽取方案

> **文档状态（2026-08-02）**：重构记录/待复核。
>
> `WorkbenchLayoutManager`、`WorkbenchStateManager`、`WorkbenchActionManager` 三个 Manager 已于 2026-08-02 作为死代码删除（从未被实例化，逻辑与 WorkbenchWindow 内联实现逐行一致）。`ViewportInputRouter` 等其他新结构仍在使用。`WorkbenchWindow` 保留内联实现（1244 行）。

## Context

项目当前存在三类技术债:(1) `BaseTool::switchToSelectTool()` 标注 `@deprecated` 仍未删除,`ToolFactories.h` 与 `ToolManager` 模板注册功能重叠;(2) 绘图态下 `Ctrl+Z` 被劫持为局部 `stepBack()`,与用户"全局撤销"心智模型冲突;(3) `StatusBar`/`StatusBar3D` 大量平行存在但共享抽象仅到 `IMainWindow`,2D/3D 状态栏无法通过统一接口访问。

本次改造聚焦这三个低中风险点,为后续抽取 `IMenuManager`/`ILeftToolBar` 和统一交互风格(删除 `ViewInputDispatcher`)打基础。交互统一涉及 9 类行为迁移,留待下一轮单独处理。

---

## 改造点 1:删除废弃接口(风险:极低)

### 1.1 删除 `BaseTool::switchToSelectTool()`

**修改文件:**
- [BaseTool.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/BaseTool.h) 删除 230-234 行整个 `switchToSelectTool()` 方法
- [DialogTool.cpp:19](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/DialogTool.cpp#L19) `switchToSelectTool()` → `switchTool("SelectTool")`
- [DialogTool.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/DialogTool.h) 第 16、24 行注释中 `switchToSelectTool` 字样同步更新为 `switchTool("SelectTool")`
- [BaseToolTests.cpp:40](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Test/BaseToolTests.cpp#L40) `using BaseTool::switchToSelectTool;` → `using BaseTool::switchTool;`
- [BaseToolTests.cpp:495](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Test/BaseToolTests.cpp#L495) `tool.switchToSelectTool();` → `tool.switchTool("SelectTool");`
- [ToolManagerTests.cpp:23](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Test/ToolManagerTests.cpp#L23) `using BaseTool::switchToSelectTool;` → `using BaseTool::switchTool;`
- [ToolManagerTests.cpp:387](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Test/ToolManagerTests.cpp#L387) `selectTool->switchToSelectTool();` → `selectTool->switchTool("SelectTool");`

**注意:** `switchTool` 是 protected,测试 fixture 的 `using` 声明必须同步更新,否则编译失败。

### 1.2 删除冗余工厂文件

**删除文件:**
- [ToolFactories.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/ToolFactories.h) (156 行,7 个工厂类,零调用方)
- [IToolFactory.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/IToolFactory.h) (基类,唯一引用方是 ToolFactories.h)

**前置确认:** [ToolInitializer.cpp:72-103](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/ViewWidget/ToolInitializer.cpp#L72-L103) 已通过 `tm.registerTool<ConcreteTool>("Name")` 模板注册全部 17 个工具。CMakeLists 用 GLOB_RECURSE 自动收集,无显式引用。

**误报排除:** `ViewInputDispatcher.cpp` 中的 `switchToSelectToolIfNeeded` 是匿名命名空间本地函数,与 `BaseTool::switchToSelectTool()` 无关,不改动。

### 验证
- 全量构建 UI2D + UI2DCoreTests
- Grep 搜索 `switchToSelectTool` 应仅剩 `ViewInputDispatcher.cpp` 的本地函数
- 运行 `BaseToolTests` / `ToolManagerTests` 全绿

---

## 改造点 2:撤销层级语义修正(风险:低)

### 当前 Bug

[BaseTool.cpp:152-189](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/BaseTool.cpp#L152-L189):

```cpp
const bool stepBackKey =
    event->key() == Qt::Key_Backspace
    || (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier));
if (stepBackKey && isDrawing()) {
    ...
    return true;  // Ctrl+Z 被完全消费,不传到主 Undo 栈
}
```

绘图态下 `Ctrl+Z` 走局部 `stepBack()`(仅清当前预览),不触发主 `UndoRedoManager::undo()`,与用户"全局撤销"心智模型冲突。Idle 态行为正确(`BaseTool` 返回 false,Ctrl+Z 经 ShortcutManager 路由到主栈)。

### 修正

**修改 [BaseTool.cpp:154-156](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/BaseTool.cpp#L154-L156):**

```cpp
const bool stepBackKey = event->key() == Qt::Key_Backspace;
```

移除 `Qt::Key_Z + ControlModifier` 分支。`isAutoRepeat()` / `canStepBack()` / `stepBack()` 逻辑保留(仅 Backspace 走)。Ctrl+Z 在绘图态返回 false → 经 `ShortcutManager`/`CommandActionHub` 命中 `Edit_Undo` → `UndoRedoManager::undo()`。

**修改 [BaseTool.h:45](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/DrawTools/BaseTool.h#L45) 注释:**

```
绘制中 Backspace:回退一步(stepBack,仅当前图元,不入主 Undo 栈)
Ctrl+Z:始终走主 Undo 栈(UndoRedoManager::undo),绘图态亦不例外
```

### 验证
- 手动测试:绘图态画线 → Ctrl+Z 应触发主栈 undo(整图元消失,而非仅清预览)
- 手动测试:绘图态 Backspace 仍只清当前预览
- 检查 `BaseToolTests` 中是否有断言"Ctrl+Z 触发 stepBack"的用例,有则反转为"返回 false"

---

## 改造点 3:抽取 IStatusBar 接口(风险:中)

### 关键约束(阻塞风险)

不能用 `statusBar()` 作为 `IMainWindow` getter —— `QMainWindow::statusBar()` 返回 `QStatusBar*`,签名冲突无法编译。改用 `customStatusBar()`,与 2D 现有命名一致。

### 步骤

**1. 新建 [UI/Common/Include/UI/IStatusBar.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/IStatusBar.h):**

```cpp
#pragma once

class QString;

// 状态栏统一接口 — StatusBar(2D) / StatusBar3D(3D) 共有协议
// 与 IMainWindow 一致:全局类,非 QObject,无导出宏
class IStatusBar
{
public:
    virtual ~IStatusBar() = default;

    virtual void setPositionText(const QString& text) = 0;
    virtual void setMessageText(const QString& text) = 0;
    virtual void setInfoText(const QString& text) = 0;
    virtual void clearAll() = 0;
    virtual void retranslateUi() = 0;
};
```

5 个共有方法签名已精确核对一致。`setSelectionInfo` 签名不同(2D: `int,double,double`;3D: `int,QString,int`)不进接口,各自保留具体方法。`StatusBar3D::setEntityCount` 为 3D 独有,不进接口。

**2. 修改 [StatusBar.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/StatusBar/StatusBar.h) (2D):**

- `#include "UI/IStatusBar.h"`
- `class StatusBar : public QWidget, public IStatusBar`
- 5 个方法加 `override`(`setPositionText`/`setMessageText`/`setInfoText`/`clearAll`/`retranslateUi`)
- `setSelectionInfo` 保留具体方法

**3. 修改 [StatusBar3D.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Ui/StatusBar/StatusBar3D.h) (3D):**

- `#include "UI/IStatusBar.h"`
- `class StatusBar3D : public QWidget, public IStatusBar`
- 5 个方法加 `override`
- `setSelectionInfo` / `setEntityCount` 保留具体方法

**4. 修改 [IMainWindow.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/IMainWindow.h):**

新增前向声明和纯虚 getter:

```cpp
class IStatusBar;  // 前向声明

virtual IStatusBar* customStatusBar() const = 0;
```

**5. 修改 [MainWindow.h](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Ui/MainWindow/MainWindow.h) (2D):**

`StatusBar* customStatusBar() const;` → `StatusBar* customStatusBar() const override;`

利用 C++ 协变返回(`StatusBar*` 是 `IStatusBar*` 的派生),现有 `UiStateBridge.cpp:125` 的 `StatusBar* sb = mw->customStatusBar()` 仍可用。

**6. 修改 MainWindow3D.h (3D):**

新增 `StatusBar3D* customStatusBar() const override { return m_statusBar3D; }`(协变返回)。保留现有 `statusBar3D()` 不动,避免破坏 3D 内部调用方。

### 多重继承安全性

`IStatusBar` 无 QObject 祖先,无菱形问题。moc 仅一个 QObject 基(QWidget),合法。

### 验证
- 构建 UICommon / UI2D / UI3D
- `dynamic_cast<IStatusBar*>(mw2d->customStatusBar())` 非 null
- `dynamic_cast<IStatusBar*>(mw3d->customStatusBar())` 非 null
- 2D/3D 状态栏功能回归(位置文本、消息、清空、语言切换)

---

## 执行顺序

1. **改造点 1**(独立,纯删除)→ 构建验证
2. **改造点 2**(独立,改 BaseTool 语义)→ 构建验证
3. **改造点 3**(跨 Common/2D/3D,最重)→ 构建验证

1 和 2 互不依赖可并行,但建议串行便于 review。3 放最后,因涉及多模块。

---

## 风险点

1. **(阻塞已解决)改造点 3 getter 命名**:`statusBar()` 撞 `QMainWindow::statusBar()`,改用 `customStatusBar()`
2. **改造点 2 行为反转**:绘图态 Ctrl+Z 语义从"局部回退"变"主栈撤销",需回归交互测试
3. **改造点 1 测试 using 声明**:`switchTool` 是 protected,测试 fixture 的 `using BaseTool::switchTool;` 必须同步
4. **协变返回 const 一致性**:`IMainWindow::customStatusBar() const` 与两个实现类的 constness 必须完全一致,否则非 override 而是隐藏

---

## 当前框架稳定化进展（2026-06）

### 设计主线
当前 UI 重构已从“清理临时代码”转为“稳定框架 + 收口接口”。新的主线是：

1. **命令调度稳定化**：`dispatchOperation` 兼容 C++17，支持轻量测试请求与完整业务请求共存。
2. **工作台切换稳定化**：`WorkbenchWindow / UiShellHost` 负责切换生命周期，确保旧工作台停用、布局保存、新工作台激活、状态中心同步形成闭环。
3. **图元/文档统一访问**：`UiEntity` 作为统一实体基类，`EntityDocument2D / SceneDocument3D` 逐步提供 `entityById()`、`entities()`、按对象删除等统一入口。
4. **选择与几何外置**：`UiSelectionTools` 处理修剪、延伸、变换；`UiGeometryAlgorithms` 处理纯几何计算；视口逐步回到输入与投影职责。
5. **状态中心单点写入**：命令阶段、工作台、选择上下文、繁忙状态等尽量通过 `UiStateCenter` 聚合，减少 UI 各层重复写状态。

### 处理流程约定
每次往前推进时，遵循下面的顺序：

1. **先修编译隐患**：任何已知成员缺失、接口不匹配、头文件自包含等问题先修掉。
2. **再收口框架接口**：优先稳定 `dispatch`、工作台切换、文档访问、状态中心写入链路。
3. **再推进行为抽离**：把 UI 内部杂糅的工具逻辑逐步外提到独立工具/算法层。
4. **最后补测试**：框架稳定后，再补 `gtest` 单测、集成测试与回归测试，避免接口反复变化导致测试重写。

### 当前已确定的同步原则
- 设计文档必须随代码推进同步更新，不再只记录一次性的阶段方案。
- 每次新增接口、修改切换链、拆分工具层，都要在文档里补上“设计意图 + 处理流程 + 风险点”。
- 测试补充放在框架稳定之后，但设计文档中要先预留测试覆盖点。

### 接下来优先处理
- 继续收紧 `triggerWorkbench()` 和 `UiShellHost` 的切换协作。
- 继续统一 `EntityDocument2D / SceneDocument3D` 的访问方式。
- 保持 `dispatch` 接口的 C++17 兼容与测试友好。

---

## 重构过程记录（按推进顺序）

> 这一节用于完整记录当前项目的处理过程。后续每次推进，都应在这里追加，而不是另起一个文档，方便直接回看整条演进链路。

### 1. 框架起点：先稳定 UI 主骨架
- 先从 `WorkbenchWindow / UiShellHost / UiWorkbench` 这条主链入手，避免在工作台切换、主题切换、状态栏刷新时出现状态分裂。
- 目标是把“窗口生命周期、工作台切换、状态中心写入”收敛到明确入口，减少各处重复写状态。
- 当前已明确：工作台切换必须先停用旧工作台、保存布局、清理临时状态，再挂载新工作台并恢复布局。

### 2. `dispatch` 先稳定，再谈测试
- 命令分发必须兼容 C++17。
- 轻量测试请求对象不强制带 `source / params / traceId`，真实业务请求则可以携带完整上下文。
- `dispatchOperation(...)` 已改为更宽容的模板模式，避免测试结构与模板要求不一致。
- 后续所有命令分发相关改动，都要先保证接口向后兼容，再补测试。

### 3. 图元与文档统一访问
- 引入 `UiEntity` 统一图元接口，作为 2D / 3D 图元的共同抽象。
- `EntityDocument2D / SceneDocument3D` 逐步提供统一入口：`entityById()`、`entities()`、按对象删除。
- 视口层不再过度依赖 `lineById / circleById / arcById` 这种分散接口，而是优先走统一图元入口。
- 目的：让上层逻辑更像操作统一对象模型，而不是拼接多个具体类型。

### 4. 选择、几何、工具职责外置
- `UiGeometryAlgorithms` 专注纯几何计算，例如点到线段投影、旋转、镜像。
- `UiSelectionTools` 专注选择集相关动作：修剪、延伸、变换、复制。
- `Viewport2D` 逐步退回为输入事件和视图投影层，避免继续堆积业务逻辑。
- 这样做的好处是：几何可以单独复用，工具行为可以单独收口，视口不会继续膨胀。

### 5. 工作台切换链的稳定化过程
- `WorkbenchWindow::triggerWorkbench()` 由“直接切换”逐步变成“带前置状态清理与布局快照的切换流程”。
- 处理内容包括：保存旧布局、标记 busy、清理命令上下文、清理选择上下文、停用旧工作台、重建窗口内容、恢复新布局、激活新工作台。
- 同时增加“相同工作台直接返回”的保护，避免重复触发切换造成副作用。
- 切换链中的提示文案统一由 `workbenchSwitchText()` 生成，避免状态中心里出现多处重复拼接。
- `UiShellHost` 只负责主窗口与工作台对象生命周期，不应在切换流程中额外制造状态分歧。
- 切换收口过程中又进一步抽出 `resetCommandStateToIdle()`，把“命令清零”从工作台收尾/切换前置里拆成单点实现，避免重复写入和漏写字段。
- 进一步抽出 `clearSelectionState()`，把选择文本、选择来源、选择类型从工作台收尾中单独剥离，避免命令态和选择态混在一起清理。
- 进一步抽出 `resetWorkbenchLocalMirror()`，把 `busy / workbenchId / themeId / selection*` 的本地镜像归零从状态中心清理中分离出来，确保“状态源清理”和“本地镜像清理”各自独立。
- 在头文件中补足这些辅助函数的中文职责注释，方便只看接口声明也能理解边界。
- `bindStateSignals()` / `unbindStateSignals()` 只负责连接与断开，不承担状态初始化或业务编排，避免信号层膨胀。
- 切换流程现在按照“冻结命令态 → 清理选择态 → 停用旧工作台 → 清空容器 → 更新工作台 ID → 绑定新工作台 → 恢复布局 → 激活新工作台”的顺序执行，且每一步都只做自己职责内的事。
- 工作台 ID 和切换上下文会在冻结阶段写一次、在重建阶段再确认一次，保证中间态也不会偏离状态中心。
- `UiShellHost` 只做宿主编排，`WorkbenchWindow` 才做具体切换/刷新/镜像；两者不要互相越权接管对方职责。
- 切换完成后，本地窗口状态也会再次对齐状态中心，避免中间态沿用旧工作台 ID 或旧 busy 标记。

### 6. 文档同步原则
- 任何一次框架推进，都要同步更新本文件。
- 更新内容必须包含：设计意图、处理流程、风险点、下一步优先级。
- 文档不是最终说明书，而是“当前处理过程记录”，随着重构继续向前追加。
- 任何新增代码若改变了流程边界，必须同步补充中文注释，确保后续看代码就能顺着文档理解。

### 7. 测试安排原则
- 先稳定框架，再补 `gtest` 单测。
- 集成测试 / 回归测试放在框架和核心接口稳定之后。
- 对于会反复变化的接口，先写设计约束到文档里，避免测试太早固化错误心智模型。
- 先建立测试骨架，再逐步填真实断言；不要为了“有测试”而把框架推进打断。
- 单测优先覆盖命令核、调度接口、注册/查找、状态标志等稳定底座；UI 行为测试待框架边界进一步收口后再补。

### 8. 窗口状态镜像原则
- `syncWindowStateFromStateCenter()` 只做镜像，不做判断、不做决策、不做重建。
- `syncWorkbenchSelectionFromStateCenter()` 只做选择语义镜像，不参与展示拼接。
- `refreshStatusText()` 只做展示刷新，不写业务状态。
- `updateWindowTitle()` 只拼接标题，不承担工作台逻辑或主题逻辑。
- 主题、工作台、命令、选择等状态都优先以状态中心为准，窗口本地状态只作为展示镜像和幂等保护。

### 9. 单测推进原则
- 先补稳定底座的单测，再考虑 UI 交互层测试。
- 命令核测试优先覆盖：`dispatchOperation`、`OperationRegistryBase`、`CommandCatalogBase`、`CommandEnableRule`。
- 在接口还可能继续收口的阶段，只补“语义不容易变化”的测试，避免测试先于框架冻结错误边界。
- bus 相关测试先验证注册/查找/缺失路径，再逐步补更复杂的派发和执行行为。
- 新增测试前先确认相关头文件与模板接口真实存在，避免为了补测试引入不存在符号导致编译失败。

### 10. 位图显示流程收口原则（2026-08）
- 位图显示不再走“线框退化路径”，`SyImage` 必须进入独立位图同步链。
- `SceneRefreshCoordinator` 是**唯一编排者**：全量和增量都收敛到同一个 `reconcileBitmaps(sm, fullReconcile)`。
- 位图层的单一真源是场景中“可见的 `SyImage` 集合”（实体可见 + 图层可见）。
- 增量刷新只负责两类事情：`dirty` 的图元增量更新，以及位图账本与场景期望集的差异收敛。
- 全量刷新必须先清空 GPU 位图层，再按场景顺序重建，避免旧纹理残留。
- `RenderWidget` 不保存业务语义，只负责把 `SyImage` 的像素与四角坐标同步给 Renderx。
- Renderx 侧 `BitmapRenderer` 按 `entityId` 管理多纹理；旧的 `renderSetBitmap/renderClearBitmap` 只作为过渡兼容入口，映射到保留槽位 `entityId=0`。
- 图层显隐变化不依赖“位图实体 dirty”，而是由同步收敛逻辑直接处理移除与重新上传。

### 11. 位图同步链的最终形态
1. 导入阶段把图片转成 `SyImage`，并补全四角坐标。
2. 场景变更只进入 `SceneRefreshCoordinator`。
3. 协调器先处理矢量图元，再调用 `reconcileBitmaps` 同步位图。
4. `reconcileBitmaps` 对比“场景期望集”和“本地账本”，完成新增、更新、删除、隐藏、显示的统一收敛。
5. `RenderWidget` 只做提交，不做策略判断。
6. Renderx 只负责资源持有与绘制，不知道 UI 语义。
7. 这样可保证图片导入、修改、图层显隐、全量重建四条路径最终落到同一套状态模型。

### 12. 位图流程的回归检查点
- 图片导入后是否能显示，不再依赖线框顶点。
- 同一文档多张图片是否能同时显示。
- 图层隐藏后图片是否立即消失，重新显示后是否恢复。
- 全量刷新后位图是否仍与场景一致。
- 旧兼容 API（`renderSetBitmap`）是否仍能工作，但仅作为单图过渡入口。

### 当前已完成的收口
- 命令分发更宽容、更适合 C++17。
- UI 主工作台切换链更稳定。
- 图元/文档访问风格正在统一。
- 选择与几何处理已明显外置。
- 位图显示已经从“线框退化”收口到“场景驱动的纹理同步”。

### 下一步建议
- 继续收紧 `triggerWorkbench()` 与 `UiShellHost` 的职责边界。
- 继续统一 `SceneDocument3D` 与 `EntityDocument2D` 的访问习惯。
- 如果后续图片还要支持排序/Z 序或局部裁剪，再把位图同步从协调器进一步抽成独立组件，并补单测。
- 在框架稳定后，再开始补 `gtest` 单测与回归测试。
