# License Library (License.dll)

## 功能描述

许可证验证库，为 SanYi CAD 项目提供软件授权管理能力。主要功能包括：

- **机器码生成** —— 基于硬件指纹生成唯一机器码
- **许可证激活** —— 通过注册码激活软件许可证
- **许可证校验** —— 每次启动/保存/导出时验证许可证有效性
- **防篡改机制** —— 原子变量散射校验 + 编译时字符串加密，防止逆向破解
- **密钥生成工具** —— 离线 RSA 密钥对生成与注册码签名（仅供内部使用）

## 使用方法

### 典型调用顺序

```
License_ConfigInit  →  License_Create  →  License_Check / License_Activate  →  License_Destroy
```

### 基本使用流程

```c
// 1. 初始化配置
LicenseConfig config;
License_ConfigInit(&config);
config.configDir = "C:/ProgramData/SanYiCAD";

// 2. 创建上下文
LicenseContext* ctx = License_Create(&config);

// 3. 获取机器码（发给管理员生成注册码）
char machineCode[128] = {0};
License_GetMachineCode(ctx, machineCode, sizeof(machineCode));

// 4. 激活许可证（使用管理员提供的注册码）
int ret = License_Activate(ctx, "BASE64_REG_CODE_HERE");

// 5. 检查许可证状态
if (License_Check(ctx) == LICENSE_OK) {
    // 许可证有效，执行业务逻辑
}

// 6. 销毁上下文
License_Destroy(ctx);
```

### 防篡改守卫调用

```c
// 在关键操作（保存/导出/渲染）前调用对应的 Guard 检查
if (License_GuardCheck(LICENSE_GUARD_SAVE) == LICENSE_OK) {
    // 执行保存操作
}
```

## 设计框架

### 不透明结构体指针 (Pimpl 模式)

`LicenseContext` 为前向声明的不透明结构体，客户端仅通过指针句柄操作，内部实现细节对调用方完全隐藏，保证 ABI 稳定性。

### 双导出宏

| 宏 | 用途 |
|---|---|
| `LICENSE_API` | 控制符号的导入/导出（Windows `__declspec(dllexport/dllimport)`、GCC/Clang `__attribute__((visibility))`） |
| `LICENSE_C_API` | 确保 C 链接（`extern "C"`），实现跨编译器 / 跨 IDE 的 ABI 兼容 |

### 防篡改机制

- **LicenseGuard** —— 将单一 `bool` 验证状态散射为多个 `std::atomic<uint32_t>` 变量（tokenA_hi/lo、tokenB_hi/lo、crossSum、timestamp），不同调用场景（Startup/Save/Export/Render/Generic）使用不同的变量组合校验，patch 全部变量并保持数学关系的难度远高于 patch 单个 bool
- **StrEncrypt** —— 编译时 XOR 加密，字符串常量存储于独立的 `secure` 段（MSVC `#pragma section("secure")`），运行时解密使用，避免敏感信息（如公钥路径、错误提示）被直接扫描

## 依赖库

| 依赖 | 用途 |
|---|---|
| OpenSSL (SSL + Crypto) | RSA 密钥生成、注册码签名与验证 |
| Windows: `Iphlpapi.lib` | 网络接口信息采集（用于机器指纹生成） |
| macOS: `IOKit.framework` + `CoreFoundation.framework` | 硬件信息采集（用于机器指纹生成） |

### 依赖库安装

```bash
vcpkg install openssl
```

## 构建配置

### CMake 集成

```cmake
# 查找 OpenSSL
find_package(OpenSSL REQUIRED)

# 构建 License 库
add_subdirectory(License)

# 链接到目标
target_link_libraries(YourTarget PRIVATE License)
```

### 构建选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `BUILD_LICENSE_TESTS` | OFF | 启用单元测试（自动定义 `LICENSE_TEST_HOOKS` 宏） |
| `SANYI_LICENSE_SYMBOLS` | ON | 生成调试符号（PDB/DWARF） |

### 版本信息

当前版本：**1.0.0**

```c
#define LICENSE_VERSION_MAJOR 1
#define LICENSE_VERSION_MINOR 0
#define LICENSE_VERSION_PATCH 0
#define LICENSE_VERSION ((1 << 16) | (0 << 8) | 0)  // 0x00010000
```

## API 概要

### 公共函数（18 个）

| 函数 | 说明 |
|---|---|
| `License_GetVersion` | 获取版本号（uint32_t） |
| `License_GetVersionString` | 获取版本字符串（只读） |
| `License_ConfigInit` | 初始化配置结构体 |
| `License_IsCheckEnabled` | 查询是否启用许可证检查 |
| `License_SetCheckEnabled` | 启用/禁用许可证检查 |
| `License_Create` | 创建许可证上下文 |
| `License_Destroy` | 销毁许可证上下文 |
| `License_Check` | 校验当前许可证状态 |
| `License_Activate` | 使用注册码激活许可证 |
| `License_ReValidate` | 强制重新校验许可证 |
| `License_Clear` | 清除当前许可证 |
| `License_GetMachineCode` | 获取机器码 |
| `License_GetInfo` | 获取许可证详细信息 |
| `License_GuardMarkValid` | 标记守卫为有效（内部使用） |
| `License_GuardMarkInvalid` | 标记守卫为无效（内部使用） |
| `License_GuardRefresh` | 刷新守卫状态 |
| `License_GuardCheck` | 场景化防篡改校验 |
| `License_GuardIsQuickValid` | 快速内存状态检查 |
| `License_GetLastErrorMessage` | 获取最近一次错误信息 |

### 密钥生成接口（2 个，仅供内部）

| 函数 | 说明 |
|---|---|
| `LicenseKeygen_GenerateKeyPair` | 生成 RSA 公钥/私钥对 |
| `LicenseKeygen_GenerateRegCode` | 根据机器码 + 有效期 + 功能 + 客户名生成签名注册码 |

### 测试钩子（2 个，仅 `BUILD_LICENSE_TESTS` 时可用）

| 函数 | 说明 |
|---|---|
| `LicenseTest_SetPublicKeyPem` | 注入临时 RSA 公钥（用于单元测试） |
| `LicenseTest_ClearPublicKeyPem` | 清除临时公钥覆盖 |

### 错误码枚举

```c
typedef enum LicenseResult {
    LICENSE_OK = 0,                    // 操作成功
    LICENSE_ERR_INVALID_ARG = -1,      // 参数无效
    LICENSE_ERR_NULL_POINTER = -2,     // 空指针
    LICENSE_ERR_NOT_INITIALIZED = -3,  // 未初始化
    LICENSE_ERR_BUFFER_TOO_SMALL = -4, // 缓冲区过小
    LICENSE_ERR_IO = -5,               // IO 错误
    LICENSE_ERR_VERIFY_FAILED = -6,   // 签名验证失败
    LICENSE_ERR_EXPIRED = -7,          // 许可证已过期
    LICENSE_ERR_VERSION_MISMATCH = -8, // 版本不匹配
    LICENSE_ERR_OUT_OF_RANGE = -9,     // 数值越界
    LICENSE_ERR_INTERNAL = -99         // 内部未知错误
} LicenseResult;
```

## 防篡改特性详解

### LicenseGuard 原子验证

传统的 `bool isValid` 可被逆向工程师直接内存 patch 绕过。LicenseGuard 采用**散射状态校验**策略：

- 状态分散在 4 组 `std::atomic<uint32_t>` 变量中（tokenA_hi/lo、tokenB_hi/lo、crossSum、timestamp）
- 每组变量使用不同的 `Scramble` 算法混淆
- `crossSum` 作为交叉校验和，确保各组之间的数学关系
- `timestamp` 防止重放攻击
- 不同业务场景（启动/保存/导出/渲染）使用不同 Flavor，触发不同的校验路径

### StrEncrypt 编译时加密

敏感字符串（如公钥 PEM、内部错误信息）通过宏 `DEFINE_ENCRYPTED_STR` 在编译时进行 XOR 加密：

- 密钥为数组首字节，后续字节依次与 `key + index` 进行 XOR
- 加密数据存放于 MSVC `secure` 段，独立于普通只读段
- 运行时首次调用解密，结果缓存为 `std::string`

## 目录结构

```
License/
├── License/
│   ├── Include/
│   │   ├── LicenseAPI.h              # 导出宏定义
│   │   └── License/
│   │       ├── LicenseDLL.h          # 主 API 头文件
│   │       ├── LicenseKeygen.h       # 密钥生成 API（内部）
│   │       └── LicenseTestHooks.h    # 测试钩子 API
│   ├── Src/
│   │   ├── LicenseDLL.cpp            # 主 API 实现
│   │   ├── LicenseManager.h/.cpp    # 许可证管理器
│   │   ├── LicenseGuard.h/.cpp      # 防篡改守卫
│   │   ├── LicenseInternal.h/.cpp   # 内部工具函数
│   │   ├── MachineFingerprint.h/.cpp # 机器指纹生成
│   │   ├── StrEncrypt.h             # 编译时 XOR 加密
│   │   ├── KeygenTool.h/.cpp        # 密钥生成工具（内部）
│   │   ├── LicenseKeygenDLL.cpp      # 密钥导出实现
│   │   └── LicenseTestHooks.cpp     # 测试钩子实现
│   └── CMakeLists.txt
└── Test/
    ├── LicenseTests.cpp              # 单元测试
    └── CMakeLists.txt
```