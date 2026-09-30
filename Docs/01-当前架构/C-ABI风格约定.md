# C ABI 风格约定

> **性质**：标准/规范文档（待落地）。
> **范围**：所有对外暴露 `extern "C"` / C ABI 的模块（CrashHandler、License、Log、Engraving、GeoModelCore、Vision、Nesting、Renderx、Utility、UI3D、SanyiPlugin 等）。Hardware 与 FileIO 为纯 C++ DLL，不含 C ABI 导出，不在本约定范围内。
> **背景**：见 [`框架现状与修理计划.md`](框架现状与修理计划.md) §3 第 3 条「统一各 facade 的版本查询与错误码风格」。

---

## 1. 为什么需要统一

跨 DLL 的 C ABI 是**对外契约**。当前各模块的版本查询与错误码命名/形态不一致，
导致：

- 消费者需要为每个模块记一套不同的调用方式；
- 新增模块时没有可照抄的模板，继续发散；
- 文档与自检清单难以统一。

本约定给出**一套最小强制集合**，各模块照此收敛。

---

## 2. 版本查询约定

### 2.1 强制提供

| 函数 | 签名 | 说明 |
|---|---|---|
| `<Module>_GetVersion` | `uint32_t <Module>_GetVersion(void)` | **packed 语义版本**：`(major<<16) \| (minor<<8) \| patch` |
| `<Module>_GetVersionString` | `const char* <Module>_GetVersionString(void)` | 人类可读版本 `"MAJOR.MINOR.PATCH"`，**静态存储期**，调用方不得释放 |

### 2.2 可选提供

| 函数 | 签名 | 说明 |
|---|---|---|
| `<Module>_GetAbiVersion` | `uint32_t <Module>_GetAbiVersion(void)` | **ABI 兼容整数**，仅当 ABI 破坏性变更需要独立计数器时提供；每次破坏性变更 +1 |

### 2.3 命名与编码

- `<Module>` = 该 DLL 既有的导出符号前缀（如 `CrashHandler` / `License` / `SyLog` / `Engraving` / `GeoModel` / `Vision` / `Nesting` / `rx` / `Utility` / `UI3D` / `SanyiPlugin`）。
  **同一 DLL 内前缀必须唯一且固定**，不混用（例如 `rx` 与 `RenderX_` 二选一）。
- packed 编码统一为 `(major<<16)|(minor<<8)|patch`（与 `LICENSE_MAKE_VERSION` / `CRASHHANDLER_MAKE_VERSION` / `NESTING_MAKE_VERSION` 一致）。
- 建议每个模块在公共头文件提供 `#define <MODULE>_MAKE_VERSION(major,minor,patch)` 与 `<MODULE>_VERSION` 宏。

### 2.4 当前现状与差距

| 模块 | `_GetVersion` | `_GetVersionString` | `_GetAbiVersion` | 差距 |
|---|:---:|:---:|:---:|---|
| CrashHandler | ✅ | ✅ | — | 缺 `_GetAbiVersion`（可选）；缺 `MAKE_VERSION` 宏于公共头 |
| License | ✅ | ✅ | — | 同上 |
| Log (`SyLog_`) | ✅ | ✅ | — | 缺 `_GetAbiVersion`（可选）；缺 `MAKE_VERSION` 宏于公共头 |
| Engraving | ✅ | ✅ | — | 同上 |
| GeoModel (`GeoModel_`) | ✅ | ✅ | — | 同上；建议补齐 `GEOMODEL_MAKE_VERSION` 宏于公共头 |
| Vision | ✅ | ✅ | — | 同上；版本宏仅在 .cpp，建议上提至公共头 |
| Nesting | ✅ | ✅ | ✅ | 符合；含 `NESTING_MAKE_VERSION` / `NESTING_VERSION` 宏 |
| Renderx (`rx`) | ❌ | ✅ | ✅ (`rxGetAbiVersion`) | **缺 `rxGetVersion()`**（packed 语义版本）；`rxGetAbiVersion` 为 `(major<<16)\|minor` 无 patch，属既定变体 |
| Utility | ❌ (`SanYiUtilityVersion`) | ❌ | ❌ | 命名/返回类型/编码均不符合约定 |
| UI3D | ❌ (`UI3D_GetVersion` 返回 `int`) | ❌ | ❌ | 返回类型应为 `uint32_t`，缺字符串版本 |
| SanyiPlugin | ✅ | ✅ | ✅ | 插件子系统独立约定，已符合本文件核心要求 |

> 结论：核心 7 模块（除 Renderx）版本查询编码一致、覆盖完整。Renderx 仍缺 `rxGetVersion()`（packed 语义版本）。Utility/UI3D 为遗留占位导出，需补齐或移出 C ABI 面。SanyiPlugin 属独立插件约定，仅作参考。

---

## 3. 错误 / 状态约定

### 3.1 首选：返回状态枚举

- 每个模块定义 `<Module>Status` / `<Module>Result`（`typedef enum`，`int32_t` 兼容），
  **成功值必须为 0**（`<MODULE>_OK = 0`）。
- 所有可能失败的 API 返回该枚举；不要用 `bool` + 隐式错误状态。

### 3.2 枚举 → 文本

```c
const char* <Module>_StatusText(int32_t status);   // 静态存储期；未知码返回非空占位串
```

### 3.3 详细错误消息（可选，按需）

两种形态，模块**二选一**并保持一致：

| 形态 | 签名 | 适用 |
|---|---|---|
| 线程局部缓冲 | `int <Module>_GetLastErrorMessage(char* buffer, size_t bufferSize)` | 无句柄、全局/线程级失败 |
| 句柄关联 | `const char* <Module>_GetLastError(<Module>Handle handle)` | 以句柄为单位的作业/会话 |

> 返回值约定：`GetLastErrorMessage` 成功返回 0（`_OK`），失败返回负错误码（如 `_ERR_NULL_POINTER`、`_ERR_BUFFER_TOO_SMALL`）。**不返回写入长度**。缓冲不足时通过错误码区分，不以负值表示长度不足。

### 3.4 当前现状与差距

| 模块 | 形态 | 差距 |
|---|---|---|
| CrashHandler | `int _GetLastErrorMessage(char*,size_t)` | 需补 `CrashHandler_StatusText(int32_t)`；返回 `int` 而非枚举类型 |
| License | `int _GetLastErrorMessage(char*,size_t)` | 需补 `License_StatusText(int32_t)`；返回 `int` 而非枚举类型 |
| Engraving | `int _GetLastErrorMessage(char*,size_t)` | 需补 `Engraving_StatusText(int32_t)`；错误码为匿名 enum 无 typedef，C 调用需 `enum` 关键字 |
| Log (`SyLog_`) | 无失败 API | N/A（仅版本查询） |
| Nesting | `const char* _GetLastError(handle)` + `Nesting_StatusText(int32_t)` | **符合** |
| Renderx | 直接返回 `RxResult : int32_t` | 需补 `rxStatusText(int32_t)`（现有 `rxResultName(RxResult)` 签名/命名不符）；`RxResult` 已显式底层类型 ✅ |
| Vision | 返回 `int32_t`（即 `VisError` 值） | 需补 `Vision_StatusText(int32_t)`（现有 `Vision_ErrorName(int32_t)` 功能等价、命名不符）；无详细错误消息 API（两种形态皆缺） |
| GeoModel (`GeoModelCore`) | 无 C-ABI 失败 API（仅 C++ 层 `GmcStatus`） | 需补 `GeoModel_StatusText(int32_t)` 与二选一详细错误消息 API |
| Hardware | 无 C-ABI 导出（纯 C++ DLL） | **不在本约定范围**；C++ 层有 `hwErrorName(HwError)` 但非 extern "C" |
| SanyiPlugin | `SanyiPlugin_GetLastError(handle)` + `SanyiPlugin_ResultText` | 符合（独立插件约定） |

---

## 4. 类型与边界约定（现行，重申）

1. 跨边界只用 **POD**、**句柄**、**明确错误码**；不暴露 STL 容器/`std::string` 作为契约。
2. 字符串出参用 `(char* buffer, size_t bufferSize)`；返回值用静态存储期的 `const char*` 仅限"只读、不释放"。**bufferSize 必须为 `size_t`**，不得用 `int32_t`/`int` 等窄类型。
3. `enum class` 用于 C ABI 时必须显式指定底层类型（如 `: int32_t`）。
4. 导出宏统一 `<MODULE>_EXPORTS` 控制 `dllexport/dllimport`（CrashHandler/License/Log/Engraving/GeoModelCore/Vision/Nesting/Renderx 已完成；FileIO/Hardware 无 C ABI 导出；Utility/UI3D 待补齐）。
5. **C ABI 公共头文件必须可被 C 编译器消费**（仅含 `<stdint.h>`/`<stddef.h>`、`extern "C"` 块、POD 结构体、`typedef enum`、无 C++ 专用语法）。当前 CrashHandler、License、Engraving(CAPI.h)、Nesting 符合；Log、GeoModelCore、Vision、Renderx 需拆分纯 C 头。

---

## 5. 落地顺序（建议）

1. **Renderx**：补 `rxGetVersion()`（packed 语义版本 `(major<<16)|(minor<<8)|patch`），保留 `rxGetAbiVersion()` / `rxGetVersionString()`。
2. **状态文本**：为 CrashHandler/License/Engraving/GeoModelCore/Vision 补 `<Module>_StatusText(int32_t)`；Renderx 将 `rxResultName` 重命名/别名为 `rxStatusText(int32_t)`；Vision 将 `Vision_ErrorName` 重命名/别名为 `Vision_StatusText(int32_t)`。
3. **详细错误消息**：为 Vision/GeoModelCore/Renderx 补齐二选一形态（`_GetLastErrorMessage` 或 `_GetLastError(handle)`）。
4. **C 头文件拆分**：为 Log/GeoModelCore/Vision/Renderx 提供纯 C 兼容头（仅 extern "C" + stdint.h/stddef.h + POD），现有 C++ 头另存为 `*API.h`/`*DLL.h`。
5. **Utility/UI3D**：补齐 `*_GetVersion()` 返回 `uint32_t` packed、`*_GetVersionString()`、错误约定；或确认移出 C ABI 面（改为内部 C++ DLL）。
6. **自检扩展**：在 CI 或脚本中校验：① 每个 C ABI 头提供 `_GetVersion` + `_GetVersionString`；② 头文件可被 C 编译器编译通过；③ 存在 `_StatusText(int32_t)`；④ 存在二选一详细错误消息 API；⑤ 字符串 out-param 使用 `size_t`。

> 变更均为**新增**函数，不改动既有签名，避免破坏现有消费者；老函数保留直至消费者迁移完成。

---

## 6. 自检清单（新增 C ABI 模块时）

- [ ] `<Module>_GetVersion()` 返回 packed 语义版本 `(major<<16)|(minor<<8)|patch`
- [ ] `<Module>_GetVersionString()` 返回静态版本串 `"MAJOR.MINOR.PATCH"`
- [ ] 公共头文件提供 `#define <MODULE>_MAKE_VERSION(major,minor,patch)` 与 `<MODULE>_VERSION` 宏
- [ ] 需要 ABI 兼容计数时提供 `<Module>_GetAbiVersion()`
- [ ] 失败 API 返回 `int32_t`，取值为 `<Module>Status`/`<Module>Result` 枚举常量（成功 = 0）
- [ ] 提供 `<Module>_StatusText(int32_t)` 返回静态存储期字符串，未知码返回非空占位串
- [ ] 详细消息二选一并保持一致：`int <Module>_GetLastErrorMessage(char* buffer, size_t bufferSize)`（成功返回 0，失败返回负错误码） 或 `const char* <Module>_GetLastError(<Module>Handle handle)`
- [ ] 导出宏 `<MODULE>_EXPORTS` 控制 `dllexport/dllimport`
- [ ] 跨边界仅 POD/句柄/错误码；不暴露 STL/`std::string`/C++ 类
- [ ] 字符串出参使用 `(char* buffer, size_t bufferSize)`，bufferSize 为 `size_t`
- [ ] `enum class` 用于 C ABI 时显式指定底层类型（如 `: int32_t`）
- [ ] C ABI 公共头文件可被 C 编译器编译通过（仅 `<stdint.h>`/`<stddef.h>`、`extern "C"`、POD、`typedef enum`）
