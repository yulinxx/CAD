# 交互与 UI 审计报告

- 工程：`c:\Users\xx\Documents\Cpp\CAD`
- 审计日期：2026-11-11
- 审计性质：**只读**。未修改任何源码，未执行 `git add/commit/push`，未构建整仓，`ThirdParty/` 未触碰。
- 取证方式：逐符号 `grep` + `read` 复核当前代码；脚本统计 `.ts` / `base.json` / CMake 引用；所有结论均给出行号。历史文档（`Docs/01-当前架构/`、`Docs/09-框架审查/`）只作线索，结论一律以当前代码重新验证。
- 在途改动说明：工作区存在他人未提交改动（`RenderBridge`、`CMakeLists.txt`、子模块指针等），本报告不将其作为问题源；涉及处仅作标注。

## 总览统计

| 分区 | 条目数 | 阻断 | 严重 | 一般 | 建议 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 一、交互逻辑 | 9 | 0 | 2 | 5 | 2 |
| 二、UI 设计 | 7 | 0 | 1 | 3 | 3 |
| 三、死代码 | 13 | 0 | 0 | 3 | 10 |
| 四、多套逻辑不一致 | 6 | 0 | 2 | 2 | 2 |
| **合计** | **35** | **0** | **5** | **13** | **17** |

结论摘要：
- 未发现「阻断」级问题；`IInteractionDispatcher`、`OperationRouting3D`、`ServiceLocator3D` 等历史冗余已从代码中彻底删除（全仓 0 引用），只有注释残留。
- 2D/3D 的撤销命令均已支持 redo（2D 用 before/after 快照 swap，3D 用 `firstExecute` 标志），**不存在「只能撤销不能重做」的现存缺陷**。
- 剩余风险集中在两处：**属性编辑（显隐/锁定/改名）绕过撤销栈**，与**快照型撤销叠加后状态回滚**；以及**中文本地化覆盖不全**。

---

## 一、交互逻辑

### I-1【严重】3D 场景树的改名/显隐/锁定完全绕过撤销栈，且与 2D 语义相反

**现象**：2D 的图元改名走 `SceneEditService::mutateEntities`，会作为独立撤销命令入栈；3D 的同类操作直接写引擎对象，Ctrl+Z 无法回退。

**证据**：
- 2D 可撤销路径 [Workbench2D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L1917-L1929)：

```cpp
// 走可撤销编辑路径：重命名作为独立撤销命令入栈（前后快照）。
// 若直接写引擎，重命名不会进入撤销栈……
m_scene.sceneEditService->mutateEntities({ *eid }, [..] { entity->setName(name.c_str()); }, "Rename");
```

- 3D 直写路径 [Workbench3D.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L1458-L1462)：

```cpp
if (auto* mesh = m_sceneManager3D->findMeshById(*eid))
{
    const QByteArray utf8 = newName.toUtf8();
    mesh->setName(utf8.constData());      // 不产生撤销命令
    m_sceneManager3D->markDataChanged();
```

- 3D 显隐 / 锁定同样是直写：[Workbench3D.cpp#L1435](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L1435)、[Workbench3D.cpp#L1519](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L1519)。
- `Engine/3D/Src/Edit/SceneUndoCommands3D.cpp` 中只有 Delete/Transform/Add/Replace/SceneReplace 五类命令，**没有 Name/Visibility/Lock 命令**。

**影响**：用户在 3D 场景树改名后按 Ctrl+Z，界面无任何反应（栈里没有该动作），只能手动改回；2D 同名操作却可撤销，两个工作台行为相反。

**建议修法**：新增 `RenameMeshCommand3D` / `SetVisibilityCommand3D` / `SetLockCommand3D`，复用已有的 `UndoRedoManager3D::pushCommand`；或在 3D 场景树编辑入口统一收口到一个可注入撤销的 `MeshEditService3D`。

### I-2【严重】2D 显隐/锁定直写引擎，与快照型撤销叠加后会被旧快照回滚

**现象**：显隐、锁定修改不产生撤销命令，但撤销其它操作时会用 `entity->clone()` 的旧快照整对象替换，把用户后来的显隐/锁定/改名悄悄回滚。

**证据**：
- 直写：[Workbench2D.cpp#L1863-L1865](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L1863-L1865) `entity->setVisible(visible);`，随后只有 `notifySceneChanged()` 与树重建，无入栈。
- 快照是整对象克隆：[SceneUndoCommands.cpp#L17-L23](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/2D/Src/Edit/SceneUndoCommands.cpp#L17-L23)

```cpp
std::unique_ptr<Eg::AprEntity> clonePreserveId(const Eg::AprEntity* entity)
{
    auto snap = std::unique_ptr<Eg::AprEntity>(entity->clone());
```

- 撤销即整批换回：[SceneUndoCommands.cpp#L95-L107](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/2D/Src/Edit/SceneUndoCommands.cpp#L95-L107) `Eg::SceneWriteGateway::replaceEntitiesById(*scene, std::move(snapshots));`
- 而这些字段确实属于业务状态、并会持久化：[AprEntitySerializer.cpp#L193-L194](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/AprEntitySerializer.cpp#L193-L194) `set_name` / `set_visible`，反序列化在 L884-L893 恢复 `name/visible/locked`。

**影响**：用户先隐藏图元 A，再撤销一次更早的「移动 A」，A 会突然重新显示（旧快照里 `visible=true`）；锁定态同理。表现为「撤销后状态莫名其妙变回去」，且保存后该错误状态被写盘。

**建议修法**：与 I-1 同一套修法——把显隐/锁定纳入撤销模型；若产品上坚持「显隐不进撤销栈」，则撤销恢复时必须**按字段白名单合并**（只恢复几何字段，不覆盖 visible/locked/name），而不是整对象替换。

### I-3【一般】2D 与 3D 的滚轮派发口径不一致：3D 触控板绕过活动工具

**现象**：2D 所有滚轮/触控板事件先给活动工具，未消费再走导航；3D 只有鼠标滚轮进 `ToolManager3D`，触控板 Ctrl 捏合直接改相机。

**证据**：
- 2D 工具优先 [ViewportInputRouter.cpp#L388-L403](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L388-L403)：

```cpp
if (m_toolManager)
{
    if (auto* tool = m_toolManager->getActiveTool())
    {
        if (tool->onWheel(worldPos, event)) { event->accept(); return; }
```

- 3D 仅鼠标滚轮给工具 [RenderWidget3D.Gestures.cpp#L459-L477](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.Gestures.cpp#L459-L477)：

```cpp
if (!isTrackpad) { m_toolManager->handleWheel(event); ... return; }
float delta = ...;
m_camera.zoom(delta);      // 触控板路径：完全绕过工具
```

**影响**：后续任何 3D 参数类工具（如按 Ctrl+滚轮调参）在触控板上永远收不到事件；同一手势在两个工作台效果不同。

**建议修法**：3D 也改为「先 `m_toolManager->handleWheel(event)`，返回已消费则 return，否则再走相机」。

### I-4【一般】3D 右键菜单快照字段不全，与 2D 的 `captureSnapshot` 不对称

**现象**：3D 手工构造 `CommandUiSnapshot3D`，只填 `hasSelection/selectionCount/anyLockedEntity`；2D 由 provider 统一填 `canUndo/canRedo/hasClipboard/anyHidden/typeMask`。

**证据**：
- 3D 手填 [Workbench3D.cpp#L654-L666](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L654-L666)，随后 `CommandActionHub3D::applySnapshotToMenu(configured, snapshot);`（L673）。
- 2D 统一快照 [CommandActionHubActions2D.cpp#L615-L646](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandActionHubActions2D.cpp#L615-L646) 填充 `hasClipboard` / 选择上下文 / `canUndo/canRedo`。
- 当前 `canvas.3d` 只有 Delete / Fit All / Reset View（[base.json#L420-L427](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L420-L427)），因此尚未暴露。

**影响**：潜伏缺陷。一旦往 `canvas.3d` 加 Undo/Paste/显示隐藏项，其启用态会恒为默认值（错误亮起或恒灰）。

**建议修法**：给 `CommandActionHub3D` 增加与 2D 对称的 `captureSnapshot`，右键菜单只调它。

### I-5【一般】鼠标处理器里 `event->ignore()` 与 `eventFilter` 无条件 `return true` 自相矛盾

**证据**：[ViewportInputRouter.cpp#L282](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L282)、[#L323](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L323)、[#L346](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L346) 都调用 `event->ignore();`，而 [eventFilter#L214](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L214) 对所有鼠标事件直接 `return true`（消费）。

**影响**：`ignore()` 实际无效。当前 `RenderWidget` 是叶子控件所以看不出差异；一旦给渲染控件加子控件或改事件链，会因「注释说忽略、代码却消费」产生误判。

**建议修法**：让三个 handler 返回 `bool consumed`，`eventFilter` 用它决定返回值；或删除无效的 `ignore()`。

### I-6【一般】拖拽导入失败对用户完全静默，且结果信号存在「先失败后成功」的错误顺序

**证据**：
- 进场即发失败 [FileDropHandler.cpp#L265](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.cpp#L265) `emit sigFileImported(filePath, false);`（此时还没尝试导入），真实结果在 [L289](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.cpp#L289) / [L320](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.cpp#L320) 才发。
- 图片导入失败只写日志 [L284-L287](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.cpp#L284-L287)；不支持的后缀直接 `continue`（[L260-L263](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.cpp#L260-L263)），用户看不到任何提示。
- 两个信号全仓**无任何 connect**：仅声明于 [FileDropHandler.h#L75-L77](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.h#L75-L77)，`sigFileImported`/`sigDropFinished` 仅出现在 emit 处。

**影响**：拖入不支持的格式或损坏图片时「什么都没发生」；若后续有人接线，会先收到一次 false 再收到真实结果，产生错误提示闪烁。

**建议修法**：删除 L265 的预发；失败路径补状态栏/弹窗提示；若两个信号长期无接收者，直接删除信号与 emit。

### I-7【一般】3D 滚轮判定存在恒假子条件

**证据**：[RenderWidget3D.Gestures.cpp#L453-L454](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Render/RenderWidget3D.Gestures.cpp#L453-L454)

```cpp
const bool zoomGesture =
    ((event->modifiers() & Qt::ControlModifier) && isTrackpad) || (event->modifiers() & Qt::ControlModifier);
```

**影响**：`A && B || A` 恒等于 `A`，`isTrackpad` 项完全无效，读者会误以为触控板有独立缩放策略。

**建议修法**：简化为 `const bool zoomGesture = event->modifiers() & Qt::ControlModifier;`。

### I-8【建议】`dispatchCommand` 与 `dispatchCommandForResult` 两条分发路径语义不同

**证据**：
- [Workbench2D.cpp#L167](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L167) 的 `dispatchCommand` 优先 `OperationRouting::dispatch`（保证 Rotate/Align 的 angle/mode、Move/Mirror 的对话框分发一致），仅在无目录条目时回退 `bus->run`。
- [Workbench2D.cpp#L216-L240](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L216-L240) 的 `dispatchCommandForResult` **完全不走** `OperationRouting`，直接 `m_commands.operationBus->run(...)`。
- 目前 `dispatchCommandForResult` 生产调用只有 `file.save`（[WorkbenchWindow.cpp#L647](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/WorkbenchWindow.cpp#L647)），不需要对话框参数，故未触发缺陷。

**影响**：潜伏。任何人用 `dispatchCommandForResult` 调用需要参数对话框的命令（Move/Mirror/Rotate）都会绕过对话框逻辑。

**建议修法**：让 `dispatchCommandForResult` 复用 `OperationRouting::dispatch` 的结果通道，或至少加注释限定「仅限无参命令」。

### I-9【建议】交互分发器已删除，但 `ViewportInputRouter` 残留 5 处描述它的注释

**证据**：[ViewportInputRouter.cpp#L601](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L601)、[#L717](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L717)、[#L743](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L743)、[#L781](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L781)、[#L794](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Render/ViewportInputRouter.cpp#L794)，均写着：

```cpp
// 交互分发器的 begin/submit/cancel 当前无生产调用方，hasActiveCommand() 恒 false。
```

全仓 grep `hasActiveCommand` 仅命中这些注释与 `Docs/`，**该 API 已不存在**。

**影响**：维护者会以为还存在第二条命令取消路径，排查事件优先级时被误导。

**建议修法**：删除这 5 处注释（保留其中关于工具层取消语义的描述）。

---

## 二、UI 设计

### U-1【严重】中文翻译覆盖缺口：菜单标签缺失、27 条 tooltip 全部未进入翻译

**现象与量化证据**：
- `UI/Common/Translations/April_zh.ts`：`<message>` 共 **1675** 条，`type="unfinished"` **375** 条，其中空翻译 **306** 条。
- `base.json` 中共 **27** 条 `"tooltip"`，逐条在 `April_zh.ts` 中检索，**27/27 全部缺失**（例：`Select everything that is not currently selected`、`Snap the cursor to grid points and key geometry when drawing`、`Boolean-union selected 3D solids into one body`）。
- 菜单标签 `Process Bitmap...` / `Vectorize Bitmap...`（[base.json#L110-L111](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L110-L111)）不在翻译声明中：`UI/Common/Src/MenuLayoutTranslationStrings.cpp` 共 167 条 `QT_TRANSLATE_NOOP`，检索 `Process Bitmap` / `Vectorize Bitmap` 均为 0 命中。
- 运行时查表确认未翻译即为英文：[UiLabelLocalizer.cpp#L36-L48](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/UiLabelLocalizer.cpp#L36-L48) 依次查 6 个上下文，查不到就返回原 label。

**影响**：中文用户在算法菜单看到英文项；所有悬停提示为英文；306 条空翻译会在界面上显示为空字符串（比英文更糟）。

**建议修法**：把 JSON 的 `label` / `tooltip` 统一纳入 `lupdate` 提取（现有脚本 `PythonHost/scripts/harvest_ts.py` 已按 `April_*.ts` 收集，可扩展为生成 `QT_TRANSLATE_NOOP` 清单）；优先补 27 条 tooltip 与 306 条空翻译。

### U-2【一般】进度对话框 DPI 双重放大

**证据**：[ProgressDialog.cpp#L34-L43](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Dlg/ProgressDialog.cpp#L34-L43)

```cpp
const qreal scaleFactor = parent ? parent->devicePixelRatio() : (screen() ? screen()->devicePixelRatio() : 1.0);
const int baseWidth = static_cast<int>(420 * scaleFactor);
...
resize(baseWidth, baseHeight);
```

Qt6 中 `QWidget::resize` 使用**逻辑像素**，`devicePixelRatio` 已被 Qt 自动处理；这里再乘一次等于放大两次。

**影响**：150% 缩放下进度框约 630×300 逻辑像素（应为 420×200），200% 时接近满屏并可能超出父窗口边界。

**建议修法**：去掉 `* scaleFactor`，改为按内容 `adjustSize()` + `setMinimumWidth` 自适应。

### U-3【一般】工具栏可勾选按钮的勾选态不随业务状态同步

**证据**：
- `action->setCheckable(true)`：[CommandActionHubActions2D.cpp#L110](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandActionHubActions2D.cpp#L110)。
- 刷新函数只改 text/tooltip/icon/enabled，**没有 `setChecked`**：[CommandActionHubActions2D.cpp#L367-L402](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandActionHubActions2D.cpp#L367-L402)。
- 同一中枢的临时菜单路径却有 `setChecked`：[CommandActionHubActions2D.cpp#L577-L586](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandActionHubActions2D.cpp#L577-L586)。
- 状态来源是选择上下文：[CommandActionHubActions2D.cpp#L633-L636](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandActionHubActions2D.cpp#L633-L636)（`groupOn` / `bezierOn`）。

**影响**：Group/Bezier 按钮的高亮只反映「用户上次点击」，不反映当前选择；换选后按钮仍保持按下态，撤销后同样不同步。

**建议修法**：在 `refreshToggleActionStates` 内加 `QSignalBlocker blocker(action); action->setChecked(isOnMode);`（阻断器只挡 `toggled`，不影响 `QActionGroup` 的互斥，因为这里不涉及组内互斥）。

### U-4【一般】22 个「未实现」占位按钮仍会随上下文出现

**证据**：
- 四个上下文共注册 6 + 5 + 5 + 6 = **22** 个占位项：[Workbench2D.cpp#L1145-L1230](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L1145-L1230)（QR_ 6 个、Bitmap_ 5 个、Vector_ 5 个、Image_ 6 个），字段末位 `enabled=true`。
- 真正禁用靠前缀黑名单：[TopToolBar.cpp](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ToolBar/TopToolBar.cpp#L27-L44) 的 `isPlaceholderActionId` 命中 `Text_/QR_/Bitmap_/Vector_/Image_`，随后 `action->setEnabled(false)`，tooltip 变成 `"%1 (not implemented yet)"`。

**影响**：选中二维码/位图/矢量/图片类图元时，顶栏出现一排灰按钮并提示「尚未实现」，看起来像功能损坏。

**建议修法**：未实现的上下文**不要注册**（直接不 `registerContext`），或整体挂到 feature gate 后面。

### U-5【建议】关于对话框硬编码像素尺寸，高 DPI / 长译文下有截断风险

**证据**：[AboutDialog.cpp#L97](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Dlg/AboutDialog.cpp#L97)、[#L110](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Dlg/AboutDialog.cpp#L110) `resize(520, 580)`；[#L136](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Dlg/AboutDialog.cpp#L136) `headerWidget->setFixedHeight(140)`；[#L184-L192](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/Dlg/AboutDialog.cpp#L184-L192) 按钮 `setFixedHeight(32)` / `setMinimumWidth(100)`。

**影响**：德/俄等长译名下标题行高不足会裁切文字；`setFixedHeight` 无法被布局撑开。

**建议修法**：`setFixedHeight` → `setMinimumHeight`，尺寸统一走 `UI/UiMetrics`，并按 `QFontMetrics` 计算。

### U-6【建议】`MenuDispatcher` 两个注入方法是空实现，但调用方当真

**证据**：[MenuDispatcher.cpp#L40-L48](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/MenuDispatcher.cpp#L40-L48)

```cpp
void MenuDispatcher::setWorkbenchWindow(WorkbenchWindow* window) { Q_UNUSED(window); }
void MenuDispatcher::setStateCenter(UiStateCenter* stateCenter) { Q_UNUSED(stateCenter); }
```

调用点在 [WorkbenchMenuManager.cpp#L217-L218](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/WorkbenchMenuManager.cpp#L217-L218)。

**影响**：接口看起来依赖了窗口与状态中心，实际完全没用到；后续维护者会误以为分发器能读窗口状态。

**建议修法**：删除这两个方法与调用点（属接口收窄，不改变行为）。

### U-7【建议】部分失败路径只有日志、界面无反馈

**证据**：
- 未注册命令：[Workbench2D.cpp#L170-L176](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L170-L176) 只 `APR_WARNF`。
- 命令中枢缺总线：[CommandActionHubActions2D.cpp#L147-L153](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandActionHubActions2D.cpp#L147-L153) 只 `APR_ERRORF`（此处若发生，表现为「按钮能点，工具不切换」）。
- 图片拖入失败：[FileDropHandler.cpp#L284-L287](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/FileDropHandler.cpp#L284-L287) 只 `APR_WARN`。

**影响**：装配错误或导入失败时，用户侧完全无感知，只能从日志排查。

**建议修法**：统一走状态栏短提示（`statusBar()->showMessage(...)`）或一次性弹窗；装配类错误保留日志即可，但导入类失败应有用户可见反馈。

---

## 三、死代码

### D-1【一般】`PendingOperationRegistry` 是空壳：占位注册器定义了却从不调用

**证据**：[PendingOperationRegistry.cpp#L24-L55](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/Composition/PendingOperationRegistry.cpp#L24-L55)

```cpp
int totalRegistered = 0;
auto registerPlaceholders = [&reg, &totalRegistered](...) { ... };   // 定义后从未被调用
// 当前无待占位操作；后续新增未接入操作时在此追加数组并调用 registerPlaceholders
APR_DEBUGF("[Composition] Total %d placeholder operations registered across all categories", totalRegistered);
```

类仍被实例化：[CoreOperationRegistry.cpp#L66](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/Composition/CoreOperationRegistry.cpp#L66) `PendingOperationRegistry pendingRegistry(m_bus);`。

**影响**：日志恒为 `Total 0`；`registerPlaceholders` 是未使用 lambda（部分编译器会给 unused 警告），读者会误以为存在占位机制。

**建议修法**：删除 `PendingOperationRegistry` 及其 include/实例化；若确要保留占位能力，改成静态函数并在注释中写明「当前为空」。

### D-2【一般】5 个 UI 测试文件从未被任何 CMake 目标编译

**证据**：逐个在全部 `CMakeLists.txt`（排除 `ThirdParty/`）中检索文件名，命中均为 0：

| 文件 | CMake 引用数 |
| --- | ---: |
| `UI/Common/Test/UndoRedoTests.cpp` | 0 |
| `UI/2D/Test/EntityEditorFactoryTests.cpp` | 0 |
| `UI/2D/Test/EntityEditPolicyTests.cpp` | 0 |
| `UI/2D/Test/PolyLineSelfSnapTests.cpp` | 0 |
| `UI/3D/Test/ToolManager3DTests.cpp` | 0 |

**影响**：这 5 个测试从未运行，其中 `UndoRedoTests` 恰是覆盖撤销/重做的测试，长期不跑会让 I-1/I-2 这类回归无人拦截。

**建议修法**：要么加入对应 `Test/CMakeLists.txt` 的 gtest 目标，要么删除，避免「有测试但没跑」的错觉。

### D-3【一般】`UI/3D/CMakeLists.txt` 使用 15 处未定义的 `SANYI_*` 变量

**证据**：[UI/3D/CMakeLists.txt#L283-L288](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/CMakeLists.txt#L283-L288) 与 [#L600-L604](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/CMakeLists.txt#L600-L604)，涉及 `SANYI_UI_COMMON_DIR`、`SANYI_ENGINE_COMMON_DIR`、`SANYI_ENGINE_3D_DIR`、`SANYI_UTILITY_DIR`、`SANYI_LOG_DIR`。全仓检索 `set(SANYI_<...>_DIR ...)`，**0 条定义**。

**影响**：两个测试目标（`UI3DModelSplitPickTests`、`UI3DRefreshTests`）的 include 路径会展开为 `/Include` 这类绝对根路径；一旦启用对应构建开关，编译失败或意外引入系统头。

**建议修法**：改用 `CMAKE_CURRENT_SOURCE_DIR` / `CMAKE_SOURCE_DIR` 相对拼接，与同文件其它目标保持一致。

### D-4【一般】7 个头文件 + 6 个抽象层零引用

**证据**（全仓 grep，排除 `ThirdParty/`；引用数 = 1 表示只有自身）：

| 文件 | 引用数 | 说明 |
| --- | ---: | --- |
| `UI/2D/Src/UI/Dlg/PropertySinkAdapter.h` | 0 | 无任何 include |
| `UI/Common/Include/UI/Command/CommandKernel.h` | 1 | 仅自身 |
| `UI/Common/Include/Render/SelectionColors.h` | 1 | 仅自身 |
| `UI/Common/Include/UI/Algorithm/Engine2DTypes.h` | 1 | 仅自身 |
| `Main/Src/UI/Services/IUndoService.h` | 1 | 仅自身（且引用了不存在的 `UndoCommand` 类型）；2026-11-11 已删除 |
| `UI/2D/Include/UI2D/Operation/OperationRegisterSink2D.h` | 1 | 仅自身 |
| `UI/2D/Src/Algorithm/NestingParameters.h` | 1 | 仅自身 |
| `UI/Common/Include/UI/Interaction/ICommandGateway.h` | 0 | 死抽象层 |
| `UI/Common/Include/UI/Interaction/IInteractionController.h` | 0 | 死抽象层 |
| `UI/Common/Include/UI/Interaction/IStateService.h` | 1 | 仅自身；`.cpp` 只有 ctor/dtor |
| `UI/Common/Include/UI/Interaction/IViewportController.h` | 3 | 声明+实现+自身；`ToolContext::viewport` 全仓无赋值/使用 |
| `UI/2D/Include/UI2D/Interaction/ToolModelAdapter.h` | 2 | 仅 adapter 自己 |
| `UI/Common/Include/UI/Interaction/ToolModelAdapter3D.h` | 3 | 仅 adapter 自己 |
| `Engine/Common/Include/Engine/Tool/IToolModel.h` | 5 | 仅被上述两个零实例化 adapter 实现 |

**影响**：约 1.2k 行无效抽象，干扰对「真实架构」的判断；`IUndoService.h` 引用了不存在的类型，任何人 include 都会编译失败。

**建议修法**：整批删除；`IToolModel` 系列若确为未来扩展预留，至少加 `// 未接入，保留至 X` 并在 CMake 中不参与构建。

### D-5【建议】2D 快捷键对话框命令被 `#if 0` 编译掉，且无任何入口

**证据**：[HelpOperationRegistry.cpp#L62-L67](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/Composition/HelpOperationRegistry.cpp#L62-L67)

```cpp
#if 0
    Ops::declare(reg, OperationId::Help_Shortcut, [parentWidget] { ... });
#endif
```

`base.json` 的 Help 菜单只有 `help.docs` / `help.settings` / `help.theme` / `help.language` / `help.about`（[base.json#L247-L278](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L247-L278)），没有 `help.shortcuts`。

**影响**：双重死路径——既没注册也没入口；读者容易以为 2D 有快捷键对话框而 3D 没有。

**建议修法**：删除 `#if 0` 块与 `Help_Shortcut` 枚举/目录条目，或恢复注册并补菜单项（当前实际入口在 Settings 对话框的快捷键页，见 M-5）。

### D-6【建议】`view.test_view` 已注册进命令目录，但没有任何 UI 入口

**证据**：
- 注册：[ViewOperationRegistry.cpp#L131-L142](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/Composition/ViewOperationRegistry.cpp#L131-L142) 声明 `OperationId::View_TestView`（弹出 `TestViewWindow`）。
- 目录：[CommandCatalog.cpp#L521-L528](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/Operation/CommandCatalog.cpp#L521-L528) 文本 `"Test View"`、命令 id `"view.test_view"`，别名表在 L1027。
- 但在 `base.json` 中检索 `view.test_view` **0 命中**（菜单、工具栏、右键、快捷键均无）。

**影响**：调试窗口只能靠代码触发；同时 `UiConfigSelfCheck` 只查「JSON→目录」方向，查不出「目录有、UI 无」的反向不可达。

**建议修法**：若为调试入口，放到 `#ifndef NDEBUG` 调试菜单下；否则删除注册与目录条目。

### D-7【建议】3D 切换分支里的空 `if` 块

**证据**：[Workbench3D.cpp#L1643-L1647](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L1643-L1647)

```cpp
// 刷新命令动作状态（撤销/重做可用性）
if (m_services3D.commandActionHub && m_services3D.undoRedoManager)
{
    // 3D 命令中枢在 attachToWindow 中已创建，此处刷新其 UI 状态
}
```

**影响**：条件成立时什么也不做；注释暗示这里应刷新撤销/重做可用性，实际刷新在 `m_mainWindow3D->refreshCommandUiState()`（L1634）。

**建议修法**：删除空块与注释，或补上真正需要的 `refreshActionStates` 调用。

### D-8【建议】`UnitManager::pxPerMm()` 无任何调用

**证据**：`UI/2D/Src/Manager/UnitManager/UnitManager.cpp` 中 `pxPerMm()` 定义，全仓检索调用点为 0（仅自身声明/定义）。

**影响**：死函数，且它返回的 72 DPI 口径与其它模块的 96 DPI 冲突（见 M-1），保留会持续误导。

**建议修法**：与 M-1 一起收敛后删除。

### D-9【建议】`SceneManagerSerializer` / `ISceneSerializer` 无生产调用方

**证据**：`Engine/Persistence/Src/SceneManagerSerializer.cpp` 与 `Include/Engine/Persistence/ISceneSerializer.h` 仅在 `MainTests` / `SceneSerializerAlphaTests` 中被使用；生产代码 0 引用（`EnginePersistence` 库仍被链接，但用的是同库的 `Database.cpp`，由 `DatabaseBootstrapper` 使用，故整个库不是死的）。

**影响**：存在两套场景序列化入口（`ISceneSerializer` 与 `FileIO` 的 protobuf 序列化），容易让人误以为主存盘走这里。

**建议修法**：删除该抽象与实现，或明确迁移到测试专用目录。

### D-10【建议】14 个遗留 `SanYiCAD_*.qm` 永远不被加载

**证据**：`UI/Common/Translations/` 下共 28 个 `.qm`，其中 `SanYiCAD_*.qm` 14 个；而 [LanguageManager.h#L60-L66](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Include/UI/LanguageManager.h#L60-L66) 的语言表 `qmFileBase` 全部是 `April_en/zh/ja/...`，加载逻辑 [LanguageManager.cpp#L112](file:///c:/Users/xx/Documents/Cpp/CAD/UI/Common/Src/LanguageManager.cpp#L112) 只用该基础名拼 `.qm`。

**影响**：打包体积浪费；排查语言问题时会被这 14 个文件误导。

**建议修法**：删除 14 个 `SanYiCAD_*.qm`。

### D-11【建议】`Workbench2D.cpp` 5 处重复 include

**证据**：统计该文件 `#include` 重复项：`"UiStateCenter.h"` ×2、`"UI2D/Operation/OperationBus.h"` ×2、`"UI2D/Operation/OperationId.h"` ×2、`"UI2D/Operation/CommandCatalog.h"` ×2、`"Engine2D/Core/SceneManager.h"` ×2。

**影响**：仅可读性问题，无功能影响。

**建议修法**：去重。

### D-12【建议】`Plugin_Demo` 注册了但无任何 UI 入口

**证据**：`UI/3D/Src/Plugin/PluginManager3D.cpp#L25` 注册 `makeDemoCommandPlugin3D()`；handler 只打日志并返回固定字符串（[DemoCommandPlugin3D.cpp#L32-L37](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Plugin/DemoCommandPlugin3D.cpp#L32-L37)）；`base.json` 与快捷键表中无 `plugin.demo`。

**影响**：纯调试残留，命令目录里多一条永远达不到的 id。

**建议修法**：包进 `#ifndef NDEBUG` 并只在调试菜单暴露，或删除。

### D-13【建议】`MenuDispatcher` 的空注入方法（与 U-6 同一处）

见 U-6。此处不重复计数，仅在死代码清单中交叉标注：`setWorkbenchWindow` / `setStateCenter` 为 `Q_UNUSED` 空实现，属「已下线接口残留」。

---

## 四、多套逻辑不一致

### M-1【严重】单位换算存在三套口径：72 DPI、96 DPI、独立换算表

**证据**：
- 72 DPI（UI 侧）：`UI/2D/Src/Manager/UnitManager/UnitManager.cpp` 中 `s_dPxPerMm = 72.0 / 25.4`（反算 `25.4 / 72.0`）。
- 96 DPI 回退（图片导入）：`UI/2D/Src/Adapter/ImageImporter.cpp` DPI 无效时回退 96；`UI/2D/Src/Option/ImagePasteService.cpp` 同样回退 96。
- 独立换算表（文件层）：`FileIO/FileIO/Src/UnitTransform.cpp` 自带单位→mm 表；`FileIO/FileIO/Src/ImageUtils.cpp` 用 `ppm * 0.0254` 且 96 DPI 回退。

**影响**：同一张 300 DPI 图片从「导入」与「粘贴」进入、或经「文件层换算」与「UI 层换算」处理时，物理尺寸可能出现 96/72 = 1.33 倍偏差；`Unit::Pixel` 目前在 `UnitSelectionMenu` 中不可选，所以 72 DPI 分支是潜伏路径，但两套系数并存，任何一次放开 Pixel 单位就会立刻显形。

**建议修法**：以 `FileIO/UnitTransform` 为唯一换算源，UI 侧删除 `s_dPxPerMm` 与 96 回退，统一调用同一个 `mmPerPx(dpi)`。

### M-2【严重】「属性编辑是否入撤销栈、是否进序列化」三处口径不同

**证据**（与 I-1/I-2 同源，此处按「同一功能多套实现」归类）：
- 2D 改名：入撤销栈（`mutateEntities`，可撤销）。
- 3D 改名：不入撤销栈（`mesh->setName`）。
- 2D/3D 显隐、锁定：均不入撤销栈，但 `visible` / `locked` / `name` 都参与持久化（[AprEntitySerializer.cpp#L193-L194](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/AprEntitySerializer.cpp#L193-L194)、[#L884-L893](file:///c:/Users/xx/Documents/Cpp/CAD/FileIO/FileIO/Src/AprEntitySerializer.cpp#L884-L893)）。
- 2D 撤销用整对象克隆快照（[SceneUndoCommands.cpp#L17-L23](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/2D/Src/Edit/SceneUndoCommands.cpp#L17-L23)），3D 撤销用所有权换入换出（[SceneUndoCommands3D.cpp#L452-L463](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/3D/Src/Edit/SceneUndoCommands3D.cpp#L452-L463)），因此同一「撤销」在两个工作台对属性字段的影响完全不同。

**影响**：用户无法建立稳定的撤销预期；2D 场景下撤销会静默回滚属性修改（见 I-2），3D 不会——同类操作两种结果。

**建议修法**：定义一份「可撤销字段白名单」（几何 + 属性）并在 2D/3D 共用；非白名单字段（如 visible/locked）在撤销恢复时显式保留当前值。

### M-3【一般】工具栏构建双轨：JSON 声明 + 代码手写

**证据**：
- `base.json` 声明 4 个 toolbar（`top.toolbar.2d.default`、`left.toolbar.3d`、`top.toolbar.3d`、`right.toolbar.3d`），全部带 `"skippedByBuilder": true`（[base.json#L288](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L288)、[#L319](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L319)、[#L342](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L342)、[#L354](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/configs/base.json#L354)）。
- `UiLayoutBuilder` 因此直接跳过：[UiLayoutBuilder.cpp#L472-L475](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/UiLayoutBuilder.cpp#L472-L475) `if (tb.skippedByBuilder) { ... continue; }`。
- 实际构建：2D 读 JSON 数据（[Workbench2D.cpp#L1097-L1134](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L1097-L1134)）再交 `TopToolBar` 组装；3D 另有 `build3DToolBarActions`（[Workbench3D.cpp#L122-L163](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L122-L163)）。

**说明**：数据源是同一份 JSON（`ToolBarActionDef::commandId` 读取键为 `commandId`，含 `command` 回退，见 [UiConfigLoader.cpp#L601-L605](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/UiConfigLoader.cpp#L601-L605)），因此**内容一致，不是缺陷**；问题是存在两条构建代码，任何一侧漏掉状态刷新都会漂移。

**影响**：新增工具栏项时要同时改「JSON 数据」与「两处构建代码」；`skippedByBuilder` 让通用构建器对该配置永久失效，后续接入新客户配置时容易以为配置没生效。

**建议修法**：让 `UiLayoutBuilder` 支持「只建 Action 不建 QWidget」的模式，由工作台注入 QToolBar 宿主，消除双轨。

### M-4【一般】`help.about` 两套实现，其中 3D 的是永不被调用的桩

**证据**：
- 分发器短路：[MenuDispatcher.cpp#L65-L74](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/MenuDispatcher.cpp#L65-L74) 把 `help.about` 定义为窗口级命令；`dispatch` 中直接弹 `AboutDialog`。
- 3D 另有注册：[HelpOperations3D.cpp#L32-L34](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Src/Operation/HelpOperations3D.cpp#L32-L34)

```cpp
OperationId3D::Help_About, [](OperationContext3D& ctx, const OperationRequest3D&) {
    APR_DEBUG("[HelpOperations3D] Help_About triggered");
```

**影响**：3D 的 `Help_About` 是只打日志的桩，永远走不到；若将来有人去掉短路，3D 关于框会变成「什么都不弹」。

**建议修法**：删除 `Help_About` 的 3D 注册与目录条目，只保留 MenuDispatcher 一处。

### M-5【建议】快捷键对话框有三条路径，实际只有一条能用

**证据**：
- 2D 注册被 `#if 0` 编译掉（见 D-5）。
- 3D 注册了 `Help_Shortcut`，但 `base.json` 无 `help.shortcuts` 菜单项（检索 0 命中）。
- 实际入口是 Settings 对话框内的快捷键页（`Workbench2D` / `Workbench3D` 的 `showSettingsDialog` 路径）。

**影响**：三套实现/入口并存，排查「快捷键设置在哪」需要读三处代码。

**建议修法**：确定唯一入口（建议保留 Settings 页），删除另外两处残留。

### M-6【建议】2D 与 3D 批量隐藏后的选择集处理策略不同

**证据**：
- 2D 批量隐藏只把**被隐藏的**图元从选择集移除，其余保持选中：[Workbench2D.cpp#L1982-L2005](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench2D.cpp#L1982-L2005)。
- 3D 批量隐藏只要 `visible==false` 就 `clear()` 全部选择：[Workbench3D.cpp#L1489-L1493](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/Workbench/Workbench3D.cpp#L1489-L1493)

```cpp
// 隐藏图元时清除选择（3D 目前只支持清空全部选择）
if (!visible && m_services3D.selectionService) { m_services3D.selectionService->clear(); }
```

**影响**：同样在场景树勾掉一个图元的可见性，2D 仍保留其它选中项，3D 会把整批选择清空——用户感到「3D 里点一下就把选择弄丢了」。

**建议修法**：3D 按 id 精确移除被隐藏项（`selectionService->deselect(ids)` 或等价接口），与 2D 对齐；若 3D 选择服务暂不支持按 id 反选，至少记录 TODO 并统一为「清空」或「保留」。

---

## 附：修复优先级建议

| 优先级 | 条目 | 理由 |
| --- | --- | --- |
| P0 | I-1、I-2、M-2 | 撤销后状态被静默回滚，属数据一致性问题，且影响存盘结果 |
| P0 | U-1 | 306 条空翻译会直接显示空白，用户可见度最高 |
| P1 | I-3、I-4、U-3、U-4 | 交互/UI 与真实状态脱节，影响日常操作手感 |
| P1 | D-2、D-3 | 测试不跑、CMake 变量未定义，属交付质量基础设施 |
| P2 | U-2、U-5、M-1、M-3、M-4、M-6 | 局部体验或维护性问题 |
| P3 | 其余「建议」项 | 清理型，不改变行为 |

## 附：未确认事项

1. **`view.test_view` 是否会经调试菜单注入**：仅确认 `base.json` 与快捷键表无该 id，未排查客户覆盖配置（`april.json` / `client_a.json` / `client_b.json` / `example_client.json`）是否存在；标记「未确认」。
2. **DPI 双重放大在真实高 DPI 环境的表现**：结论基于 Qt6 逻辑像素语义推导，未实机截图验证（只读任务，未运行程序）。
3. **`UiConfigSelfCheck` 反向检查缺失**：只确认「JSON→目录」方向有校验（[UiConfigSelfCheck.cpp#L246-L299](file:///c:/Users/xx/Documents/Cpp/CAD/Main/Src/UI/ClientConfig/UiConfigSelfCheck.cpp#L246-L299)），未确认是否存在独立的「目录→UI」可达性测试；标记「未确认」。
