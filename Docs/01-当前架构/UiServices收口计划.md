# UiServices 渐进式收口计划

## 背景

`UiServices` 是 UI 层所需服务的聚合结构，当前暴露 15 个指针，其中只有 4 个是抽象接口（`ISelectionService`、`IUndoRedoManager`、`IInteractionDispatcher`、`IRecentFileService`），其余为具体实现类，违反"UI 只保留入口、交互和状态同步"原则。

---

## 使用频率分析

| 服务字段 | 类型 | 使用频率 | 依赖复杂度 | 状态 |
|---------|------|---------|-----------|------|
| `sceneEditService` | `SceneEditService*` | **极高** (30+ 处) | 高耦合 | 需创建接口 |
| `operationBus` | `OperationBus*` | **极高** (30+ 处) | 高耦合 | 需创建接口 |
| `stateCenter` | `UiStateCenter*` | **高** (20+ 处) | 高耦合 | 需创建接口 |
| `layerManager` | `LayerManager*` | 中 (13+ 处) | 中耦合 | 需创建接口 |
| `selectionService` | `ISelectionService*` | 高 | 低 | ✅ 已是接口 |
| `undoManager` | `IUndoRedoManager*` | 高 | 低 | ✅ 已是接口 |
| `interactionDispatcher` | `IInteractionDispatcher*` | 高 | 低 | ✅ 已是接口 |
| `layerManagerBridge` | `QtLayerManagerBridge*` | 中 | 中耦合 | 需评估 |
| `layerEditService` | `LayerEditService*` | 中 | 中耦合 | 需评估 |
| `unitManager` | `UnitManager*` | 中 (17 处) | 低 | 需评估 |
| `viewportActionHub` | `ViewportActionHub*` | 中 | 中耦合 | 需评估 |
| `document2D` | `SceneDocument2D*` | 低 | 高耦合 | 需评估 |
| `importService` | `ImportService*` | 低 | 中耦合 | 需评估 |
| `persistenceService` | `PersistenceService*` | 中 (1 处) | 低 | ⚠️ 不可移除 |
| `recentFileService` | `IRecentFileService*` | 低 | 低 | ✅ 已是接口 |
| `clipboard` | `EntityClipboard*` | 低 (1 处) | 低 | ⚠️ 不可移除 |

---

## 渐进式收口计划

### 阶段 1：清理死代码 ✅ 已完成

**状态**：部分完成

**结果**：
- ❌ `persistenceService` - **不可移除**，在 `WorkbenchWindow.cpp:304` 被使用
- ❌ `clipboard` - **不可移除**，在 `UiWorkbench.cpp:978` 被使用

---

### 阶段 2：创建高频服务抽象接口 ⚠️ 高风险

**目标**：为高频使用的具体服务创建抽象接口

| 服务 | 操作 | 风险 |
|------|------|------|
| `SceneEditService` | 创建 `ISceneEditService` 接口 | 高 |
| `OperationBus` | 创建 `IOperationBus` 接口 | 高 |
| `UiStateCenter` | 创建 `IUiStateCenter` 接口 | 高 |
| `LayerManager` | 创建 `ILayerManager` 接口 | 高 |

**风险评估**：
- 影响范围广，涉及 30+ 处代码修改
- 需要大量测试验证
- 建议：延后执行

---

### 阶段 3：服务注入重构 ⚠️ 高风险

**目标**：将服务从 UiServices 移到具体类的构造函数参数

**风险评估**：
- 需要大规模重构
- 建议：延后执行

---

### 阶段 4：精简 UiServices ⚠️ 高风险

**目标**：最终 UiServices 只保留最小接口集

**风险评估**：
- 需要阶段 2、3 的工作作为基础
- 建议：延后执行

---

## 低风险可执行项

### 1. 文档完善

- [x] 创建收口计划文档 ✅
- [ ] 更新服务使用频率分析
- [ ] 记录已知约束

### 2. 代码规范

- [ ] 在 UiServices 注释中标注非抽象字段
- [ ] 添加 TODO 标记待处理服务

### 3. 接口审查

- [ ] 检查现有接口是否满足需求
- [ ] 评估是否有服务可以完全移除

---

## 风险与注意事项

1. **向后兼容**：修改 UiServices 会影响所有现有消费者
2. **测试覆盖**：重构期间需确保测试用例覆盖关键路径
3. **增量进行**：每个阶段独立测试后再进行下一阶段
4. **当前约束**：`persistenceService` 和 `clipboard` 仍在使用，不可移除

---

## 结论

**当前状态**：
- 阶段 1 无法进一步清理死代码
- 阶段 2-4 风险较高，建议延后

**推荐行动**：
1. 保持当前 UiServices 结构不变
2. 如有需要，可选择性为新服务创建接口
3. 在未来重构时参考本计划
