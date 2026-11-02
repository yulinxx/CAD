# 动态库制作标准与现状

本文只保留当前工程正在遵守的 DLL / ABI 规范、当前模块现状，以及后续仍需继续统一的约束。

---

## 1. 当前 ABI 总原则

1. 对外边界优先使用 C ABI、POD、句柄和错误码。
2. 跨 DLL 的 public 接口避免直接暴露 STL、Qt 类型和模板实现细节。
3. 内部 C++ DLL 可以使用 STL，但不把 STL 作为跨边界契约。
4. 所有导出模块都要明确导出宏、头文件边界和最小公共 API。
5. 渲染相关的对外入口以 `Renderx` / `RenderX` 为准，不再沿用旧的 `RenderCommon` / `Render2D` / `Render3D` / `RenderNext` 口径。

---

## 2. 当前模块边界

| 模块 | 当前定位 | 公开边界 |
|------|----------|----------|
| `Utility` | 基础工具库（Ut::Vec/Color/BBox/Mat 数学类型） | 内部 C++ DLL，供兄弟模块 PRIVATE link |
| `Log` | 日志系统 | C 接口 + C++ 封装 |
| `CrashHandler` | 崩溃捕获 | C++ DLL |
| `Licensing` | 许可校验（原 `License`，已改名避免与根目录 LICENSE 文件大小写冲突） | C++ DLL |
| `EngineCommon` | 引擎公共基类与通用类型（`ISceneContext`、`IUndoRedoManager` 等） | 内部 C++ DLL |
| `Engine2D` | 2D 几何与文档核心 | 内部 C++ DLL |
| `Engine3D` | 3D 几何与场景核心（含 `FioEntityConverter`，已从 FileIO 下沉到 Engine3D 内部） | 内部 C++ DLL |
| `EnginePersistence` | 文档持久化（已从静态库改为动态库） | 内部 C++ DLL |
| `FileIO` | 导入导出 | 内部 C++ DLL |
| `Renderx` / `RenderX` | 统一渲染入口 | C ABI |
| `RenderBridge` | 宿主与渲染 DLL 的桥接（会话生命周期、宿主回调、常驻几何仓与绘制列表、覆盖层容器） | 内部 C++ DLL，**公共头已通过 `RenderBridgeTypes.h` 收口为完全中性的 POD 类型**（见 §3.6） |
| `UICommon` | UI 公共能力（通过内联前向声明访问 Engine2D 类型，不直接链接 Engine2D） | 内部 C++ DLL |
| `UI2D` | 2D 视图与交互 | 内部 C++ DLL |
| `UI3D` | 3D 视图与交互 | 内部 C++ DLL |
| `Nesting` | 套料/排样 | C ABI（v2，`structSize` 前向兼容，见 [`套料算法.md`](../06-算法专题/套料算法.md) §8） |
| `Raster` | 光栅化处理 | C++ DLL，支持 `install(EXPORT)` |
| `GeoModelCore` | 几何建模（OpenCASCADE） | C++ DLL，支持 `install(EXPORT)` |
| `Hardware`、`Network`、`Vision`、`Engraving`、`PythonHost`、`PyBindCore` | 扩展模块 | 按各自模块边界控制 |


---

## 3. 当前重点规则

### 3.1 跨 DLL 接口

- 只暴露必要函数，不暴露内部容器布局。
- 需要批量数据时优先使用指针 + 长度。
- 需要对象生命周期时优先使用不透明句柄。
- 需要版本兼容时必须提供版本查询或结构体大小校验。

### 3.2 STL 与 Qt

- `std::vector`、`std::string`、`QString`、`QVector` 不应直接出现在外部 ABI facade。
- 允许在 DLL 内部、模板实现和纯内部 helper 中使用。
- 若接口必须面向跨模块调用，优先改成 POD + 回调。

### 3.3 渲染模块

- 生产渲染目标名为 `RenderX`。
- 代码目录位于 `Renderx/`。
- 对外头文件以 `Renderx/include/render/render.h` 为准。
- 当前主接口围绕 `RenderDevice*`、`renderCreateDevice()`、`renderSubmitGeometry()`、`renderFrame()` 展开。

### 3.4 头文件与源文件的目录归位

判定标准只有一条：**头文件是否属于该 DLL 对外契约**。是，就必须放进 `Include/`；否，就留在 `Src/`。

- 导出的声明（带 `XXX_API` 导出宏、或被其他模块 `#include`）→ `Include/<模块名>/...`
- 全部实现（`.cpp`）→ `Src/...`
- 仅模块内部使用的头文件（内部 helper、私有实现细节）→ 与其 `.cpp` 同放 `Src/...`

**子目录必须镜像一致**：`Include/` 与 `Src/` 的相对路径保持同名同层级，头文件挪走后不改变相对包含关系。

```
UI/2D/
  Include/UI2D/ToolBar/RightToolBar.h   ← 导出头（含 UI2D_API）
  Src/UI/ToolBar/RightToolBar.cpp       ← 实现
```

这样做的目的是**交付时能整目录打包**：对外只需给 `Include/` + `.lib` + `.dll`，不必从 `Src/` 的层层子目录里逐个挑头文件；同时 `Include/` 本身就是这个 DLL 的公共 API 清单，一眼可见边界。

反例（当前 UI2D 的实际状态，属于待整理项）：`Src/UI/RightToolBar/RightToolBar.h` 带 `UI2D_API` 却留在 `Src/` 下，只在 `Include/UI2D/ToolBar/ToolBarBase.h` 放了个 `#include "UI/ToolBarBase/ToolBarBase.h"` 的转发头。转发头是权宜之计，不是目标形态——它让 `Include/` 无法独立交付，因为真正的声明还在 `Src/` 里。

### 3.5 跨库判型：禁止 `dynamic_cast`，一律用 `eType`

`SyEntity` 派生体的判型必须走 `entity->eType`（`EType` 枚举）+ `static_cast`，**不得**用
`dynamic_cast`。

原因是 RTTI 在多动态库进程里不可靠：`SyImage`、`SyFillRegion` 这类图元整体定义在头文件中，
没有任何 out-of-line 的虚函数把 vtable / typeinfo 锚定进 `Engine2D`，于是每个包含该头的库
都会各自生成一份弱符号 typeinfo；工程按 `-fvisibility=hidden` 编译，这些符号不导出，链接器
无法合并。结果是「谁创建的对象」决定了 `dynamic_cast` 能不能成功——`Main`/`UI2D` 里 new 出来
的 `SyImage`，传进 `Engine2D` 内部再 `dynamic_cast<const SyImage*>` 会返回 `nullptr`，
而且**不报任何错**，只是静默走进 else 分支。

已知踩过的坑（2026-08-30 修复）：`Geo2DQuery::isClosed()` 用 `dynamic_cast` 认位图，在真实
进程里恒为 false，于是 `hitTest()` 跳过「点是否在图元内部」这一判据，`SelectionGizmo` 判不出
`HandleKind::Body`，表现为「位图能缩放能旋转、就是拖不动」。同一进程里
`Geo2DPath::isClosedContour()` 用的是 `eType` switch，一直是对的，两边结论长期不一致。

这个缺陷还有一个恶性特征：**把相关代码单独链成一个小程序去测，`dynamic_cast` 是正确的**，
测试全绿。要复现必须在多库进程里跑（`MainTests` 这种链全部 dylib 的目标）。

`Engine/2D/Src/Algorithm/` 下的 `Geo2DSampling` / `Geo2DAnalysis` / `Geo2DEdit` 仍有上百处
`dynamic_cast<const SyLine*>` 之类的写法。它们目前"能用"是因为 `SyLine`、`SyPolygon` 等有
对应 `.cpp` 把虚函数锚定在 `Engine2D` 内，typeinfo 只有一份；这是巧合而不是保证——任何图元
一旦改成纯头文件实现，就会立刻重现同一类静默失效。这批替换尚未进行。

**当前统计**（2026-09-13 审计）：
- `Geo2DEdit.cpp`: 45 处 `dynamic_cast`
- `SceneManager.cpp`: 2 处 `dynamic_cast`
- `UndoRedoManager.cpp` / `SceneUndoCommands.cpp`: 4 处（命令合并场景，非图元判型）

**修复模式**：

```cpp
// ❌ 错误：跨 DLL dynamic_cast 不可靠
if (auto* line = dynamic_cast<const SyLine*>(entity)) {
    // ...
}

// ✅ 正确：使用 eType 枚举判型
switch (entity->eType) {
case EType::LINE: {
    const auto* line = static_cast<const SyLine*>(entity);
    // ...
    break;
}
case EType::POLYGON: {
    const auto* poly = static_cast<const SyPolygon*>(entity);
    // ...
    break;
}
// ...
}
```

---

### 3.6 RenderBridge 类型中性化（2026-11-02 已完成）

**问题**：RenderBridge 的旧公共头文件曾泄漏上层模块类型 —— `RenderTypes.h`（来自旧 RenderCommon）、`Eg::UiTextCoordMode` / `Eg::UiTextItem`（来自 EngineCommon），导致下游 include RenderBridge 公共头时被迫引入 UICommon 和 EngineCommon 的 include 路径，打破了「桥接层类型中性」的设计初衷。

**修复**：
- 新建 `RenderBridge/Include/RenderBridge/RenderBridgeTypes.h`，作为 RenderBridge 模块唯一的类型来源
- 数学类型（`Vec2f`/`Color`/`BBox2d`/`Mat3f`）直接使用 `Utility` 的 `Ut::` 命名空间（纯 POD、无 DLL 依赖），不再 include RenderTypes.h
- 覆盖层描述结构体（`SelectionOutlinePath` / `SelectionDashStyle` / `TextLayoutInfo`）在本模块内一次性定义，取代此前 UICommon 提供的同名结构体（`Render::` 下的别名）
- 文本对齐/坐标模式改用 `uint8_t` 常量（`kCoordModePixel` / `kCoordModeWorldPos` / `kHAlignLeft`…），与 `Eg::UiTextCoordMode` 语义保持一致但不引入 EngineCommon 头

**效果**：下游（UI2D/UI3D/Main/Engine2D）不再因 include RenderBridge 公共头而被迫引入 UICommon 和 EngineCommon 的 include 路径。

### 3.7 CMake 链接可见性（2026-11-02 已改为 PRIVATE 默认）

**`CMake/Utils.cmake`** 中的 `sanyi_add_shared_library()` 函数已废弃旧的 `LINK_LIBRARIES` / `QT_COMPONENTS` 参数（默认 PUBLIC 会导致 Qt / 第三方依赖被下游无差别传递），改用显式的可见性变体：

| 参数 | 语义 | 适用场景 |
|------|------|----------|
| `PUBLIC_LINK_LIBRARIES` | 公共头里作为类型出现的兄弟模块依赖 | Utility 的 `QString`、Network/Hardware 的 `QObject`、Engine* 的 `boost/geometry` |
| `PRIVATE_LINK_LIBRARIES` | 仅实现内部使用的库 | 绝大多数模块的第三方依赖 |
| `PUBLIC_QT_COMPONENTS` | 公共头里出现 Qt 类型（QObject/QWidget/QString） | UICommon/UI2D/UI3D |
| `PRIVATE_QT_COMPONENTS` | 仅实现内部用的 Qt 组件 | 不对外暴露 Qt 的模块 |

**默认行为**：未显式指定时全部按 PRIVATE 处理，避免依赖污染下游。

**导出体系现状（两套并存）**：
- **标准 `install(EXPORT)`**：Nesting、Raster、GeoModelCore 等独立可复用模块（可单独 `find_package()`）
- **手搓 Config**：UI*、Engine*、RenderBridge、RenderX 等非独立模块（通过 `sanyi_export_imported_target()` 生成 `<Target>Config.cmake`，供集成构建内部使用）

**当前已知问题**：
- Nesting SDK 的 install 目录在集成构建中被重复执行两次（集成构建和独立构建各自生成一份）
- `SANYI_PROJECT_LIBS` 手工列表维护模块依赖，新增模块时需手动更新

---

## 4. 当前需要继续统一的点

1. 继续补齐各 facade 的版本查询。
2. 继续统一错误码风格。
3. 继续压缩跨 DLL 的 public STL 用法。
4. 继续把渲染相关的旧命名统一替换为 `Renderx` / `RenderX`。
5. 继续保持 UI 与引擎、渲染的职责分离。
6. 继续按 §3.4 归位导出头：把 `Src/` 下带导出宏的头文件迁入 `Include/`，并逐步撤掉转发头。UI2D 目前尚有 19 个此类头文件留在 `Src/`。
7. 继续统一 `Include` / `Src` 的目录名大小写（多数模块用 `Include` / `Src`，`Renderx` 用 `include` / `src`）。
8. 继续把 `Engine/2D/Src/Algorithm/` 下残留的 `dynamic_cast` 判型替换为 `eType`（见 §3.5）。
9. `UiServices` 仍暴露具体类型（20 个字段里仅 4 个是接口指针），未完成收口 —— 见 [`耦合性分析.md`](耦合性分析.md) §2.2 C。
10. CMake 导出体系统一 —— 当前 `install(EXPORT)` 与手搓 Config 两套并存，新增模块时需人工选择。
11. 可直接拆出复用的 DLL：Nesting、Raster、Log、Licensing、GeoModelCore（均已支持独立构建）；需先解依赖才能拆的：UICommon、EngineCommon、RenderBridge（有跨层耦合）。

---

## 5. 结论

本文件只作为当前 DLL / ABI 约束说明，不保留历史审计过程、阶段记录或变更时间线。后续内容只补充当下仍然有效的规则与待统一项。

**文档版本**: 1.2
**最后更新**: 2026-11-02
