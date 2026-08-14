# 重构计划：UI 架构 4 项清理

## Context

上一轮架构体检报告中标出 4 项 Major 风险，本次重构按以下顺序处理，每项独立 commit、可独立回滚：

1. **Tool → SceneChangeSet** — 解决"栈对象 + 双重 clone + 无事务"问题
2. **SceneEnvGeometry 转换职责下放** — UI/2D 不该知道 Engine/2D 的 SceneEnvGeometry 形状
3. **CMake include 传播** — 上游 PUBLIC，下游 0 重复定义
4. **2D/3D 收编（MainWindow/ServiceLocator/DocumentManager）** — 验证命令总线 base 复用后，把上层重复的窗口/服务/文档基类抽出来

不动：`SceneEditService3D` / `UndoCommands3D` / `TransformEditBridge3D`（3D 独有功能，2D 无对应物），4 套并行的渲染系统（`Renderx` / `Render/NextGen` / `SanYiRender`），`BaseTool` 6 个旧 setter（重构 1 完成后可删但保留 1 个 release）。

---

## 重构 1：Tool 走 SceneChangeSet（1.5 天）

### 现状

- `ToolContext::addEntity` 签名 `std::function<void(Eg::SyEntity*)>` ([ToolContext.h:37](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/DrawTools/ToolContext.h#L37))
- Tool 栈上构造 `SyEntity` 传给 `addEntityFromPointer` → 双重 clone（`SceneEditService.cpp:117` + `SceneUndoCommands.cpp:238`）
- `SceneEditService::addEntities(std::vector<std::unique_ptr<Eg::SyEntity>>, QString)` ([SceneEditService.h:55](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Edit/SceneEditService.h#L55)) 和 `applyChangeSet(SceneChangeSet&&, QString)` ([SceneEditService.h:59-60](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Edit/SceneEditService.h#L59-L60)) 都已经支持 unique_ptr
- `SceneChangeSet::toAdd` 是 `std::vector<std::unique_ptr<Eg::SyEntity>>`（[SceneChangeSet.h:16-39](file:///c:/Users/xx/Documents/Cpp/CAD/Engine/2D/Include/Engine2D/Core/SceneChangeSet.h#L16-L39)）

### 改造

**`ToolContext.h` 加 2 个新字段**（保留 `addEntity` 至少 1 release）：
```cpp
struct ToolContext {
    // 旧：保留至全替换
    std::function<void(Eg::SyEntity*)> addEntity;

    // 新：表达整批事务（多图元 / 原子撤销）
    std::function<void(Eg::SceneChangeSet&&, QString)> submitChanges;

    // 新：单图元便捷（内部构造一个 toAdd 的 SceneChangeSet）
    std::function<void(std::unique_ptr<Eg::SyEntity>, QString)> addEntityEx;

    SceneEditService* sceneEditService = nullptr;
};
```

**`BaseTool` 加 2 个新方法**（旧 `emitEntity` 保留）：
```cpp
void submitEntity(std::unique_ptr<Eg::SyEntity> e, QString desc = QStringLiteral("Draw"));
void submitChanges(Eg::SceneChangeSet&& set, QString desc);
```

**`ToolManager::initializeTools` 注入新 lambda**（[ToolManager.cpp:20-33](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/DrawTools/ToolManager.cpp#L20-L33)）：
```cpp
if (sceneEditService) {
    enrichedCtx.submitChanges = [edit = sceneEditService](Eg::SceneChangeSet&& s, QString desc) {
        edit->applyChangeSet(std::move(s), desc);
    };
    enrichedCtx.addEntityEx = [edit = sceneEditService](std::unique_ptr<Eg::SyEntity> e, QString desc) {
        Eg::SceneChangeSet set;
        set.toAdd.push_back(std::move(e));
        edit->applyChangeSet(std::move(set), desc);
    };
}
```

**改 16 个 Tool 的提交点**（机械替换，模式相同）：

代表改动 `LineTool::completeLine` ([LineTool.cpp:106-117](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/DrawTools/LineTool.cpp#L106-L117))：
```cpp
// 旧
Eg::SyLine line;
line.basePoint = m_startPoint;
line.vPoints = {start, end};
emitEntity(&line);  // 栈对象，依赖 addEntityFromPointer 立即 clone

// 新
auto line = std::make_unique<Eg::SyLine>();
line->basePoint = m_startPoint;
line->vPoints = {start, end};
submitEntity(std::move(line));  // ownership 直接进入 SceneChangeSet
```

其他需要改的工具（按 pattern）：
- `PolyLineTool::finalizePolyline` / `BezierTool::finalizeCurve` / `Bezier2Tool::finalize`
- `PolygonTool::closeAndSubmit` / `RectangleTool::submit` / `TriangleTool::submit`
- `CircleTool::finalize` / `ArcTool::finalize` / `EllipseTool::finalize`
- `PointTool::submit` / `SmartLineTool::finalize`
- `TextInputTool::commit` / `BitmapInputTool::commit` / `QRCodeInputTool::commit`
- `NurbsTool::commit` / `SplineTool::commit`

### 关键文件

- 修改 `UI/2D/Src/UI/DrawTools/ToolContext.h`（加 2 字段）
- 修改 `UI/2D/Src/UI/DrawTools/BaseTool.h` / `BaseTool.cpp`（加 2 方法 + 改 `initialize` 接收新字段）
- 修改 `UI/2D/Src/UI/DrawTools/ToolManager.cpp:20-33`（注入新 lambda）
- 修改 16 个 Tool 的 `*Tool.cpp` 完成方法
- ✅ 注释 `SceneEditService::addEntityFromPointer` 标 `@deprecated`

### 验证

- 跑 `UI/2D/Test/BaseToolTests.cpp`、`LineToolTests.cpp`（已有）
- 新增 `UI/2D/Test/ToolSubmitChangesTests.cpp`：
  - `submitEntity` 调 `applyChangeSet` 后 `sceneManager` 有图元
  - Undo 单步回退，Redo 单步前进
  - 多次 `submitEntity` 同事务时，1 次 Undo 全清
  - `submitEntity` 失败（undo stack full）时不修改 scene

---

## 重构 2：SceneConverter 转换职责下放（1 天）

### 现状

- `RenderWidget.h:212-263` 有 51 行 `template<SceneEnvGeometryLike> setSceneEnvGeometry`，在头文件展开
- `ViewRenderCoordinator.h:4-5` 跨模块相对 include `../../../../../Render/Common/...` 和 `../../../../../Engine/2D/Include/...`
- `BaseTool.cpp:4` 同样 `../../../../Render/Common/Include/Render/RenderTypes.h`
- `SceneConverter3D` 已存在 `Render/Common/Include/Render3D/SceneConverter3D.h` 和 `Render/Common/Src/SceneConverter3D.cpp` — 2D 缺对应物

### 改造

**新建 2 个文件**（对齐 SceneConverter3D 位置）：

`Render/Common/Include/Render2D/SceneConverter2D.h`：
```cpp
#pragma once
#include "Engine2D/Environment/SceneEnvironment.h"
#include "Render/RenderTypes.h"

namespace Render2D
{
    class SceneConverter2D
    {
    public:
        static Render::SceneEnvGeometry toRender(const Eg::SceneEnvGeometry& src);
    };
}
```

`Render/Common/Src/SceneConverter2D.cpp`：把 `RenderWidget.h:212-263` 的函数体搬过来，命名空间从全局改为 `Render2D::SceneConverter2D::toRender`。

**`RenderWidget.h` 改动**：
- 删除 template overload（line 212-263，共 51 行）
- 只保留非模板 `void setSceneEnvGeometry(const Render::SceneEnvGeometry& geo)`（line 210）
- 删除头文件对 `RenderTypes.h` 的依赖（已移到 `SceneConverter2D.cpp`）

**`ViewRenderCoordinator` 改动**（[ViewRenderCoordinator.h:25](file:///c:/Users/xx/Documents/Cpp/CAD/UI/2D/Src/UI/ViewWidget/ViewRenderCoordinator.h#L25)）：
- 删除 line 4-5 的 2 个 `../../../../../` 相对 include
- `setSceneEnvGeometry(const Eg::SceneEnvGeometry&)` 实现改为：
  ```cpp
  void ViewRenderCoordinator::setSceneEnvGeometry(const Eg::SceneEnvGeometry& geo) const {
      if (!m_renderWidget) return;
      m_renderWidget->setSceneEnvGeometry(Render2D::SceneConverter2D::toRender(geo));
  }
  ```

**`BaseTool.cpp` 改动**：
- line 4 的 `../../../../Render/Common/Include/Render/RenderTypes.h` 改为 `#include "Render/RenderTypes.h"`

**`Render/Common/CMakeLists.txt` 改动**：
- 在 `RenderCommon` 现有 PUBLIC include 列表加 `${SANYI_ENGINE_2D_DIR}/Include`（SceneConverter2D.h 需要 Engine 类型）
- 同步 `RenderCommon/CMakeLists.txt` 把 Render/2D 的 include 列表里移除重复项

### 关键文件

- 新建 `Render/Common/Include/Render2D/SceneConverter2D.h`
- 新建 `Render/Common/Src/SceneConverter2D.cpp`
- 修改 `Render/2D/Include/Render2D/RenderWidget.h:209-263`（删 template）
- 修改 `Render/2D/Src/RenderWidget.cpp`（如果 inline 函数搬到 cpp）
- 修改 `UI/2D/Src/UI/ViewWidget/ViewRenderCoordinator.h:4-5`（删相对 include）
- 修改 `UI/2D/Src/UI/ViewWidget/ViewRenderCoordinator.cpp:20-25`（改实现）
- 修改 `UI/2D/Src/UI/DrawTools/BaseTool.cpp:4`（删相对 include）
- 修改 `Render/Common/CMakeLists.txt`（加 Engine 依赖）

### 验证

- rebuild 编译通过，C1083 错误不再出现
- `grep -rn "SceneEnvGeometryLike" .` 应为 0
- `grep -rn "setSceneEnvGeometry" .` 只剩 1 个非模板版本
- 运行时：开/关网格/改台面大小 → RenderWidget 显示正确

---

## 重构 3：CMake include 传播（0.5 天）

### 现状

- `UI/2D/CMakeLists.txt:91-115` 复制了 21 个上游 include 路径作为 PRIVATE（TODO 自己承认应通过 target_link_libraries 传播）
- 上游模块的 `target_include_directories` 全部 PRIVATE

### 改造

**Step 3.1：把上游所有模块的 include 改 PUBLIC**

只动 `target_include_directories` 段（确认每个模块的 CMake 都有这行，把 PRIVATE 改 PUBLIC）：

- `Utility/CMakeLists.txt`
- `Log/CMakeLists.txt`
- `Engine/Common/CMakeLists.txt`
- `Engine/2D/CMakeLists.txt`
- `Engine/3D/CMakeLists.txt`
- `Render/Common/CMakeLists.txt`
- `Render/2D/CMakeLists.txt`
- `Render/3D/CMakeLists.txt`
- `UI/Common/CMakeLists.txt`
- `UI/3D/CMakeLists.txt`
- `FileIO/FileIO/CMakeLists.txt`
- `Nesting/Nesting/CMakeLists.txt`
- `Network/Network/CMakeLists.txt`
- `Vision/Vision/CMakeLists.txt`（如启用）
- `Hardware/Hardware/CMakeLists.txt`（如启用）

**Step 3.2：删除 `UI/2D/CMakeLists.txt:91-115` 的所有 PRIVATE 路径**

整段（约 25 行）删除，仅保留 PUBLIC 的 3 行：
```cmake
target_include_directories(${LIB_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/UI2D"
        "${CMAKE_CURRENT_SOURCE_DIR}/Src"
)
```

**Step 3.3：删所有跨模块相对 include**

确认 grep：`grep -rn "include \"\\.\\./" UI Engine Render`（限定相对路径深度 ≥ 3 级）。已知位置：
- `UI/2D/Src/UI/ViewWidget/ViewRenderCoordinator.h:4-5`（重构 2 已覆盖）
- `UI/2D/Src/UI/DrawTools/BaseTool.cpp:4`（重构 2 已覆盖）

替换为：
- `"../../../../Render/Common/Include/Render/RenderTypes.h"` → `"Render/RenderTypes.h"`
- `"../../../../../Engine/2D/Include/Engine2D/Environment/SceneEnvironment.h"` → `"Engine2D/Environment/SceneEnvironment.h"`

### 关键文件

- 修改 14 个 `CMakeLists.txt`（Step 3.1）
- 修改 `UI/2D/CMakeLists.txt:91-115`（Step 3.2，删 25 行）
- 修改 2-5 个含相对 include 的 .h/.cpp（Step 3.3）

### 验证

```bash
cmake --build build --clean-first -j
# 应无 "cannot open include file" 错误
grep -rn "include \"\\.\\./" UI/2D Engine Render | grep -v third_party
# 应为空
```

---

## 重构 4：2D/3D 收编 MainWindow / ServiceLocator / DocumentManager（3 天）

### 现状（来自探索）

- `UI/3D/Include/UI3D/Operation/OperationBus3D.h` 等**已继承** `Cmd::OperationBusBase`（[OperationBus3D.h:15-30](file:///c:/Users/xx/Documents/Cpp/CAD/UI/3D/Include/UI3D/Operation/OperationBus3D.h#L15-L30)）
- `UI/3D/Include/UI3D/Edit/SceneEditService3D.h` 等是 **3D 独有**，**不改**
- 真正复制粘贴的是：`MainWindow2D/3D` `ServiceLocator2D/3D` `DocumentManager2D/3D` — 大量相同方法

### 改造

**新建 5 个基类**（`UI/Common/Include/UI/AppHost/`）：

`IAppHost.h`（纯虚接口）：
```cpp
class IAppHost {
public:
    virtual QMainWindow* mainWindow() = 0;
    virtual IServiceLocator* serviceLocator() = 0;
    virtual IDocumentManager* documentManager() = 0;
    virtual Eg::SceneManager* scene() = 0;
    virtual SelectionManager* selection() = 0;
    virtual ~IAppHost() = default;
};
```

`MainWindowBase.h`（QMainWindow 子类，CRTP）：
- 通用字段：m_settings / m_recentFiles / m_pluginManager / m_statusBar
- 通用方法：saveWindowState / restoreWindowState / registerBuiltInCommands
- 模板方法：createSceneView() / createDocumentPanel() 留给子类重写

`ServiceLocatorBase.h`（CRTP）：
- 通用字段：m_services（std::unordered_map<std::string, std::any>）
- 通用方法：registerService / getService / unregisterService
- 模板方法：createDefaultServices() 留给子类重写

`DocumentManagerBase.h`（CRTP）：
- 通用字段：m_documents / m_activeDocument
- 通用方法：openDocument / closeDocument / setActiveDocument
- 模板方法：createDocument(filename) / supportedExtensions() 留给子类重写

**派生类改造**：

`MainWindow2D` / `MainWindow3D` 改为继承 `UI::MainWindowBase<MainWindow2D>` / `UI::MainWindowBase<MainWindow3D>`，重写 `createSceneView()` 和 `registerBuiltInCommands()`。

`ServiceLocator2D/3D` 改为继承 `UI::ServiceLocatorBase<ServiceLocator2D/3D>`，重写 `createDefaultServices()`。

`DocumentManager2D/3D` 改为继承 `UI::DocumentManagerBase<DocumentManager2D/3D>`，重写 `createDocument()` 和 `supportedExtensions()`。

### 关键文件

- 新建 `UI/Common/Include/UI/AppHost/{IAppHost,AppHostBase,MainWindowBase,ServiceLocatorBase,DocumentManagerBase}.h`
- 新建 `UI/Common/Src/AppHost/{MainWindowBase,ServiceLocatorBase,DocumentManagerBase}.cpp`
- 修改 `UI/2D/Src/UI/MainWindow/MainWindow2D.h/.cpp` — 继承 MainWindowBase
- 修改 `UI/3D/Src/UI/MainWindow/MainWindow3D.h/.cpp` — 继承 MainWindowBase
- 修改 `UI/2D/Src/Service/ServiceLocator2D.h/.cpp` — 继承 ServiceLocatorBase
- 修改 `UI/3D/Src/Service/ServiceLocator3D.h/.cpp` — 继承 ServiceLocatorBase
- 修改 `UI/2D/Src/Service/DocumentManager2D.h/.cpp` — 继承 DocumentManagerBase
- 修改 `UI/3D/Src/Service/DocumentManager3D.h/.cpp` — 继承 DocumentManagerBase

### 风险

- 3D 的窗口布局（Dock 顺序、状态栏布局）保留 override
- 渐进式：先收编 `ServiceLocatorBase`（最干净），再 `DocumentManagerBase`，最后 `MainWindowBase`
- 不做大规模行为重写，只删 2D/3D 相同的代码段

### 验证

- 启动 2D 入口（`MainApp/MainApp2D.exe`），所有菜单/工具栏正常
- 启动 3D 入口（`MainApp/MainApp3D.exe`），所有菜单/工具栏正常
- 创建新文档 / 打开 / 关闭 / 切文档 路径都正常
- `grep -c "void.*::save" UI/2D/Src/UI/MainWindow/ UI/3D/Src/UI/MainWindow/` 应有重复的方法被收编到 base
- `wc -l UI/2D/Src/UI/MainWindow/MainWindow2D.cpp UI/3D/Src/UI/MainWindow/MainWindow3D.cpp` 总行数应减少 30%+

---

## 执行顺序与总时间

| 顺序 | 重构 | 风险 | 估计时间 | 依赖 |
|---|---|---|---|---|
| 1 | CMake 传播（重构 3） | 低 | 0.5 天 | 无 |
| 2 | SceneConverter 下放（重构 2） | 中 | 1 天 | 依赖重构 3（include 传播） |
| 3 | Tool → SceneChangeSet（重构 1） | 中 | 1.5 天 | 依赖重构 3 |
| 4 | 2D/3D 收编（重构 4） | 中高 | 3 天 | 独立 |

**总时间**：约 6 个工作日

**回滚策略**：每项独立 commit/PR，可独立 revert。

---

## 整体验证清单（重构完成后）

```bash
# 1. 完整 rebuild
cmake --build build --clean-first -j

# 2. 单元测试
UI2DTest/UI2DTest.exe
Engine2DTest/Engine2DTest.exe

# 3. 跨模块 include 检查
grep -rn "include \"\\.\\./" UI Engine Render | grep -v third_party
# 应为空

# 4. 模板泄漏检查
grep -rn "SceneEnvGeometryLike" .
# 应为空

# 5. 双重 clone 检查
grep -n "addEntityFromPointer" Engine/2D/Src/Edit/SceneEditService.cpp
# 仍存在但注释 @deprecated

# 6. 运行时验证
MainApp/MainApp2D.exe
MainApp/MainApp3D.exe
# 画线、撤销、重做、多图元选择、文档切换
```

---

## 关键不动的部分

- `Renderx/` `Render/NextGen/` `SanYiRender/` — 渲染层收敛是单独 PR
- `RenderWorld` / `RenderWidgetEx` 切到主链路 — 单独 PR（涉及性能优化）
- `BaseTool` 6 个旧 setter — 重构 1 完成后保留 1 release 再删
- `addEntityFromPointer` 公开方法 — 保留 1 release 再删
- 3D 独有：`SceneEditService3D` / `UndoCommands3D` / `TransformEditBridge3D`
- 5 套并行的命令基类（`OperationBusBase` 等）— 已复用，无改动
