# SanYi CAD

工业级激光加工 CAD/CAM 软件，支持雕刻 / 切割 / 打标工艺。

- 2D/3D 图形编辑与渲染
- 百万级图元场景
- 跨平台（Windows / Linux / macOS）
- 渲染后端可切换（OpenGL → Vulkan / DX12）

---

## 目录结构

```
SanYiCAD/
├── Main/                     # 应用入口
│   └── Src/
│       ├── Runtime/          # CADApplicationRuntime（启动 + 许可校验）
│       ├── Bootstrap/        # AppBootstrapper（组件初始化）
│       ├── UI/               # 主窗口、工作台
│       ├── Common/           # AppPathManager、AppInitializer
│       └── License/          # 许可证模块（离线注册码）
├── Engine/                   # 图形引擎（2D / 3D / Common）
├── Render/                   # 渲染（2D / 3D / Core）
├── Renderx/                  # 新一代渲染（Render2D / Render3D / Common）
├── UI/                       # UI 组件库（2D / 3D / Common）
├── FileIO/                   # 文件导入导出
├── Nesting/                  # 排样 / 嵌套
├── Hardware/                 # 激光硬件控制
├── Engraving/                # 雕刻工艺
├── Network/                  # 网络同步（可选）
├── Vision/                   # 视觉模块（可选）
├── GeoModelCore/             # OpenCASCADE 几何建模
├── CrashHandler/             # 崩溃捕获（基于 Breakpad）
├── Log/                      # 日志系统
├── Utility/                  # 通用工具库
├── PyBindCore/               # Python 绑定
├── PythonHost/               # Python 宿主
├── SanYiRender/              # 独立渲染 DLL
├── Tools/
│   ├── KeygenTool/           # 离线注册码生成工具
│   └── encrypt_string.py     # 字符串加密工具
├── ThirdParty/               # 第三方库
├── Doc/                      # 架构分析文档
├── CMakeLists.txt            # 根构建配置
└── Config.txt                # 用户配置（vcpkg/Qt 路径、编译开关）
```

---

## 构建

### 依赖

| 依赖 | 版本 | 说明 |
|------|------|------|
| Qt | 5.15 / 6.x | UI 框架 |
| vcpkg | 最新 | 包管理器 |
| MSVC | 2022+ | 编译器 |
| OpenSSL | 3.x | RSA 加密（通过 vcpkg 安装） |
| OpenCASCADE | 7.x | 几何建模（可选） |

### 配置

1. 编辑 `Config.txt`，设置 `VCPKG_DIR` 和 `Qt_INSTALL_DIR`
2. 确保 vcpkg 中已安装 OpenSSL：
   ```bash
   vcpkg install openssl:x64-windows
   ```

### 编译

```bash
cd build
cmake .. -DBUILD_KEYGEN_TOOL=ON    # 可选：编译注册码生成工具
cmake --build . --config Release
```

### 可选模块编译开关（在 Config.txt 中设置）

| 开关 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_HARDWARE` | ON | 激光硬件控制模块 |
| `BUILD_ENGRAVING` | ON | 雕刻工艺模块 |
| `BUILD_GEOMODELCORE` | ON | OpenCASCADE 几何建模 |
| `BUILD_VISION` | OFF | 视觉模块 |
| `BUILD_NETWORK` | OFF | 网络同步模块 |
| `BUILD_SANYI_RENDER` | ON | 独立渲染 DLL |
| `BUILD_KEYGEN_TOOL` | OFF | 注册码生成工具 |

---

## LicenseManager 许可证模块

基于 RSA-2048 非对称加密 + 机器指纹绑定的离线注册码系统。

详细文档：[`Main/Src/License/LicenseManager.md`](Main/Src/License/LicenseManager.md)

### 架构概览

```
┌──────────────────────────────────────────────────┐
│  Layer 6: [可选] 在线验证 IOnlineVerifier        │
├──────────────────────────────────────────────────┤
│  Layer 5: 运行时二次校验 LicenseGuard::Check()     │
│  保存/导出/渲染时散射验证                           │
├──────────────────────────────────────────────────┤
│  Layer 4: 散射状态（无单 bool）                    │
│  6 个关联变量 + 交叉和 + 时间戳                    │
├──────────────────────────────────────────────────┤
│  Layer 3: 字符串加密 StrEncrypt + SECURE_SECTION  │
│  公钥/错误信息不在 .rdata 中以明文出现              │
├──────────────────────────────────────────────────┤
│  Layer 1: RSA-2048 非对称加密                     │
│  私钥签发的注册码，公钥无法伪造                     │
├──────────────────────────────────────────────────┤
│  Layer 0: IsLicenseCheckEnabled()                 │
│  开发阶段直接 return false 免除整个安全系统         │
└──────────────────────────────────────────────────┘
```

### 快速使用

**禁用许可校验（开发阶段）：**

```cpp
// LicenseManager.h:10
inline bool IsLicenseCheckEnabled() { return false; }  // 改 false 即跳过
```

**生成密钥与注册码：**

```bash
# 编译 KeygenTool
cmake .. -DBUILD_KEYGEN_TOOL=ON
cmake --build . --target KeygenTool

# 生成密钥对
KeygenTool genkey private.pem public.pem

# 用户激活时，用机器码生成注册码
KeygenTool genreg <machine_code> 2028-12-31 all 2026-07-04 "Customer" private.pem
```

**关键功能二次校验（保存/导出/渲染）：**

```cpp
#include "License/LicenseGuard.h"

void OnSave() {
    if (!LicenseGuard::Check(LicenseGuard::Flavor_Save))
        return;  // 校验失败，拒绝保存
    // ... 保存逻辑
}
```

### 安全特性

| 特性 | 说明 |
|------|------|
| 散射状态 | 6 个相互关联的 int32_t 变量，patch 单个无效 |
| 多 Flavor 校验 | Save/Export/Render 各走不同检查路径 |
| 字符串加密 | 公钥/错误信息在 `.secure` 节区以密文存储 |
| 时间戳校验 | 检测时间冻结/回拨攻击 |
| 在线验证接口 | 预留 `IOnlineVerifier`，支持后续联网双因子验证 |
| 零侵入开关 | `IsLicenseCheckEnabled()` 一个函数控制全局 |

---

## 模块依赖关系

```
Utility
  ↑
CrashHandler ← Log
  ↑
EngineCommon → Engine2D / Engine3D
  ↑
RenderCommon → Render2D / Render3D
  ↑
UICommon → UI2D / UI3D
  ↑
FileIO / Nesting / Hardware / Engraving
  ↑
Main (SanYiCAD.exe)
  └── LicenseManager ← LicenseGuard ← StrEncrypt
```

---

## 文档

架构分析与设计文档位于 `Doc/` 目录：

- [架构全方位分析与重新设计](Doc/Architecture_Analysis_And_Redesign.md)
- [SanYi CAD 完整分析报告](Doc/SanYi_CAD_Complete_Analysis_Report.md)
- [UI 交互框架设计文档](Doc/UI交互框架设计文档.md)
- [渲染框架分析](Doc/Rendering_Framework_Analysis.md)
- [许可证模块文档](Main/Src/License/LicenseManager.md)
