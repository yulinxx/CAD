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
│   ├── Src/
│   │   ├── Runtime/          # CADApplicationRuntime（启动 + 许可校验）
│   │   ├── Bootstrap/        # AppBootstrapper（组件初始化）
│   │   ├── UI/               # 主窗口、工作台、命令系统
│   │   │   └── Test/         # 测试目录（单元/集成/端到端测试）
│   │   ├── Common/           # AppPathManager、AppInitializer
│   │   ├── License/          # 许可证模块（离线注册码）
│   │   ├── Composition/      # 依赖注入、命令注册
│   │   └── RenderCore/       # UI 层渲染抽象（UiRenderCore）
│   └── CMakeLists.txt        # 主应用构建配置
├── Engine/                   # 图形引擎
│   ├── Common/               # 通用几何算法、数学工具
│   ├── 2D/                   # 2D 图元定义、布尔运算、CAM 路径生成
│   └── 3D/                   # 3D 场景管理、网格操作
├── Render/                   # 渲染层
│   ├── Common/               # 渲染公共类型、着色器管理
│   ├── 2D/                   # 2D 渲染实现（Qt Widgets + Shader）
│   ├── 3D/                   # 3D 渲染实现（OpenGL + Gizmo）
│   └── Core/                 # 底层渲染核心（GL46Backend、RenderBuffer）
├── Renderx/                  # 新一代渲染框架（C API，独立开发中）
├── UI/                       # UI 组件库
│   ├── Common/               # 通用 UI 组件、状态管理
│   ├── 2D/                   # 2D 工作台 UI、工具面板、属性面板
│   └── 3D/                   # 3D 工作台 UI、场景树、设置对话框
├── FileIO/                   # 文件导入导出（DXF、PLT、SVG、STL 等）
├── Nesting/                  # 排样 / 嵌套算法
├── Hardware/                 # 激光硬件控制、材质数据库
├── Engraving/                # 雕刻工艺（浮雕生成、刀具路径）
├── Network/                  # 网络同步（可选）
├── Vision/                   # 视觉模块（图像处理、轮廓识别，可选）
├── GeoModelCore/             # OpenCASCADE 几何建模（可选）
├── CrashHandler/             # 崩溃捕获（基于 Breakpad）
├── Log/                      # 日志系统
├── Utility/                  # 通用工具库（字符串、文件、加密）
├── PyBindCore/               # Python 绑定
├── PythonHost/               # Python 宿主（脚本扩展）
├── SanYiRender/              # 独立渲染 DLL（纯 POD 契约）
├── Tools/
│   ├── KeygenTool/           # 离线注册码生成工具
│   └── encrypt_string.py     # 字符串加密工具
├── ThirdParty/               # 第三方库
├── Doc/                      # 架构分析文档
├── CMakeLists.txt            # 根构建配置
├── Config.txt                # 用户配置（vcpkg/Qt 路径、编译开关）
└── README.md                 # 项目说明（本文档）
```

---

## 模块详细说明

### Core 层（基础基础设施）

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **Utility** | 通用工具库：字符串处理、文件操作、加密、数据结构 | C++17 | 无 |
| **CrashHandler** | 崩溃捕获与报告（基于 Breakpad） | Breakpad | Utility |
| **Log** | 结构化日志系统 | spdlog | Utility |

### Engine 层（图形引擎核心）

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **EngineCommon** | 通用几何算法、数学工具、图元定义 | C++17 + Boost.Geometry | Utility, Log |
| **Engine2D** | 2D 图元（线/圆/弧/多边形）、布尔运算、CAM 路径 | Clipper2, SQLiteCpp, FreeType | EngineCommon, Log |
| **Engine3D** | 3D 场景图、网格操作、布尔运算 | Boost | EngineCommon, Log |

### Render 层（渲染）

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **RenderCommon** | 渲染公共类型（顶点、着色器、纹理） | OpenGL | EngineCommon |
| **Render2D** | 2D 渲染器（基于 Qt Widgets） | Qt + Shader | RenderCommon, Engine2D |
| **Render3D** | 3D 渲染器（基于 OpenGL） | OpenGL | RenderCommon, Engine3D |
| **Render/Core** | 底层渲染核心（GPU 缓冲区、着色器程序） | OpenGL | RenderCommon |
| **UiRenderCore** | UI 层渲染抽象（场景编译、后端桥接、软件渲染） | Qt | Engine2D, Engine3D, UICommon |
| **SanYiRender** | 独立渲染 DLL（纯 POD 契约，无 Engine 依赖） | C API | 无 |

### UI 层（用户界面）

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **UICommon** | 通用 UI 组件、状态中心、命令系统 | Qt Widgets | EngineCommon |
| **UI2D** | 2D 工作台：主窗口、工具栏、属性面板、视口 | Qt Widgets | UICommon, Engine2D |
| **UI3D** | 3D 工作台：场景树、设置对话框、3D 视口 | Qt Widgets | UICommon, Engine3D, GeoModelCore |

### 功能模块

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **FileIO** | 文件导入导出（DXF、PLT、SVG、STL、NC） | C++17 | Engine2D, Engine3D |
| **Nesting** | 排样算法（矩形/不规则嵌套） | C++17 | Engine2D |
| **Hardware** | 激光硬件控制、材质数据库、工艺参数 | Qt SerialPort | UICommon |
| **Engraving** | 浮雕生成、刀具路径计算、雕刻预览 | OpenCASCADE | Engine3D, GeoModelCore |
| **GeoModelCore** | OpenCASCADE 几何建模集成 | OpenCASCADE | Engine3D |
| **Network** | HTTP 请求、WebSocket、云端同步（可选） | Qt Network | UICommon |
| **Vision** | 图像处理、轮廓识别、定位（可选） | OpenCV | Engine2D |

### Python 集成

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **PythonHost** | Python 脚本宿主（进程内执行） | CPython | Utility |
| **PyBindCore** | Python 绑定（C++ 接口暴露） | pybind11 | PythonHost, Engine2D |

### 应用入口

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **Main** | 应用入口：启动流程、许可证校验、窗口创建 | Qt Widgets | 所有模块 |

---

## 模块依赖关系

```
                              ┌─────────────────────────┐
                              │     Main (SanYiCAD)     │
                              │   应用入口 + 许可校验    │
                              └───────────┬─────────────┘
                                          │
          ┌───────────────────────────────┼───────────────────────────────┐
          │                               │                               │
          ▼                               ▼                               ▼
   ┌───────────────┐             ┌───────────────┐             ┌───────────────┐
   │    UI2D       │             │    UI3D       │             │   Hardware    │
   │  2D 工作台    │             │  3D 工作台    │             │  激光控制     │
   └───────┬───────┘             └───────┬───────┘             └───────┬───────┘
           │                             │                             │
           ▼                             ▼                             │
   ┌───────────────┐             ┌───────────────┐                     │
   │   UICommon    │◄────────────│  UiRenderCore │                     │
   │  通用 UI 组件 │             │ 渲染抽象层     │                     │
   └───────┬───────┘             └───────┬───────┘                     │
           │                             │                             │
           ▼                             ▼                             │
   ┌───────────────┐             ┌───────────────┐                     │
   │   Engine2D    │             │   Engine3D    │                     │
   │ 2D 几何引擎   │             │ 3D 场景引擎   │                     │
   └───────┬───────┘             └───────┬───────┘                     │
           │                             │                             │
           └───────────────┬─────────────┘                             │
                           ▼                                           │
                   ┌───────────────┐                                   │
                   │  EngineCommon │                                   │
                   │  通用几何工具 │                                   │
                   └───────┬───────┘                                   │
                           │                                           │
           ┌───────────────┼───────────────┐                           │
           │               │               │                           │
           ▼               ▼               ▼                           │
   ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │
   │    Render2D   │ │    Render3D   │ │   FileIO      │             │
   │ 2D 渲染器     │ │ 3D 渲染器     │ │ 文件导入导出   │             │
   └───────┬───────┘ └───────┬───────┘ └───────┬───────┘             │
           │                 │                 │                       │
           └────────┬────────┘                 │                       │
                    ▼                          │                       │
            ┌───────────────┐                  │                       │
            │  RenderCommon │◄─────────────────┘                       │
            │ 渲染公共类型   │                                          │
            └───────┬───────┘                                          │
                    │                                                  │
           ┌────────┴────────┐                                         │
           │                 │                                         │
           ▼                 ▼                                         │
   ┌───────────────┐ ┌───────────────┐                                 │
   │    Nesting    │ │   Engraving   │                                 │
   │  排样算法     │ │  雕刻工艺     │                                 │
   └───────┬───────┘ └───────┬───────┘                                 │
           │                 │                                         │
           ▼                 ▼                                         │
   ┌───────────────┐ ┌───────────────┐                                 │
   │ GeoModelCore  │ │  PythonHost   │                                 │
   │ OpenCASCADE   │ │ Python 宿主    │                                 │
   └───────┬───────┘ └───────┬───────┘                                 │
           │                 │                                         │
           └────────┬────────┘                                         │
                    ▼                                                  │
            ┌───────────────┐                                          │
            │      Log      │◄─────────────────────────────────────────┘
            │   日志系统    │
            └───────┬───────┘
                    │
                    ▼
            ┌───────────────┐
            │   CrashHandler│
            │   崩溃捕获     │
            └───────┬───────┘
                    │
                    ▼
            ┌───────────────┐
            │    Utility    │
            │   通用工具库   │
            └───────────────┘
```

---

## 构建

### 系统要求

| 平台 | 要求 |
|------|------|
| Windows | Windows 10+, MSVC 2022+, CMake 3.20+ |
| Linux | Ubuntu 20.04+, GCC 11+, CMake 3.20+ |
| macOS | macOS 12+, Clang 14+, CMake 3.20+ |

### 外部依赖

#### Qt 框架

| 版本 | 说明 |
|------|------|
| Qt 5.15 | LTS 版本，稳定可靠 |
| Qt 6.x | 最新版本，支持更好的 OpenGL/Vulkan |

**Qt 组件**：Core, Gui, Widgets, OpenGLWidgets

#### vcpkg 包

```bash
# 基础依赖
vcpkg install openssl:x64-windows
vcpkg install freetype:x64-windows
vcpkg install boost:x64-windows
vcpkg install clipper2:x64-windows
vcpkg install sqlitecpp:x64-windows

# 测试依赖（可选）
vcpkg install gtest:x64-windows

# Python 绑定（可选）
vcpkg install python3:x64-windows
vcpkg install pybind11:x64-windows
```

#### 可选依赖

| 库 | 说明 | 安装方式 |
|----|------|----------|
| OpenCASCADE | 几何建模引擎 | 源码编译或通过 vcpkg |
| OpenCV | 计算机视觉（Vision 模块） | vcpkg install opencv:x64-windows |

### 配置

1. 编辑 `Config.txt`，设置 `VCPKG_DIR` 和 `Qt_INSTALL_DIR`：

```cmake
# vcpkg 路径
set(VCPKG_DIR "C:/Users/xx/vcpkg/" CACHE PATH "VCPKG installation directory")

# Qt 版本（5 或 6）
set(QT_VERSION_MAJOR 6 CACHE STRING "Major version of Qt")

# Qt 安装路径
set(Qt_INSTALL_DIR "C:/Users/xx/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt installation directory")
```

2. 编译选项配置：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_HARDWARE` | ON | 激光硬件控制模块 |
| `BUILD_ENGRAVING` | ON | 雕刻工艺模块 |
| `BUILD_GEOMODELCORE` | ON | OpenCASCADE 几何建模 |
| `BUILD_VISION` | OFF | 视觉模块 |
| `BUILD_NETWORK` | OFF | 网络同步模块 |
| `BUILD_SANYI_RENDER` | ON | 独立渲染 DLL |
| `BUILD_KEYGEN_TOOL` | OFF | 注册码生成工具 |
| `BUILD_MAIN_TESTS` | OFF | 主应用集成测试 |
| `BUILD_ALL_TESTS` | OFF | 所有单元测试 |

### 编译

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake（默认 Release）
cmake ..

# 配置 CMake（带编译选项）
cmake .. -DBUILD_KEYGEN_TOOL=ON -DBUILD_ALL_TESTS=ON

# 编译
cmake --build . --config Release

# 编译特定目标
cmake --build . --target SanYiCAD
cmake --build . --target MainTests
cmake --build . --target KeygenTool
```

### 运行

```bash
# Windows
cd bin_Qt6/Release
SanYiCAD.exe

# Linux
cd bin_Qt6/Release
./SanYiCAD

# macOS
cd bin_Qt6/Release
open SanYiCAD.app
```

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

## 渲染后端切换

通过环境变量控制渲染后端：

```bash
# Windows
set SAN_YI_RENDER_BACKEND=opengl    # 强制 OpenGL
set SAN_YI_RENDER_BACKEND=software  # 强制软件渲染

# Linux/macOS
export SAN_YI_RENDER_BACKEND=opengl
export SAN_YI_RENDER_BACKEND=software
```

| 后端类型 | 说明 | 状态 |
|----------|------|------|
| OpenGL | 跨平台硬件加速 | 默认 |
| Vulkan | 新一代图形 API | 预留 |
| Metal | Apple 平台硬件加速 | 预留 |
| Software | 纯 CPU 软件渲染 | 可用（fallback） |

---

## 文档

架构分析与设计文档位于 `Doc/` 目录：

- [架构全方位分析与重新设计](Doc/Architecture_Analysis_And_Redesign.md)
- [SanYi CAD 完整分析报告](Doc/SanYi_CAD_Complete_Analysis_Report.md)
- [UI 交互框架设计文档](Doc/UI交互框架设计文档.md)
- [渲染框架分析](Doc/Rendering_Framework_Analysis.md)
- [架构设计文档](Docs/架构.md)
- [许可证模块文档](Main/Src/License/LicenseManager.md)

---

## 开发规范

### C++ 标准

- 使用 C++17 标准
- 启用 C++17 特性：结构化绑定、if constexpr、std::optional 等

### 编码风格

- 类名：PascalCase（如 `RenderCoreRenderer`）
- 方法名：camelCase（如 `compileIncremental`）
- 成员变量：m_ 前缀（如 `m_sceneCompiler`）
- 常量：全大写 + 下划线（如 `BACKEND_TYPE_OPENGL`）

### 符号导出

- 仅通过 `RENDER_CORE_API` 等宏导出必要符号
- 默认隐藏所有符号（`CMAKE_CXX_VISIBILITY_PRESET hidden`）

### 测试

- 单元测试：覆盖核心算法和工具类
- 集成测试：覆盖组件协作和生命周期
- 端到端测试：覆盖完整渲染管线和稳定性

---

## 项目状态

| 模块 | 状态 | 说明 |
|------|------|------|
| Core 层 | ✅ 稳定 | Utility/Log/CrashHandler 完善 |
| Engine 层 | ✅ 稳定 | 2D/3D 几何引擎功能完整 |
| Render 层 | ✅ 活跃开发 | 统一渲染抽象层建设中 |
| UI 层 | ✅ 稳定 | 2D/3D 工作台功能完整 |
| 功能模块 | ✅ 稳定 | FileIO/Nesting/Hardware/Engraving |
| Python | ⚠️ 开发中 | PythonHost 基础框架完成 |
| 渲染后端 | ⚠️ 开发中 | OpenGL 稳定，Vulkan/Metal 预留 |
