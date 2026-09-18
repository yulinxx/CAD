# C ABI 风格约定

> **性质**：标准/规范文档（待落地）。
> **范围**：所有对外暴露 `extern "C"` / C ABI 的模块（CrashHandler、License、Log、Engraving、GeoModelCore、Vision、Nesting、Renderx、Hardware、FileIO 等）。
> **背景**：见 [`框架现状与修理计划.md`](框架现状与修理计划.md) §2.4 / §3.2「继续统一各 facade 的版本查询和错误码」。

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

- `<Module>` = 该 DLL 既有的导出符号前缀（如 `CrashHandler` / `License` / `SyLog` / `Nesting` / `rx`）。
  **同一 DLL 内前缀必须唯一且固定**，不混用（例如 `rx` 与 `RenderX_` 二选一）。
- packed 编码统一为 `(major<<16)|(minor<<8)|patch`（与 `LICENSE_MAKE_VERSION` / `CRASHHANDLER_MAKE_VERSION` 一致）。
- 建议每个模块提供 `#define <MODULE>_MAKE_VERSION(major,minor,patch)` 与 `<MODULE>_VERSION` 宏。

### 2.4 当前现状与差距

| 模块 | `_GetVersion` | `_GetVersionString` | `_GetAbiVersion` | 差距 |
|---|:---:|:---:|:---:|---|
| CrashHandler | ✅ | ✅ | — | 缺 `_GetAbiVersion`（可选） |
| License | ✅ | ✅ | — | 同上 |
| Log (`SyLog_`) | ✅ | ✅ | — | 同上 |
| Engraving | ✅ | ✅ | — | 同上 |
| GeoModelCore | ✅ | ✅ | — | 同上 |
| Vision | ✅ | ✅ | — | 同上 |
| Nesting | ✅ | ✅ | ✅ | 已补齐 `Nesting_GetVersion`（本次） |
| Renderx (`rx`) | — | ✅ | ✅ (`rxGetAbiVersion`) | 已补 `rxGetVersionString`；数值版本沿用 ABI（`rxGetAbiVersion`，属既定变体） |

> 结论：编码一致；命名与覆盖已收敛。Nesting 补齐 packed 语义版本；
> Renderx 的数值版本沿用 ABI 计数（`rxGetAbiVersion`），不再另设 packed 语义版本。

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

> 返回值约定：`GetLastErrorMessage` 返回写入长度（≥0）或负值表示缓冲不足。

### 3.4 当前现状与差距

| 模块 | 形态 | 差距 |
|---|---|---|
| CrashHandler | `int _GetLastErrorMessage(char*,size_t)` | 需补 `_StatusText`（若用枚举） |
| License | `int _GetLastErrorMessage(char*,size_t)` | 同上 |
| Engraving | `int _GetLastErrorMessage(char*,size_t)` | 同上 |
| Nesting | `const char* _GetLastError(handle)` + `_StatusText(int32_t)` | 符合 |
| Renderx | 直接返回 `RxResult`（`enum class`） | 需补 `rxStatusText`；`enum class` 作 C ABI 需确认底层类型/命名 |
| GeoModel / Hardware / Vision | `GmcErrorCode` / `HwError` / `VisError` | 需补 `_StatusText` |

---

## 4. 类型与边界约定（现行，重申）

1. 跨边界只用 **POD**、**句柄**、**明确错误码**；不暴露 STL 容器/`std::string` 作为契约。
2. 字符串出参用 `(char* buffer, size_t bufferSize)`；返回值用静态存储期的 `const char*` 仅限"只读、不释放"。
3. `enum class` 用于 C ABI 时必须显式指定底层类型（如 `: int32_t`）。
4. 导出宏统一 `<MODULE>_EXPORTS` 控制 `dllexport/dllimport`（已达成，见导出宏审计）。

---

## 5. 落地顺序（建议）

1. **Nesting**：补 `Nesting_GetVersion()`（packed 语义版本），保留 `Nesting_GetAbiVersion()`。
2. **Renderx**：补 `rxGetVersion()` + `rxGetVersionString()`，并统一 `rx` 前缀（不再出现 `RenderX_`）。
3. **状态文本**：为返回枚举的模块补 `<Module>_StatusText(int32_t)`。
4. **自检**：在 CI 或脚本中校验"每个 C ABI 头至少提供 `_GetVersion` + `_GetVersionString`"。

> 变更均为**新增**函数，不改动既有签名，避免破坏现有消费者；老函数保留直至消费者迁移完成。

---

## 6. 自检清单（新增 C ABI 模块时）

- [ ] `<Module>_GetVersion()` 返回 packed 语义版本
- [ ] `<Module>_GetVersionString()` 返回静态版本串
- [ ] 需要 ABI 兼容计数时提供 `<Module>_GetAbiVersion()`
- [ ] 失败 API 返回 `<Module>Status`（成功 = 0）
- [ ] 提供 `<Module>_StatusText(int32_t)`
- [ ] 详细消息二选一（`_GetLastErrorMessage(buf,n)` 或 `_GetLastError(handle)`）
- [ ] 导出宏 `<MODULE>_EXPORTS`，跨边界仅 POD/句柄/错误码
