# LicenseManager 模块文档

## 概述

LicenseManager 是 SanYiCAD 的离线注册码校验模块，基于 **RSA-2048 非对称加密 + 机器指纹绑定** 实现。

采用**先宽后严**的设计：**1 个函数控制全局开关**，改一行代码即可完全禁用该功能，对主程序零侵入。

---

## 设计思路

### 为什么选 RSA 非对称加密而非对称加密

| 方案 | 问题 |
|------|------|
| 对称加密（AES） | 密钥必须嵌在程序里，反编译即可提取，破解者可以自己签发注册码 |
| 自定义算法（XOR/CRC） | 极易被逆向分析、写出注册机 |
| **RSA 非对称加密** | **私钥签发、公钥验签**。公钥嵌在程序中，私钥只保存在开发者手中。即使反编译拿到公钥，也无法伪造注册码 |

RSA-2048 是目前安全性与性能的最佳平衡点。

### 为什么选机器指纹绑定而非纯注册码

纯注册码（如仅验证一个字符串）可以被一个正版用户全网分享。绑定机器指纹后，注册码与当前硬件绑定，无法跨机器使用。

指纹来源组合：
- **MachineGuid**（注册表，Windows 安装唯一 ID）
- **卷序列号**（C 盘，格式化会变化）
- **MAC 地址**（第一块物理网卡）

三者组合经 SHA256 哈希，兼顾**唯一性**与**稳定性**。

### 为什么不用单 bool 做验证结果

```
class LicenseManager {
    bool m_isValid;  // ← 逆向工程师只需内存中把 0 改成 1
};
```

**单 bool 是整个校验链中最脆弱的环节**。攻击者不需要破解 RSA，只需要用 Cheat Engine 等工具在内存中找到这个 bool 并锁定为 true。

解决方案：**散射状态（Scattered State）**—— 将"是否授权"编码到多个相互关联的 32 位整数中：

```cpp
struct GuardState {
    volatile uint32_t tokenA_hi;     // 固定魔数
    volatile uint32_t tokenA_lo;     // 魔数的混淆结果
    volatile uint32_t tokenB_hi;     // 另一组魔数
    volatile uint32_t tokenB_lo;     // 另一组混淆结果
    volatile uint32_t crossSum;      // 以上四者的交叉校验和
    volatile uint32_t timestamp;     // 授权时间戳
};
```

- **patch 单个变量** → 校验和立刻不匹配
- **patch 两个变量** → 混淆函数使它们必须满足数学关系
- **全部 patch** → 攻击者需理解并还原整个散射逻辑

### 为什么不同功能用不同 Flavor 校验

```
LicenseGuard::Check(Flavor_Save)    // 只检查 A 组 + 交叉和
LicenseGuard::Check(Flavor_Export)  // 只检查 B 组 + 交叉和
LicenseGuard::Check(Flavor_Render)  // 检查 A 组 + B 组 + 时间戳
```

不同功能走不同的检查路径，攻击者 patch 了一个地方，另一个功能仍然会触发校验失败。patch 所有出入点的成本远高于购买正版。

### 为什么加密公钥和错误字符串

公钥是 RSA 校验的信任锚点，错误字符串是搜索关键逻辑的定位标记。静态分析工具（IDA Pro、Ghidra）会搜索 `"-----BEGIN PUBLIC KEY-----"` 和 `"expired"` 来定位校验代码。

通过 **XOR 加密 + 自定义 PE 节区**：

1. 字符串以密文形式存储在单独的 `.secure` 节区，不在 `.rdata` 中
2. 运行时首次访问才解密并缓存
3. 解密密钥分散在代码中，不形成集中的解密函数

```
IDA 中搜索 "BEGIN PUBLIC KEY" → 无结果
搜索 "license" → 无结果
搜索 "expired" → 无结果
```

### 为什么预留在线验证接口

纯离线校验可以被物理断网环境绕过（虽然机器绑定做了限制）。预留 `IOnlineVerifier` 接口的目的是：

- 后续可添加 HTTP/HTTPS 联网验证，不必改现有代码
- 支持**混合模式**：离线校验通过 + 在线可选增强
- 或**强制模式**：离线 + 在线都必须通过

```cpp
class IOnlineVerifier {
    virtual bool Verify(const LicenseInfo& info) = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool IsRequired() const { return false; }
};
```

通过 `LicenseManager::SetOnlineVerifier()` 注入，零侵入。

---

## 使用的库

| 库 | 用途 | 引入方式 | 依赖级别 |
|----|------|----------|----------|
| **OpenSSL** (`libcrypto`) | RSA 验签、SHA256 哈希、Base64 编解码 | `find_package(OpenSSL REQUIRED)` | 编译依赖 |
| **Windows API** (`iphlpapi`) | 获取 MAC 地址 | `target_link_libraries(Iphlpapi.lib)` | 仅 Windows |
| **Windows API** (`advapi32`) | 注册表读取 MachineGuid | 默认链接 | 仅 Windows |
| **C++17** (`filesystem`) | 配置文件路径操作 | CMake `cxx_std_17` | 标准库 |
| **Qt 6** (`Widgets`) | 激活对话框 UI | `find_package(Qt6)` | 仅 LicenseDialog |

OpenSSL 已通过 vcpkg 集成到项目（`openssl:x64-windows 3.6.1`），无需额外安装。

---

## 文件结构

### 主程序模块（必须）

```
Main/Src/License/
├── LicenseManager.h         ← 主接口 + IsLicenseCheckEnabled() 全局开关
├── LicenseManager.cpp       ← RSA 验签 + 散射状态 + 加密解密 + 在线验证集成
├── LicenseGuard.h           ← 散射多路校验器
├── LicenseGuard.cpp         ← 散射状态实现（6 个关联变量 × 5 种 Flavor）
├── StrEncrypt.h             ← XOR 字符串解密 + SECURE_SECTION 宏
├── OnlineVerifier.h         ← 在线验证抽象接口
├── MachineFingerprint.h     ← 硬件指纹接口
├── MachineFingerprint.cpp   ← Windows API 采集指纹 + SHA256 哈希
├── LicenseDialog.h          ← Qt 激活对话框
├── LicenseDialog.cpp        ← Qt 激活对话框实现
├── KeygenTool.h             ← 注册码生成工具接口（仅头文件，供 KeygenTool 引用）
└── LicenseManager.md        ← 本文档
```

### 开发工具（可选编译）

```
Tools/KeygenTool/
├── main.cpp                 ← CLI 入口
├── KeygenTool.cpp           ← RSA 密钥生成 + 签名实现
└── CMakeLists.txt           ← 独立 CMake 目标

Tools/encrypt_string.py      ← 字符串加密工具（生成 SECURE_SECTION 数组）
```

### 构建集成

```
Main/CMakeLists.txt
├── find_package(OpenSSL REQUIRED)
├── target_link_libraries(... OpenSSL::SSL OpenSSL::Crypto Iphlpapi.lib)
└── file(GLOB_RECURSE MAIN_SOURCES "Src/*.cpp")  ← 自动包含 License/ 下文件
```

GLOB 会自动包含新增的 `LicenseGuard.cpp`，无需手动修改 CMakeLists.txt。

---

## 逻辑流程

### 启动校验 + 散射状态

```
SanYiCAD.exe
  │
  ├─ main() → runCADApplication()
  │   └─ CADApplicationRuntime::run()
  │       │
  │       ├─ AppInitializer::initialize()
  │       │
  │       ├─ IsLicenseCheckEnabled()?
  │       │   └─ false → LicenseGuard::MarkValid() → 跳过所有
  │       │
  │       ├─ LicenseManager::CheckLicense()
  │       │   ├─ ReadLicenseFile()
  │       │   ├─ MachineFingerprint::Generate()
  │       │   ├─ VerifyRegCode()
  │       │   │   ├─ 解密公钥（StrEncrypt::Decrypt）
  │       │   │   ├─ base64 解码
  │       │   │   ├─ RSA 公钥验签
  │       │   │   ├─ 比对机器码
  │       │   │   └─ 校验过期日期
  │       │   ├─ [可选] IOnlineVerifier::Verify()
  │       │   └─ 通过 → LicenseGuard::MarkValid()
  │       │       ├─ tokenA_hi = 0x3E1F5A8C
  │       │       ├─ tokenA_lo = Scramble(tokenA_hi)
  │       │       ├─ tokenB_hi = 0x3E1FA104
  │       │       ├─ tokenB_lo = Scramble(tokenB_hi)
  │       │       ├─ crossSum  = A_hi ^ A_lo ^ B_hi ^ B_lo ^ 0x7C3D9B1E
  │       │       └─ timestamp = time(nullptr)
  │       │
  │       ├─ 通过 → AppBootstrapper 初始化
  │       └─ 不通过 → LicenseDialog 弹出
  │           ├─ 激活成功 → LicenseGuard::MarkValid() → 继续
  │           └─ 取消 → return -3
  │
  │  ── 后续运行时 ──
  │
  ├─ 保存文件时 → LicenseGuard::Check(Flavor_Save)
  │   ├─ VerifyGroupA()    : tokenA_lo == Scramble(tokenA_hi)
  │   └─ VerifyCrossSum()  : crossSum == 重新计算的交叉和
  │
  ├─ 导出文件时 → LicenseGuard::Check(Flavor_Export)
  │   ├─ VerifyGroupB()    : tokenB_lo == Scramble(tokenB_hi)
  │   └─ VerifyCrossSum()
  │
  ├─ 渲染场景时 → LicenseGuard::Check(Flavor_Render)
  │   ├─ VerifyGroupA()
  │   ├─ VerifyGroupB()
  │   └─ VerifyTimestamp() : 时间戳未冻结
  │
  └─ 激活对话框 → LicenseManager::Activate()
      ├─ VerifyRegCode()  ← 同上验签流程
      └─ 通过 → MarkValid() + 写入 license.key
```

---

## 用户使用指南

### 最终用户（激活软件）

1. 启动软件，弹出激活对话框
2. 对话框显示 **Machine Code**，点击 Copy 按钮复制
3. 将 Machine Code 发给软件销售方
4. 销售方返回 **Registration Code**（注册码字符串）
5. 粘贴到输入框，点击 Activate
6. 激活成功后即可正常使用

### 开发者（生成注册码与发布）

#### 首次准备密钥

```bash
# 编译 KeygenTool
cd build
cmake .. -DBUILD_KEYGEN_TOOL=ON
cmake --build . --target KeygenTool --config Release

# 生成 RSA-2048 密钥对
KeygenTool genkey private.pem public.pem
```

#### 将公钥嵌入程序

```bash
# 加密公钥并替换到 LicenseManager.cpp
python Tools/encrypt_string.py --key 0x9D "$(cat public.pem)"
```

将输出替换到 `LicenseManager.cpp` 中 `DEFINE_ENCRYPTED_STR(kPublicKey, ...)` 的数据部分。

> 也可使用密钥的 SHA256 末字节作为 `--key` 参数以确保唯一性。

#### 生成注册码

```bash
KeygenTool genreg \
  4c6b1638cf026fa5... \     # machineCode（用户提供）
  2028-12-31 \                # expiryDate
  all \                       # features（功能级别）
  2026-07-04 \                # issueDate
  "Customer Name" \           # customer
  private.pem                 # 私钥（勿上传 Git！）
```

输出注册码字符串，发给用户。

---

## 关键功能散射校验（二次校验）

在 **保存 / 导出 / 渲染** 等关键操作中加入运行时二次校验，防止绕过启动校验后未授权使用。

### 集成示例

#### 保存文件时

```cpp
// FileIO/FileIO/Src/FileSaver.cpp
#include "../../Main/Src/License/LicenseGuard.h"

bool SaveDocument(const char* path)
{
    // 运行时散射校验 —— 不同 Flavor 走不同检查路径
    if (!LicenseGuard::Check(LicenseGuard::Flavor_Save))
    {
        // 授权状态已被篡改，拒绝保存
        ShowError("License validation failed during save.");
        return false;
    }
    // ... 实际保存逻辑
    return true;
}
```

#### 导出文件时

```cpp
// FileIO/FileIO/Src/FileExporter.cpp
#include "../../Main/Src/License/LicenseGuard.h"

bool ExportToFormat(const char* path, Format fmt)
{
    if (!LicenseGuard::Check(LicenseGuard::Flavor_Export))
    {
        ShowError("License validation failed during export.");
        return false;
    }
    // ... 导出逻辑
    return true;
}
```

#### 渲染场景时

```cpp
// Render/Core/Src/SceneRenderer.cpp
#include "../../Main/Src/License/LicenseGuard.h"

void RenderFrame()
{
    if (!LicenseGuard::Check(LicenseGuard::Flavor_Render))
    {
        // 渲染降级或中止
        return;
    }
    // ... 渲染逻辑
}
```

### Check 失败后的行为选择

| 场景 | 推荐行为 |
|------|----------|
| 保存/导出 | **拒绝操作**，弹出提示，引导用户重新激活 |
| 渲染 | 轻度降级（如降低分辨率、加水印），或直接中止 |
| 启动时 | 弹出激活对话框，无法取消则退出 |
| 通用检查 | 仅记录日志，不阻断操作（辅助检测） |

---

## 后续扩展

### 1. 在线验证（HTTP API）

```cpp
// Network/Network/Src/OnlineLicenseVerifier.h
#include "OnlineVerifier.h"

class OnlineLicenseVerifier : public IOnlineVerifier
{
public:
    bool Verify(const LicenseInfo& info) override
    {
        // POST /api/verify
        // body: { machineCode, regCode, expiryDate, features }
        // 返回 200 = 有效
        return HttpClient::Post("https://license.sanyi-cad.com/verify", payload);
    }

    bool IsAvailable() const override
    {
        return HttpClient::Ping("https://license.sanyi-cad.com/ping", 3000);
    }

    const char* GetName() const override { return "SanYiCAD Online"; }

    bool IsRequired() const override { return true; } // 强制在线
};

// 注入到 LicenseManager
void AppBootstrapper::initialize()
{
    auto verifier = std::make_unique<OnlineLicenseVerifier>();
    m_licenseManager->SetOnlineVerifier(std::move(verifier));
}
```

### 2. 功能分级授权

payload 的 `features` 字段已支持分级：

```cpp
if (licenseMgr.GetLicenseInfo().features == "basic")
{
    // 禁用高级功能
    menuExportDXF->setEnabled(false);
    menuRender3D->setEnabled(false);
}
```

在 KeygenTool 签发时指定不同 features 值：
```bash
KeygenTool genreg <machine> 2028-12-31 basic ... private.pem
KeygenTool genreg <machine> 2028-12-31 pro   ... private.pem
```

### 3. 防时间回拨

当前 `IsExpired()` 使用系统时间。如需防用户调回时钟：

```cpp
// 方案 A：联网获取 NTP 时间
// 方案 B：将上次验证时间加密存入注册表
//        每次校验时比较：新时间 < 上次时间 → 检测到回拨

static std::string GetLastVerifiedTime()
{
    // 从 HKCU\Software\SanYi\CAD\LastVerify 读取加密时间戳
}

static bool DetectTimeRollback(const std::string& currentDate)
{
    std::string lastDate = GetLastVerifiedTime();
    return !lastDate.empty() && currentDate < lastDate;
}
```

### 4. 加密更多字符串

用 `Tools/encrypt_string.py` 加密任意敏感字符串：

```bash
python Tools/encrypt_string.py "license.key"
python Tools/encrypt_string.py "Software\\Microsoft\\Cryptography"
python Tools/encrypt_string.py "MachineGuid"
```

将输出粘贴到代码中，用 `kEncryptedString()` 获取解密值。

### 5. 增加更多指纹源

`MachineFingerprint::Generate()` 使用管道符 `|` 分隔多个标识，追加新源即可：

```cpp
// MachineFingerprint.cpp
std::string MachineFingerprint::Generate()
{
    std::string raw = GetMachineGuid() + "|"
                    + GetVolumeSerial() + "|"
                    + GetMacAddress() + "|"
                    + GetMotherboardId();  // 新增：WMI 查询主板序列号
    return Sha256Hex(raw);
}
```

指纹改变后，已有注册码会失效（正确行为 —— 硬件变动需重新激活）。

### 6. 不同 Flavor 增加权重

`LicenseGuard::Check()` 目前所有 Flavor 同等处理。可按功能重要性调整：

```cpp
// 渲染→3 次散射检查，保存→2 次
if (flavor == Flavor_Render)
{
    // 连续执行三次不同检查
    return CheckOne(Flavor_Render)
        && CheckOne(Flavor_Generic)
        && CheckOne(Flavor_Render);
}
```

---

## 安全层总览

```
┌──────────────────────────────────────────────────┐
│  Layer 6: [可选] 在线验证 IOnlineVerifier        │
│  联网双因子校验，可强制在线                        │
├──────────────────────────────────────────────────┤
│  Layer 5: 运行时二次校验 LicenseGuard::Check()     │
│  保存/导出/渲染时散射验证，patch 一处无关另一处     │
├──────────────────────────────────────────────────┤
│  Layer 4: 散射状态（无单 bool）                    │
│  6 个关联变量 + 交叉和 + 时间戳                    │
├──────────────────────────────────────────────────┤
│  Layer 3: 字符串加密 StrEncrypt                   │
│  公钥/错误信息不在 .rdata 中以明文出现              │
├──────────────────────────────────────────────────┤
│  Layer 2: 自定义 PE 节区                          │
│  加密数据放在 .secure 节，非标准 .rdata/.data      │
├──────────────────────────────────────────────────┤
│  Layer 1: RSA-2048 非对称加密                     │
│  私钥签发的注册码，公钥无法伪造                     │
├──────────────────────────────────────────────────┤
│  Layer 0: IsLicenseCheckEnabled()                 │
│  开发阶段直接 return false 即可免除整个安全系统     │
└──────────────────────────────────────────────────┘
```

## 安全注意事项

1. **私钥绝对不要提交到 Git 仓库**（已添加 `*.pem` 到 `.gitignore`）
2. **每次发布用新密钥**：重新 `genkey` → 替换加密公钥 → 重签所有注册码
3. **发布版程序加壳**：建议使用 VMProtect / Themida 进一步防反编译
4. **测试前先关闭校验**：`IsLicenseCheckEnabled() { return false; }` 避免开发时反复激活
