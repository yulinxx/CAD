# UiServices 渐进式收口计划

## 背景

`UiServices` 是 UI 层所需服务的聚合结构，当前暴露 16 个指针，其中只有 4 个是抽象接口（`ISelectionService`、`IUndoRedoManager`、`IInteractionDispatcher`、`IRecentFileService`），其余为具体实现类，违反"UI 只保留入口、交互和状态同步"原则。

---

## 使用频率分析

| 服务字段 | 类型 | 使用频率 | 依赖复杂度 |
|---------|------|---------|-----------|
| `sceneEditService` | `SceneEditService*` | **极高** (30+ 处) | 高耦合 |
| `operationBus` | `OperationBus*` | **极高** (30+ 处) | 高耦合 |
| `stateCenter` | `UiStateCenter*` | **高** (20+ 处) | 高耦合 |
| `layerManager` | `LayerManager*` | 中 (13+ 处) | 中耦合 |
| `selectionService` | `ISelectionService*` | 高 | 低（已是接口）|
| `undoManager` | `IUndoRedoManager*` | 高 | 低（已是接口）|
| `interactionDispatcher` | `IInteractionDispatcher*` | 高 | 低（已是接口）|
| `layerManagerBridge` | `QtLayerManagerBridge*` | 中 | 中耦合 |
| `layerEditService` | `LayerEditService*` | 中 | 中耦合 |
| `unitManager` | `UnitManager*` | 中 (17 处) | 低 |
| `viewportActionHub` | `ViewportActionHub*` | 中 | 中耦合 |
| `document2D` | `SceneDocument2D*` | 低 | 高耦合 |
| `importService` | `ImportService*` | 低 | 中耦合 |
| `persistenceService` | `PersistenceService*` | **极低** (0 处) | 低 |
| `recentFileService` | `IRecentFileService*` | 低 | 低（已是接口）|
| `clipboard` | `EntityClipboard*` | **极低** (0 处) | 低 |

---

## 渐进式收口计划

### 阶段 1：清理死代码（低风险）

**目标**：移除未使用的字段

| 字段 | 操作 |
|------|------|
| `persistenceService` | 从 UiServices 中移除 |
| `clipboard` | 从 UiServices 中移除 |

**理由**：这两个字段在代码中完全没有被使用

**影响范围**：
- 修改 `UiServices` 结构体
- 修改 `assembleUiServices()` 函数

---

### 阶段 2：创建高频服务抽象接口

**目标**：为高频使用的具体服务创建抽象接口

| 服务 | 操作 |
|------|------|
| `SceneEditService` | 创建 `ISceneEditService` 接口 |
| `OperationBus` | 创建 `IOperationBus` 接口 |
| `UiStateCenter` | 创建 `IUiStateCenter` 接口 |
| `LayerManager` | 创建 `ILayerManager` 接口 |

**理由**：
- 这些服务在 UiWorkbench.cpp 中使用频率极高（30+ 处）
- 抽象化后可提高可测试性和可替换性

**影响范围**：
- 创建新接口文件
- 修改 UiServices 使用抽象接口
- 修改消费者代码使用接口

---

### 阶段 3：服务注入重构

**目标**：将服务从 UiServices 移到具体类的构造函数参数

**步骤**：
1. 选择一个高频使用服务（如 `SceneEditService`）
2. 找到所有消费者（如 `Workbench2D`、`Workbench3D`）
3. 将服务从 `m_services.sceneEditService` 移到构造函数参数
4. 移除 UiServices 中的对应字段

**示例**：
```cpp
// 当前
class Workbench2D {
    UiServices m_services;
    void foo() { m_services.sceneEditService->doX(); }
};

// 收口后
class Workbench2D {
    ISceneEditService* m_sceneEdit;
    void foo() { m_sceneEdit->doX(); }
};
```

---

### 阶段 4：精简 UiServices

**目标**：最终 UiServices 只保留最小接口集

**最终目标结构**：
```cpp
struct UiServices : public IUIServices
{
    // 4 个抽象接口
    ISelectionService* getSelectionService() const override;
    IUndoRedoManager* getUndoManager() const override;
    IInteractionDispatcher* getInteractionDispatcher() const override;
    IRecentFileService* getRecentFileService() const override;

    // 可选：保留少量高频具体服务指针（通过 getter 访问）
    // 或完全移除，具体服务通过构造函数注入
};
```

---

## 风险与注意事项

1. **向后兼容**：修改 UiServices 会影响所有现有消费者
2. **测试覆盖**：重构期间需确保测试用例覆盖关键路径
3. **增量进行**：每个阶段独立测试后再进行下一阶段

---

## 执行顺序

1. **阶段 1**（1-2 天）：清理死代码
2. **阶段 2**（2-3 天）：创建抽象接口
3. **阶段 3**（3-5 天）：服务注入重构（按服务逐个进行）
4. **阶段 4**（1-2 天）：精简 UiServices

**预计总工期**：7-12 个工作日
