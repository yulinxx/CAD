# Workbench2D 拆解方案

> **状态**：方案设计，待执行。
> Workbench2D.cpp 现 2215 行，God Class。本方案给出
> 渐进式拆解蓝图。与 RenderWidget3D 拆解（几何构建已抽离）
> 不同，Workbench2D 拆解涉及 Qt 信号槽迁移，**执行需
> UI 回归测试保障**，故先冻结方案再迁移。

---

## 1. 职责分组（现状）

| 职责块 | 方法 | 行范围 | 行数 |
|---|---|---|---|
| 命令系统 | id/isCommandRegistered/dispatchCommand/commandText/displayName | 149–230 | ~80 |
| 生命周期 | initialize/attachToWindow/showSettingsDialog/saveCurrentSettings | 235–385 | ~150 |
| 视口服务 | setupViewportServices/setupImportCallbacks | 385–604 | ~220 |
| **工具栏构建** | **createToolbars** | 694–1303 | **~610** |
| **场景树管理** | setupSceneTree/onSceneTreeSceneChanged/refreshSceneTree/refreshSceneTreeIfNeeded/applySceneTreeIncremental/syncSceneTreeSelection/applySceneTreeSelection/toggleEntityVisibility/renameEntity/deleteSceneTreeSelection/setSceneTreeVisibility/setSceneTreeLock | 1303–1852 | **~550** |
| 右键菜单 | onViewportContextMenu | 1852–1908 | ~55 |
| 命令 UI 状态 | refreshCommandUiState/applySelectionContext/schedulePropertiesPanelRefresh/refreshPropertiesPanel/refreshSceneTreeRowsForSelection2D | 1908–2057 | ~150 |
| 激活/停用 | activate/deactivate/shutdown/releaseCentralWidgetGLResources | 2057–2208 | ~150 |

**两个最大块**：createToolbars（610 行）与场景树管理（550 行），合计占 52%。

---

## 2. 拆解目标（拆为 4 个类）

| 新类 | 职责 | 迁移内容 | 依赖 |
|---|---|---|---|
| **Workbench2D**（瘦身） | 工作台生命周期 + 命令分发 + 激活/停用 | 保留命令系统、生命周期、activate/deactivate/shutdown | WorkbenchWindow, WorkbenchServices |
| **SceneTreeController2D** | 场景树面板管理（创建/刷新/增量/选择/批量操作） | 12 个场景树方法 + m_scenePanel2D/m_sceneTreeObserver/m_sceneMonitor + 场景树 timer/cursor/flags/revision | WorkbenchWindow, RenderViewport2D, SceneDocument2D, SelectionService, UiStateCenter |
| **ToolbarBuilder2D** | 工具栏构建（顶部/右侧/文字字体/左侧绘图） | createToolbars（610 行） | WorkbenchWindow, CommandActionHub, ToolBarContextManager, UiConfig |
| **PropertiesPanelCoordinator2D** | 属性面板 + 命令 UI 状态刷新 | refreshCommandUiState/applySelectionContext/schedulePropertiesPanelRefresh/refreshPropertiesPanel/refreshSceneTreeRowsForSelection2D + m_propertiesRefreshTimer | CommandUiSnapshot, SelectionService, UiStateCenter |

---

## 3. 优先级与风险

| 顺序 | 目标类 | 行数 | 风险 | 理由 |
|---|---|---|---|---|
| **1** | SceneTreeController2D | ~550 | 中 | 场景树逻辑内聚，但信号槽接收者需从 Workbench2D 改为 controller |
| **2** | ToolbarBuilder2D | ~610 | 低-中 | 工具栏构建是「装配」性质，可纯函数化（输入配置 → 返回 toolbar 集合） |
| **3** | PropertiesPanelCoordinator2D | ~150 | 低 | 命令 UI 状态刷新，依赖明确 |

---

## 4. SceneTreeController2D 迁移要点（最高风险）

### 4.1 信号槽迁移（关键）

现状（setupSceneTree 中）：
```cpp
connect(panel, &SceneTreePanel::selectionChanged, this, &Workbench2D::applySceneTreeSelection);
connect(panel, &SceneTreePanel::visibilityToggled, this, &Workbench2D::toggleEntityVisibility);
connect(panel, &SceneTreePanel::renameRequested, this, &Workbench2D::renameEntity);
connect(panel, &SceneTreePanel::deleteRequested, this, &Workbench2D::deleteSceneTreeSelection);
connect(panel, &SceneTreePanel::batchVisibilityRequested, this, &Workbench2D::setSceneTreeVisibility);
connect(panel, &SceneTreePanel::batchLockRequested, this, &Workbench2D::setSceneTreeLock);
```

迁移后：
```cpp
// SceneTreeController2D 构造时建立，this = controller
connect(panel, &SceneTreePanel::selectionChanged, this, &SceneTreeController2D::applySceneTreeSelection);
// ... 其余 5 个同理
```

**验收**：场景树的点选/显隐/重命名/删除/批量操作全部仍生效。

### 4.2 成员迁移

从 Workbench2D 迁移到 SceneTreeController2D：
- `SceneTreePanel* m_scenePanel2D`
- `std::unique_ptr<SceneTreeSceneObserver2D> m_sceneTreeObserver`
- `SceneMonitor* m_sceneMonitor`
- `QTimer* m_sceneTreeRefreshTimer` + `const char* m_sceneTreeRefreshSource`
- `uint64_t m_sceneTreeCursor` + `bool m_sceneTreeForceRefresh`
- `bool m_sceneTreeIncrementalBusy` + `bool m_treeRefreshPending`
- `std::size_t m_lastSceneTreeEntityCount`
- `uint64_t m_lastSceneTreeStructureRevision` / `m_lastSceneTreeTopologyRevision`

### 4.3 依赖注入

SceneTreeController2D 构造参数：
```cpp
SceneTreeController2D(WorkbenchWindow& window,
                      RenderViewport2D* viewport,
                      SceneDocument2D* document,
                      ISelectionService* selection,
                      UiStateCenter* stateCenter);
```

Workbench2D 持有 `std::unique_ptr<SceneTreeController2D> m_sceneTreeController`，
在 setupSceneTree 中改为 `m_sceneTreeController->setup(window)`。

### 4.4 跨类调用

Workbench2D 其余方法若需刷新场景树（如 activate/deactivate），
改为 `m_sceneTreeController->refreshIfNeeded("activate")`。

---

## 5. ToolbarBuilder2D 迁移要点（低风险）

createToolbars（610 行）是「装配」性质：读 JSON 配置 + CommandCatalog
→ 创建 QToolBar/按钮。适合纯函数化：

```cpp
class ToolbarBuilder2D {
public:
    // 输入：工作台窗口、命令中枢、配置；输出：创建的 toolbar 集合
    void build(WorkbenchWindow& window,
               CommandActionHub& hub,
               ToolBarContextManager& ctx,
               const UiConfigData& config);
};
```

迁移后 Workbench2D::attachToWindow 调用
`m_toolbarBuilder.build(window, *m_commandHub, ...)`。

**验收**：所有工具栏按钮出现、快捷键绑定、勾选态与现状一致。

---

## 6. 执行步骤（每步独立可回归）

1. **建 SceneTreeController2D 骨架**（空类 + 依赖注入），Workbench2D 持有它。编译通过。
2. **迁移场景树成员**（m_scenePanel2D 等）到 controller。编译通过。
3. **迁移 12 个场景树方法**到 controller，信号槽接收者改为 controller。编译 + 场景树交互回归。
4. **Workbench2D 场景树调用点**改为 `m_sceneTreeController->xxx()`。回归。
5. **建 ToolbarBuilder2D**，迁移 createToolbars。编译 + 工具栏回归。
6. **建 PropertiesPanelCoordinator2D**，迁移命令 UI 状态刷新。回归。

每步后运行：`MainTests` + 手动 2D 工作台冒烟（场景树点选/工具栏/属性面板）。

---

## 7. 预期收益

| 指标 | 现状 | 拆解后 |
|---|---|---|
| Workbench2D.cpp | 2215 行 | ~600 行（生命周期+命令+激活） |
| 最大单一方法 | createToolbars 610 行 | 拆入 ToolbarBuilder2D |
| 场景树职责 | 散在 Workbench2D | 内聚在 SceneTreeController2D，可独立单测 |
| 2D/3D 对称 | Workbench3D 也有场景树 | 场景树控制器可抽公共基类 |

---

## 8. 与其他任务联动

| 任务 | 关系 |
|---|---|
| RenderWidget3D 拆解 | 场景树控制器与渲染无关，可并行 |
| UiServices 收口 | SceneTreeController2D 依赖接口化后的服务（ISelectionService 等） |
| Main/Src/UI 与 UI/2D 边界 | SceneTreeController2D 属「应用装配层」，留在 Main/Src/UI/Workbench |
| Workbench3D 对称 | 场景树控制器模式成熟后，Workbench3D 的场景树可复用同一基类 |
