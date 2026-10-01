# C-ABI 代码改动清单（Patch 级）

依据 `C-ABI风格约定.md` §5 落地顺序生成。每项为 **仅新增/补齐**，不改动既有签名。

---

## 1. Renderx — 补 `rxGetVersion()`

| 文件 | 变更类型 | 具体内容 |
|---|---|---|
| `Renderx/include/render/renderx.h` | 新增声明 | 在 `extern "C"` 块内新增：<br>`RENDER_API uint32_t rxGetVersion();` |
| `Renderx/src/c_api/rxCApi.cpp` | 新增实现 | 在 `extern "C"` 块内新增：<br>`uint32_t rxGetVersion() { return RENDERX_VERSION; }` |
| `Renderx/include/render/renderx.h` | 新增宏 | 在 ABI 版本区新增语义版本宏：<br>`#define RENDERX_VERSION_MAJOR 5`<br>`#define RENDERX_VERSION_MINOR 3`<br>`#define RENDERX_VERSION_PATCH 0`<br>`#define RENDERX_MAKE_VERSION(major,minor,patch) (((uint32_t)(major)<<16)\|((uint32_t)(minor)<<8)\|(uint32_t)(patch))`<br>`#define RENDERX_VERSION RENDERX_MAKE_VERSION(RENDERX_VERSION_MAJOR, RENDERX_VERSION_MINOR, RENDERX_VERSION_PATCH)`<br>`#define RENDERX_VERSION_STRING "5.3.0"`（与现有 `RENDERX_VERSION_STRING` 保持一致） |
| `Renderx/Test/RxRuntimeTests.cpp` | 新增测试 | `TEST(RxRuntime, Version) { EXPECT_EQ(rxGetVersion(), RENDERX_VERSION); EXPECT_STREQ(rxGetVersionString(), RENDERX_VERSION_STRING); }` |

> 注：`rxGetAbiVersion()` 保留，继续返回 `(major<<16)|minor` 作为 ABI 计数器。

---

## 2. 状态文本 — 为 6 模块补 `<Module>_StatusText(int32_t)`

| 模块 | 头文件声明位置 | 实现文件 | 函数签名 | 备注 |
|---|---|---|---|---|
| **CrashHandler** | `CrashHandler/Include/CrashHandler/CrashHandlerDLL.h` `extern "C"` 块末尾 | `CrashHandler/Src/CrashHandlerDLL.cpp` | `CRASHHANDLER_C_API CRASHHANDLER_API const char* CrashHandler_StatusText(int32_t status);` | 复用内部错误码表，未知码返回 `"UNKNOWN"` |
| **License** | `License/Include/License/LicenseDLL.h` `extern "C"` 块末尾 | `License/Src/LicenseDLL.cpp` | `LICENSE_C_API LICENSE_API const char* License_StatusText(int32_t status);` | 同上 |
| **Engraving** | `Engraving/Include/Engraving/EngravingCAPI.h` `extern "C"` 块（错误码段后） | `Engraving/Src/EngravingCAPI.cpp` | `ENGRAVING_API const char* Engraving_StatusText(int32_t status);` | 现有 `enum EngravingResultCode` 无 typedef，C 调用需 `enum` 关键字；建议同步改为 `typedef enum EngravingResultCode { … } EngravingResultCode;` |
| **Vision** | `Vision/Include/Vision/Service/VisionServiceC.h` `extern "C"` 块 | `Vision/Src/Service/VisionServiceC.cpp` | `VISION_API const char* Vision_StatusText(int32_t status);` | **别名** `Vision_ErrorName`：`inline const char* Vision_StatusText(int32_t s) { return Vision_ErrorName(s); }` 或直接重命名导出符号（保留 `Vision_ErrorName` 兼容） |
| **GeoModelCore** | 新建纯 C 头 `GeoModelCore/Include/GeoModelCore/GeoModelCAPI.h` | `GeoModelCore/Source/GeoModelCAPI.cpp` (新建) | `GEOMODEL_API const char* GeoModel_StatusText(int32_t status);` | 需将 `GmcErrorCode` 映射表提取到 C 实现；同时新建头用于后续 C-ABI 拆分 |
| **Renderx** | `Renderx/include/render/renderx.h` `extern "C"` 块 | `Renderx/src/c_api/rxCApi.cpp` | `RENDER_API const char* rxStatusText(int32_t status);` | **别名** `rxResultName`：`inline const char* rxStatusText(int32_t s) { return rxResultName(static_cast<RxResult>(s)); }` |

> 所有实现：静态存储期字符串字面量，未知码返回非空占位串（如 `"UNKNOWN"`）。

---

## 3. 详细错误消息 — 为 3 模块补齐二选一形态

| 模块 | 选择形态 | 头文件声明 | 实现文件 | 函数签名 | 备注 |
|---|---|---|---|---|---|
| **Vision** | 句柄关联 (`_GetLastError(handle)`) | `VisionServiceC.h` | `VisionServiceC.cpp` | `VISION_API const char* Vision_GetLastError(VisionHandle handle);` | `VisionHandle` 即现有 `uint32_t` 句柄；内部按句柄查询线程局部/对象关联错误串 |
| **GeoModelCore** | 线程局部缓冲 (`_GetLastErrorMessage`) | `GeoModelCAPI.h` (新) | `GeoModelCAPI.cpp` (新) | `GEOMODEL_API int GeoModel_GetLastErrorMessage(char* buffer, size_t bufferSize);` | 成功返回 0，失败返回负错误码（`GEOMODEL_ERR_BUFFER_TOO_SMALL` 等，需在同头定义 `GeoModelResult` enum） |
| **Renderx** | 句柄关联 (`_GetLastError(handle)`) | `renderx.h` | `rxCApi.cpp` | `RENDER_API const char* rxGetLastError(RxRuntimeHandle handle);` / `RENDER_API const char* rxGetLastError(RxSessionHandle handle);` | 视现有句柄体系决定粒度；返回静态存储期字符串 |

---

## 4. C 头文件拆分 — 为 4 模块提供纯 C 兼容头

| 模块 | 新建纯 C 头 | 原 C++ 头重命名/保留 | 拆分原则 |
|---|---|---|---|
| **Log** | `Log/Include/Log/SyLogCAPI.h` | `SyLogger.h` → `SyLoggerAPI.h` (C++) | 仅保留 `SyLog_GetVersion`、`SyLog_GetVersionString`、`SyLog_StatusText`(新)、`SyLog_GetLastErrorMessage`(新)、`SyLogConfig` POD 结构体、`SyLogLevel` enum |
| **GeoModelCore** | `GeoModelCore/Include/GeoModelCore/GeoModelCAPI.h` | `GeoModelDLL.h` → `GeoModelDLL.h` (保留版本查询) | 仅保留版本查询、`GeoModel_StatusText`、`GeoModel_GetLastErrorMessage`、POD 结构体（如 `GeoModelConfig`）、错误码 `typedef enum GeoModelResult` |
| **Vision** | `Vision/Include/Vision/VisionCAPI.h` | `VisionAPI.h` + `VisionServiceC.h` → 合并/重命名 | 仅保留版本查询、`Vision_StatusText`/`Vision_ErrorName`、`Vision_GetLastError`、POD 结构体、错误码 `typedef enum VisError : int32_t`（已有） |
| **Renderx** | `Renderx/include/render/renderx_capi.h` | `renderx.h` 保留（C++ 完整头） | 仅保留版本查询、`rxStatusText`/`rxResultName`、`rxGetLastError`、POD 结构体、错误码 `RxResult : int32_t`、后端枚举 |

> 纯 C 头要求：仅 `#include <stdint.h>` `<stddef.h>`、`extern "C"` 块、POD 结构体、`typedef enum`、无 C++ 专用语法（`enum class`、`template`、`std::`、`<cstdint>`、`<string>` 等）。C++ 头可 `#include` 纯 C 头并追加 C++ 封装。

---

## 5. Utility / UI3D — 补齐或移出 C ABI

| 模块 | 方案 A：补齐约定 | 方案 B：移出 C ABI（建议） |
|---|---|---|
| **Utility** | 1. 重命名 `SanYiUtilityVersion` → `Utility_GetVersion`<br>2. 返回 `uint32_t` packed（定义 `UTILITY_MAKE_VERSION` 宏）<br>3. 新增 `Utility_GetVersionString`<br>4. 如有失败 API 补错误约定 | 将 `extern "C"` 导出移除，改为纯内部 C++ DLL（`UTILITY_API` 类导出），CMake `BUILD_SHARED_LIBS=ON` 时仅供内部链接 |
| **UI3D** | 1. `UI3D_GetVersion` 返回 `uint32_t` packed（定义 `UI3D_MAKE_VERSION`）<br>2. 新增 `UI3D_GetVersionString`<br>3. 如有失败 API 补错误约定 | 同 Utility，移除 `extern "C"` 导出 |

> 决策建议：两者均为占位/测试用导出，**倾向方案 B**（移出 C ABI），在文档 §范围 标注“Utility/UI3D 为内部 C++ DLL，不提供 C ABI”。

---

## 6. CI 自检扩展 — `CMake/CheckCApi.cmake` 增项

| 检查项 | 实现方式 | 通过条件 |
|---|---|---|
| ① 头文件可被 C 编译通过 | `execute_process(COMMAND ${CMAKE_C_COMPILER} -fsyntax-only -x c ${header} …)` | 退出码 0 |
| ② 存在 `_StatusText(int32_t)` | `file(READ)` + 正则 `StatusText` | 匹配 |
| ③ 存在二选一 LastError API | 正则 `GetLastErrorMessage\|GetLastError` | 至少一项匹配 |
| ④ bufferSize 为 `size_t` | 正则 `bufferSize` 附近 `size_t` | 匹配 |
| ⑤ 头文件含 `MAKE_VERSION` 宏 | 正则 `MAKE_VERSION` | 匹配 |

---

## 附：各模块新增文件/符号汇总表

| 模块 | 新增头文件 | 新增源文件 | 新增导出符号 | 新增宏 |
|---|---|---|---|---|
| Renderx | — | — | `rxGetVersion`、`rxStatusText`、`rxGetLastError` | `RENDERX_VERSION_*`、`RENDERX_MAKE_VERSION` |
| CrashHandler | — | — | `CrashHandler_StatusText` | — |
| License | — | — | `License_StatusText` | — |
| Engraving | — | — | `Engraving_StatusText` | `ENGRAVING_MAKE_VERSION`（建议） |
| Vision | — | — | `Vision_StatusText`、`Vision_GetLastError` | `VISION_MAKE_VERSION`（上移至头） |
| GeoModelCore | `GeoModelCAPI.h` | `GeoModelCAPI.cpp` | `GeoModel_StatusText`、`GeoModel_GetLastErrorMessage` | `GEOMODEL_MAKE_VERSION`、`GEOMODEL_VERSION` |
| Log | `SyLogCAPI.h` | — | `SyLog_StatusText`、`SyLog_GetLastErrorMessage` | `SYLOG_MAKE_VERSION`、`SYLOG_VERSION` |
| Renderx (C 头) | `renderx_capi.h` | — | 同上 | 同上 |
| Vision (C 头) | `VisionCAPI.h` | — | 同上 | 同上 |

---

## 施工建议顺序

1. Renderx `rxGetVersion` + 宏（独立、无依赖）
2. 6 模块 `StatusText`（可并行）
3. 3 模块 `LastError`（依赖句柄/错误码体系）
4. 4 纯 C 头拆分（需同步消费端 include 路径）
5. Utility/UI3D 决策落实（文档+ CMake 同步）
6. CI 检查项落地（`CheckCApi.cmake` 扩展）

---

> **约束**：所有变更为 **新增函数/宏/头文件**，不删除、不修改既有导出符号签名，保证二进制兼容。