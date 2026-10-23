# Main/Src/License —— 激活对话框与授权交互

本目录只有激活对话框（`LicenseDialog`）。它负责**用户可见的激活交互**：

- 显示本机机器码（可一键复制）
- 接收注册码并调用 `License_Activate`
- 成功/失败提示

授权判定、试用期、注册码签发、防篡改等逻辑都在
[`Licensing/License`](../../../Licensing/License)（`License.dll`）里；
启动时的授权闸门在
[`Main/Src/Runtime/LicensingBootstrap.cpp`](../Runtime/LicensingBootstrap.cpp)；
功能授权集（features）的消费在
[`Main/Src/UI/ClientConfig/UiFeatureGate.h`](../UI/ClientConfig/UiFeatureGate.h)。

本文覆盖两层内容：**使用流程**（用户看到什么、怎么激活）与**代码逻辑**（谁调用谁、数据存在哪、校验怎么走）。

---

## 1. 文件与职责速查

| 文件 | 职责 |
|------|------|
| `Main/Src/License/LicenseDialog.h/.cpp` | 激活对话框：机器码展示、注册码输入、激活结果提示 |
| `Main/Src/Runtime/LicensingBootstrap.cpp` | 启动授权闸门：许可证预检 → 试用期流程 → 激活后加载 features |
| `Licensing/License/Src/LicenseManager.cpp` | 注册码验签、payload 解析、`license.key` 读写、激活节流接入 |
| `Licensing/License/Src/MachineFingerprint.cpp` | 机器码生成（跨平台） |
| `Licensing/License/Src/TrialManager.cpp` | 试用期记录与判定（文件 + Windows 注册表） |
| `Licensing/License/Src/LicenseGuard.cpp` | 散射态防篡改守卫（Flavor 化检查） |
| `Licensing/License/Src/LicenseThrottle.cpp` | 激活失败节流（指数退避 + 硬锁定） |
| `Licensing/License/Src/StrEncrypt.h` | 编译期字符串加密（内嵌公钥、错误文案） |
| `Licensing/License/Src/KeygenTool.cpp` | RSA 密钥对生成 / 注册码签名实现（签发侧，不随主程序分发） |
| `Main/Src/UI/ClientConfig/UiFeatureGate.cpp` | features 字符串 → 功能白名单；菜单/工具栏按此过滤 |
| `Main/Src/UI/ClientConfig/UiConfigSelfCheck.cpp` | 启动自检：JSON 配置 / 编译开关 / License 三侧交叉核对 |

C ABI 契约见 [`LicenseDLL.h`](../../../Licensing/License/Include/License/LicenseDLL.h)，
签发侧 API 见 [`LicenseKeygen.h`](../../../Licensing/License/Include/License/LicenseKeygen.h)。

---

## 2. 端到端链路

```text
签发侧（厂商）                                客户端（SanYiCAD）
──────────────                                ────────────────────
机器码 ──► 注册码（RSA-2048 + SHA-256 签名） ──► 激活对话框 ──► License_Activate
                                                    │
                                            machine 匹配? 未过期? 验签通过?
                                                    │
                                                    ▼
                                          configDir/license.key（明文注册码）
                                                    │
每次启动：LicensingBootstrap::runGate ──► License_Check ──► 成功则
                                          UiFeatureGate 加载 features
                                                    │
                                                    ▼
                                    菜单/工具栏按 feature 显示或隐藏
```

注册码是**离线**的：客户端只持有公钥（编译进 DLL 并加密存储），签发方持有私钥，不需要联网。
`IOnlineVerifier` 接口已预留，但当前没有接入在线校验。

---

## 3. 用户视角：启动与注册流程

启动时先看本机是否已有有效许可证，再看试用期。三种结果：

### 3.1 已激活（有有效许可证）

- 不弹任何对话框，直接进入主界面。
- features 立即生效（菜单/工具栏按授权过滤）。
- **不会创建试用记录**（不写 `trial.info`，也不写 Windows 注册表）。

### 3.2 试用期内（未激活）

- 弹「Trial Period」对话框，显示剩余天数。
- `Register Now`：打开激活对话框；激活成功 → 进入正式版；取消 → 以试用模式继续。
- `Continue Trial`：以试用模式继续，功能不受限（`UiFeatureGate` 置为无限制）。

### 3.3 试用已过期（未激活）

- 弹「Trial Expired」对话框，只有 `Register Now` / `Exit`。
- `Exit`：进程直接退出（退出码 `-1`）。
- `Register Now`：打开激活对话框；取消 → 退出；激活成功 → 继续启动。

### 3.4 激活对话框怎么用

1. 对话框显示 **Machine Code**（40 位十六进制），点 `Copy Machine Code` 复制。
2. 把机器码发给厂商，厂商用私钥签发注册码。
3. 粘贴注册码，点 `Activate`。
4. 成功：弹框显示到期日与 features，对话框 `accept()`；失败：状态行显示具体原因
   （绑定到其他机器 / 已过期 / 注册码无效 / 节流等待中）。

对话框本身**不加载 features**：激活成功后由 `LicensingBootstrap` 重新探测一次许可证再
`UiFeatureGate::loadFromLicenseString`，避免对话框与启动流程各维护一套授权状态。

### 3.5 常见使用场景

| 场景 | 行为 |
|------|------|
| 换机器 | 机器码变化 → 注册码绑定校验失败 → 提示绑定其他机器，需重新签发 |
| 重装系统 / 换网卡 | 机器码可能变化（见第 5 节），需重新签发 |
| 卸载重装（同一台机器） | `license.key` 位于用户数据目录，通常保留；删除后需重新激活 |
| 试用记录被删 | 会重新获得 30 天试用（当前已知弱点，见第 8 节） |
| 时间回拨 | 许可证侧按本地日期比较，`LicenseGuard` 的时间戳检查可发现部分回拨 |
| 连续输错注册码 | 触发节流：先退避等待，累计失败达阈值后硬锁定 24 小时 |

---

## 4. 启动授权闸门（代码逻辑）

入口：`Main/Src/Runtime/CADApplicationRuntime.cpp` 调用
`LicensingBootstrap::runGate(configDir)`；`configDir` 来自
`AppPathManager::configDir()`（`<AppLocalData>/SanYiCAD/config`）。
返回 `0` 继续启动；`-1`（用户退出试用/取消激活）与 `-3`（拒绝激活）直接作为进程退出码。

### 4.1 Phase 0：许可证预检

```cpp
const LicenseProbe license = probeLicense(config);
```

`probeLicense()` 只做只读探测：创建短生命周期上下文 → `License_Check` → `License_GetInfo` → 销毁。
它同时给出 `checkEnabled / valid / LicenseInfo`，后续不再重复验签。

```cpp
if (!license.checkEnabled) { setUnrestricted(true); return 0; }   // 未启用校验
if (license.valid)         { applyLicensedFeatures(license); return 0; } // 已激活
```

两条分支都**直接返回**：不调用 `Trial_SetConfigDir` / `Trial_Check`。
这是刻意的——`Trial_Check()` 首次调用会在无记录时**写** `trial.info`（Windows 还写注册表），
有授权或不需要校验时不应产生这份状态。

### 4.2 Phase 1：试用期检查（仅无有效许可证时）

- `Trial_SetConfigDir(configDir)` → `Trial_Check(&remainingDays)`
- 返回 `-1`：过期 → 过期对话框 → 激活或退出（`-1`）
- 返回 `0` 且天数 > 0：试用中 → 试用对话框 → `Register` 或 `Continue Trial`
- 其他：按试用模式继续（防御性兜底）

用户选 `Continue Trial` 或取消激活时置 `inTrialMode = true`，并把 `UiFeatureGate` 置为无限制。

### 4.3 Phase 2：激活后的复核

`inTrialMode` 为真直接返回。否则再 `probeLicense` 一次：

- 有效（例如刚在试用对话框里注册成功）→ 应用 features → 返回 `0`
- 无效 → 打开 `LicenseDialog`；拒绝 → `-3`；接受后再次探测并应用 features

---

## 5. 注册码格式与机器码

### 5.1 机器码

`MachineFingerprint::Generate()` 取三部分拼接后做 SHA-1，输出 40 位十六进制：

```text
SHA1( MachineGuid | 卷序列号 | 第一块非回环网卡 MAC )
```

| 平台 | 机器唯一标识 | 卷标识 | MAC |
|------|--------------|--------|-----|
| Windows | 注册表 `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid` | `C:\` 卷序列号 | `GetAdaptersAddresses` 第一块非回环、长度 6 的适配器 |
| macOS | `IOPlatformUUID`（IOKit） | 根卷 `fsid` | `getifaddrs` + `AF_LINK` |
| Linux | `/etc/machine-id`（回退 `/var/lib/dbus/machine-id`） | 根分区 `st_dev` | `getifaddrs` + `AF_PACKET` |

注意：Windows 上「第一块长度 6 的适配器」可能命中 Hyper-V / VMware 等虚拟网卡，
插拔 USB 网卡或启用虚拟网卡都会改变机器码，这是现场「换机」投诉的常见来源。

### 5.2 注册码

```text
<urlsafe-base64(payload)>.<urlsafe-base64(RSA-SHA256(payload))>
```

- Base64：标准表去掉 `=` 填充，`+`→`-`，`/`→`_`（URL-safe）。
- 签名：RSA-2048 + SHA-256（OpenSSL `EVP_DigestVerify*`）。
- payload 为 UTF-8 文本，`|` 分隔，固定 5 段：

```text
machineCode|expiryDate|features|issueDate|customerName
```

| 字段 | 约束 | 说明 |
|------|------|------|
| machineCode | 必须与本机机器码完全一致 | 否则报「绑定到其他机器」 |
| expiryDate | `YYYY-MM-DD`，当天仍有效 | `expiryDate < 今天` 即过期 |
| features | 逗号/分号/空白分隔；`*` 或 `all` 表示全功能 | 写入 `LicenseInfo.features[256]` 上限 |
| issueDate | `YYYY-MM-DD`，不得晚于当前日期 | 防未来签发 |
| customerName | 展示用 | `LicenseInfo.customerName[128]` |

验签失败、格式错误、解码失败统一返回 `Invalid registration code`，不区分具体原因，
避免被用来探测注册码结构。

### 5.3 签发（厂商侧）

签发只用私钥，私钥不进入客户端。可用的 C ABI：

```c
// 生成密钥对（一次性）
LicenseKeygen_GenerateKeyPair("private.pem", "public.pem");

// 签发注册码
char regCode[4096];
LicenseKeygen_GenerateRegCode(machineCode, "2028-12-31", "pro,nesting",
                              "2026-10-22", "Customer", "private.pem",
                              regCode, sizeof(regCode));
```

公钥以密文形式内嵌在 `LicenseManager.cpp`（`DEFINE_ENCRYPTED_STR(kPublicKey, ...)`），
运行时解密使用；单元测试通过 `LicenseTest_SetPublicKeyPem` 注入临时公钥。

> 现状提醒：`Config.cmake` 里有 `BUILD_KEYGEN_TOOL`（默认 OFF），但仓库中
> `Tools/KeygenTool` 目录并不存在，README 里 `KeygenTool genkey/genreg` 的命令行工具
> 当前无法构建。签发请直接调用上面的 `LicenseKeygen_*` API（或自行补 CLI main）。

---

## 6. 数据落盘

所有路径都以 `configDir`（`<AppLocalData>/SanYiCAD/config`）为根，跨平台由 Qt 路径提供：

| 文件 | 内容 | 说明 |
|------|------|------|
| `license.key` | 注册码原文（明文文本） | `License_Activate` 成功后写入；启动时读取并验签 |
| `trial.info` | `start_date=YYYY-MM-DD` / `machine_hash=...` / `trial_days=30` | 仅「无有效许可证」的启动会创建 |
| `license.throttle` | `Base64(failures|lastAttempt|lockUntil).Base64(HMAC-SHA256)` | 激活失败节流状态；HMAC 密钥 = 固定盐 + 机器码 |
| Windows 注册表 | `HKCU\Software\SanYiCAD\Trial` | 与 `trial.info` 同时写入/读取，作为试用记录的冗余 |

典型位置：

- Windows：`C:\Users\<user>\AppData\Local\SanYiCAD\config\`
- macOS：`~/Library/Application Support/SanYiCAD/config/`
- Linux：`~/.local/share/SanYiCAD/config/`（未注入时 `TrialManager` 回退 `~/.config/sanyicad`）

---

## 7. 试用期实现细节

`TrialManager`（单例）状态机：`NotStarted / Active / Expired`。

- `init()`：先读 `trial.info`；Windows 上文件无效再读注册表。记录存在且校验通过（日期格式 + 机器指纹匹配）→ 按剩余天数判 `Active/Expired`。
- 无有效记录 → **以当天为起点创建 30 天试用**，写文件（Windows 再写注册表），状态 `Active`。
- `remainingDays() = trialDays - daysBetween(startDate, today)`，日期差用 UTC 正午计算避免夏令时偏差。

C ABI 包装 `Trial_Check(int* remainingDays)`：

- 返回 `-1` 表示已过期，`0` 表示可继续（含异常兜底：任何异常都放行，避免卡死启动）。
- **进程内静态缓存**：首次调用后结果被缓存，之后的 `Trial_Check` 不再重新读盘；
  `Trial_SetConfigDir` 也不会让已缓存的检查失效。同一进程内换配置目录无效。
- 头文件注释里的 `-2`（初始化失败）目前不会返回。

试用记录绑定机器指纹（对机器码再做一次 SHA-1）：
把 `trial.info` 拷到另一台机器会因 `machine_hash` 不匹配而被视为无效并重建记录。

---

## 8. 防篡改与节流（实现了什么、接了什么）

| 机制 | 实现 | 当前接入状态 |
|------|------|--------------|
| RSA-2048 验签 | `LicenseManager::VerifyRegCode` | 已接入启动/激活 |
| 公钥/敏感文案加密 | `StrEncrypt.h`（XOR + MSVC `secure` 段） | 已接入 |
| 机器指纹绑定 | `MachineFingerprint` | 已接入许可证与试用 |
| 散射态守卫 | `LicenseGuard`：tokenA/B、crossSum、timestamp，`License_Check` 成功时 `MarkValid` | 内存态已维护；**应用未调用 `License_GuardCheck`**（保存/导出/渲染前的二次校验尚未接线） |
| 激活节流 | `LicenseThrottle`：前 3 次免费，之后指数退避 30s→3600s，连续 20 次失败硬锁定 24h，成功清零 | 已接入 `License_Activate` |
| 编译期开关 | `SANYI_ENABLE_LICENSE` → `LicenseInternal::g_checkEnabled` | **仓库内无任何 CMake 定义**，即当前所有构建的许可校验都是关闭状态 |
| 运行期覆盖 | `LicenseConfig::enableCheck`：`-1` 不覆盖（`License_ConfigInit` 默认）、`0` 关闭、`1` 开启 | 已接入，`License_Create` 只在 `>= 0` 时改写全局开关 |

之所以把默认值定为 `-1`：若默认是 `1`，任何一次 `License_Create`（例如打开注册对话框）
都会把开发构建的全局开关从「关闭」翻成「开启」，出现「点注册→取消→进程退出」这类隐性问题。

已知上限（纯离线校验的固有代价）：

- 删除 `trial.info` / 注册表项即可重置试用。
- 删除 `license.throttle` 即可重置节流计数。
- 机器码依赖网卡 MAC，虚拟网卡会让机器码漂移。
- 本地日期可被修改（`LicenseGuard` 的 timestamp 只能发现部分回拨，且 Guard 尚未接到业务路径）。

---

## 9. features 与功能分级

`LicenseInfo.features` 是逗号/分号/空白分隔的功能 ID 列表，大小写不敏感：

- `*` 或 `all` → `UiFeatureGate` 进入无限制模式，全部放行。
- 空字符串 → 白名单为空，只放行「UI 配置里没声明 feature」的基础项。
- 其他 → 只放行白名单内的 feature。

消费方：

- `UiLayoutBuilder`（约第 47 行）构建菜单/工具栏时用 `UiFeatureGate::isAllowed(feature)` 决定是否出现。
- `UiConfigSelfCheck` 在启动后做「JSON 声明了 feature / 编译开关是否编进来 / License 是否授权」三侧交叉，
  日志前缀 `[ConfigSelfCheck]`，用于排查「授权了但点了没反应」。

试用模式下 `UiFeatureGate` 被显式置为无限制，因此试用用户能看到全部功能。

---

## 10. 错误与排查

| 现象 | 可能的日志/返回值 | 排查点 |
|------|-------------------|--------|
| 启动直接退出 | 退出码 `-1` / `-3` | 看 `[LicensingBootstrap]` 日志判断是试用过期还是拒绝激活 |
| 注册码被拒 | `Invalid registration code` | 注册码截断（复制少了字符）、公钥与私钥不配对 |
| 提示绑定其他机器 | `This license is bound to a different machine` | 机器码已变（网卡/卷/系统重装） |
| 提示已过期 | `License expired (YYYY-MM-DD)` | 到期日与本地日期比较 |
| 激活被节流 | `LICENSE_ERR_LOCKED` + 等待提示 | `config/license.throttle` 状态；可删除重置 |
| 功能菜单缺失 | `[UiFeatureGate] Feature 'x' is not licensed` | features 未包含该项，或 `*`/`all` 未生效 |
| 配置自检报告不一致 | `[ConfigSelfCheck] licensedNotCompiled / compiledNotLicensed` | License features 与 `BUILD_*` 开关、JSON 配置三者对不上 |

日志文件：`<AppLocalData>/SanYiCAD/logs/SanYiCAD.log`（及 `.error.log`）。

---

## 11. 相关文档

- 授权分级设计（功能等级、场景表、流程图）：[`Docs/01-当前架构/授权等级与功能分级设计.md`](../../../Docs/01-当前架构/授权等级与功能分级设计.md)
- License 库 API 与防篡改详解：[`Licensing/License/README.md`](../../../Licensing/License/README.md)
- C ABI 风格约定：[`Docs/01-当前架构/C-ABI风格约定.md`](../../../Docs/01-当前架构/C-ABI风格约定.md)
