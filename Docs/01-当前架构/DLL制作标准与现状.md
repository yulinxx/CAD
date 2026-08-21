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
| `Utility` | 基础工具库 | 内部 C++ DLL |
| `Log` | 日志系统 | C 接口 + C++ 封装 |
| `CrashHandler` | 崩溃捕获 | C++ DLL |
| `License` | 许可校验 | C++ DLL |
| `EngineCommon` | 引擎公共基类与通用类型 | 内部 C++ DLL |
| `Engine2D` | 2D 几何与文档核心 | 内部 C++ DLL |
| `Engine3D` | 3D 几何与场景核心 | 内部 C++ DLL |
| `EnginePersistence` | 文档持久化 | 内部 C++ DLL |
| `FileIO` | 导入导出 | 内部 C++ DLL |
| `Renderx` / `RenderX` | 统一渲染入口 | C ABI |
| `UICommon` | UI 公共能力 | 内部 C++ DLL |
| `UI2D` | 2D 视图与交互 | 内部 C++ DLL |
| `UI3D` | 3D 视图与交互 | 内部 C++ DLL |
| `Nesting`、`Hardware`、`Network`、`Vision`、`Engraving`、`GeoModelCore`、`PythonHost` | 扩展模块 | 按各自模块边界控制 |

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

---

## 4. 当前需要继续统一的点

1. 继续补齐各 facade 的版本查询。
2. 继续统一错误码风格。
3. 继续压缩跨 DLL 的 public STL 用法。
4. 继续把渲染相关的旧命名统一替换为 `Renderx` / `RenderX`。
5. 继续保持 UI 与引擎、渲染的职责分离。

---

## 5. 结论

本文件只作为当前 DLL / ABI 约束说明，不保留历史审计过程、阶段记录或变更时间线。后续内容只补充当下仍然有效的规则与待统一项。
