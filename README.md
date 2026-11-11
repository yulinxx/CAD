# April CAD

工业级激光加工 CAD/CAM 软件，支持雕刻 / 切割 / 打标工艺。

- 2D/3D 图形编辑与渲染
- 百万级图元场景
- 跨平台（Windows / Linux / macOS）
- 渲染经独立 DLL（RenderX，C API + POD 契约）收口，当前后端为 OpenGL

---

## 目录结构

```
April/
├── Main/                     # 应用入口
│   ├── Src/
│   │   ├── Runtime/          # CADApplicationRuntime（启动 + 许可校验）
│   │   ├── Bootstrap/        # AppBootstrapper（组件初始化）
│   │   ├── UI/               # 主窗口、工作台、命令系统
│   │   │   ├── Render/       # 场景刷新协调、图元→顶点离散化
│   │   │   ├── Services/     # UiServices 服务聚合
│   │   │   └── Test/         # 测试目录（单元/集成/端到端测试）
│   │   ├── Common/           # AppInitializer（AppPathManager 已移至 Utility）
│   │   ├── License/          # 激活对话框（LicenseDialog，核心实现见 Licensing/License）
│   │   └── Composition/      # 依赖注入、命令注册
│   └── CMakeLists.txt        # 主应用构建配置
├── Engine/                   # 图形引擎
│   ├── Common/               # 通用几何算法、数学工具
│   ├── Persistence/          # 引擎持久化基类
│   ├── 2D/                   # 2D 图元定义、布尔运算、CAM 路径生成
│   └── 3D/                   # 3D 场景管理、网格操作
├── Renderx/                  # 渲染 DLL（RenderX.dll，纯 C API + POD 契约）
│   ├── include/render/       # 对外唯一公共头 renderx.h
│   ├── src/rhi/              # RHI 抽象：gl/、metal/（Apple 平台）、null/
│   ├── src/rt/               # Runtime / Session / 增量渲染 / 字形图集
│   └── src/shader/           # 内嵌着色器（构建期打包进 DLL）
├── UI/                       # UI 组件库
│   ├── Common/               # 通用 UI 组件、状态管理、命令内核
│   ├── 2D/                   # 2D 画布、绘制工具、工具栏（工作台窗口在 Main）
│   └── 3D/                   # 3D 工作台 UI、场景树、设置对话框
├── RenderAbstraction/        # 渲染抽象接口
├── RenderBridge/             # 渲染桥接（覆盖层、提交器）
├── UiEngineAccess/           # UI → 引擎链接层门面
├── FileIO/                   # 文件导入导出（DXF、SVG、PDF、AI、IGES、STEP、OBJ、STL 等）
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
├── Python/                   # Python 运行时脚本与模块
├── CAM/                      # CAM 路径生成与工艺
├── Raster/                   # 光栅化与位图导出
├── Licensing/                # 许可证与离线注册码
├── Clients/                  # 客户端配置模板
├── Tools/
│   ├── KeygenTool/           # 离线注册码生成工具
│   └── encrypt_string.py     # 字符串加密工具
├── ThirdParty/               # 第三方库
├── Testing/                  # 跨模块测试
├── CMake/                    # 公共 CMake 函数与工具链
├── Docs/                     # 架构与专题文档
├── CMakeLists.txt            # 根构建配置
├── Config.cmake                # 用户配置（vcpkg/Qt 路径、编译开关）
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
| **RenderX** | 唯一渲染 DLL（纯 C API + POD 契约，无 Engine / Qt 依赖） | C API + OpenGL / Metal / Null | 无 |

> 后端现状：`Renderx/src/rhi/` 下已实现 **OpenGL**、**Metal**（仅 Apple 编译）、
> **Null**（无头/单测）三套 RHI；`Config.cmake` 在 macOS 默认 `METAL`，其余平台默认 `OPENGL`。
> `renderx.h` 的 `Backend` 枚举里 `Vulkan`（Phase 8）仍是占位取值，
> `isBackendAvailable` 对它返回 false，`createDevice` 报错返回 nullptr；
> 在非 Apple 平台选 Metal 同样会明确失败。**不做静默回退**，请勿把枚举取值当作可用后端。
> Metal 的设计与落地记录见 `Docs/03-渲染主链/Metal后端实施设计.md`
> 与 `Metal后端Mac开发指南.md`。
>
> 已删除的旧模块：`RenderCommon` / `Render2D` / `Render3D` / `Render/Core` / `UiRenderCore`。
> 它们的职责已分别落到 RenderX（GPU 侧）与 `Main/Src/UI/Render`（宿主侧场景刷新与离散化）。

### UI 层（用户界面）

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **UICommon** | 通用 UI 组件、状态中心、命令系统 | Qt Widgets | UiEngineAccess（链接层门面） |
| **UI2D** | 2D UI 组件：工具栏、绘制工具、属性面板、视口画布（工作台窗口在 Main） | Qt Widgets | UICommon, UiEngineAccess |
| **UI3D** | 3D 工作台：场景树、设置对话框、3D 视口 | Qt Widgets | UICommon, UiEngineAccess, GeoModelCore |

### 功能模块

| 模块 | 职责 | 技术栈 | 依赖 |
|------|------|--------|------|
| **FileIO** | 文件导入导出（DXF、PLT、SVG、PDF、AI、IGES、STEP、OBJ、STL、apr/apr3d） | C++17 | Engine2D, Engine3D |
| **Nesting** | 排样算法（矩形/不规则嵌套） | C++17 | Engine2D |
| **Nesting v2 核心能力** | QUALITY 策略(GA+退火) / 24向旋转(15°) / 镜像翻转 / 共边排布(切割侧未实现) / 孔内套排(实现完整,UI 未设许可位) / GROUP嵌套展开 / 10种2D图元类型(BEZIER/BEZIER2 分列) / 局部坐标变换 / 进度回调与取消 | - | - |
| **CAM** | CAM 路径生成与工艺 | C++17 | Utility, Log |
| **Raster** | 光栅化与位图导出 | OpenCV | OpenCV |
| **Licensing** | 许可证校验与离线注册码 | OpenSSL | OpenSSL |
| **Hardware** | 激光硬件控制、材质数据库、工艺参数 | Qt6 Core | Log, Utility（均 PRIVATE） |
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
                              │     Main (April)     │
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
   │   UICommon    │             │ Render-       │                     │
   │  通用 UI 组件 │             │ Abstraction   │                     │
   └───────┬───────┘             └───────┬───────┘                     │
           │                             ▼                             │
           │                     ┌───────────────┐                     │
           │                     │  RenderBridge │                     │
           │                     └───────┬───────┘                     │
           │                             ▼                             │
           │                     ┌───────────────┐                     │
           │                     │    RenderX    │                     │
           │                     │ 渲染 DLL(C API)│                     │
           │                     └───────────────┘                     │
           ▼                                                           │
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
                           ▼                                           │
                   ┌───────────────┐                                   │
                   │   FileIO      │                                   │
                   │ 文件导入导出   │                                   │
                   └───────┬───────┘                                   │
                           │                                           │
           ┌───────────────┴─────────────┐                             │
           │                             │                             │
           ▼                             ▼                             │
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

> 图中箭头读作「A 依赖 B」。RenderX 是纯 C ABI 渲染 DLL，**不依赖任何
> UI / Engine / Qt 模块**；UI2D/UI3D 也不直接调用 RenderX，链路是
> `RenderAbstraction` → `RenderBridge` → `RenderX`（图中已展开这三层，不再折叠）。
> 「原先 RenderX → UICommon 的横向箭头」与代码不符，已删除；依赖方向由
> `CMake/CheckDependencies.cmake` 在 configure 阶段校验。
>
> UI 与 Engine 的关系分两层：**链接层**经 `UiEngineAccess` 门面（图中 UICommon →
> Engine2D/Engine3D 即为该门面下的链接），**include 层**未收口——场景/实体/
> 选择/编辑头仍由 UI 直接 include，由 `CMake/UiEngineAccessRatchet.cmake`
> 冻结存量、禁止增长。`Hardware` 只链 `Log` / `Utility`（PRIVATE）与 `Qt6::Core`，
> 不依赖任何 UI 模块。

---

## 构建

### 系统要求

| 平台 | 要求 |
|------|------|
| Windows | Windows 10+, MSVC 2022+, CMake 4.3+ |
| Linux | Ubuntu 20.04+, GCC 11+, CMake 4.3+ |
| macOS | macOS 12+, Clang 14+, CMake 4.3+ |

### 外部依赖

#### Qt 框架

| 版本 | 说明 |
|------|------|
| Qt 6.x | 当前唯一支持的 Qt 主版本 |

> 根 `CMakeLists.txt` 要求 **CMake 4.3+**；`Config.cmake` 只查找 **Qt6**
> （`Qt6Config.cmake`），不再支持 Qt 5。使用 Qt 5 的旧构建配置会在 configure
> 阶段直接失败。

**Qt 组件**：Core, Gui, Widgets, OpenGLWidgets

#### vcpkg 包

仓库根 `vcpkg.json` 使用 `builtin-baseline` 锁定端口版本，发布与 CI 构建应启用
manifest 模式（开发机可继续使用已有的经典模式安装）：

```bash
# 锁定版本安装（推荐；configure 阶段自动安装到 build/vcpkg_installed）
cmake -S . -B build -DVCPKG_MANIFEST_MODE=ON

# 或先显式安装再 configure
vcpkg install --triplet x64-windows --x-manifest-root .

# 需要 GeoModelCore / Engraving 时追加可选 feature
cmake -S . -B build -DVCPKG_MANIFEST_MODE=ON \
  -DVCPKG_MANIFEST_FEATURES=opencascade
```

Nesting / CAM / Raster 默认开启，因此核心依赖包含 `clipper2` 与 `opencv4`。
Python 绑定所需的 `pybind11` 通过 pip 安装，不使用 vcpkg 的 `pybind11` / `python3`
端口（原因见 `Config.cmake` 注释）：

```bash
python3.11 -m pip install "pybind11>=2.11"
```

**对已有构建目录的影响**：manifest 模式默认关闭，已有 `build/` 的
`CMakeCache.txt` 中 `VCPKG_MANIFEST_MODE=OFF` 会保持不变，升级后仍可正常
configure。显式切换为 ON 时，不需要删除构建目录，但首次 configure 会按
`vcpkg.json` 重新解析并安装依赖（默认落到 `build/vcpkg_installed`）；离线机器
需预先准备 vcpkg 二进制缓存。

#### 可选依赖

| 库 | 说明 | 安装方式 |
|----|------|----------|
| OpenCASCADE | 几何建模引擎 | `-DVCPKG_MANIFEST_FEATURES=opencascade` |
| OpenCV | 位图/视觉（Raster、Vision） | 已包含在核心依赖 `opencv4` |

### 配置

不再需要手工编辑 `Config.cmake` 填绝对路径。路径优先从环境变量读取，其次回退
到平台默认值；也可以直接通过 `-D` 覆盖：

```bash
export VCPKG_ROOT="$HOME/vcpkg"          # 或 -DVCPKG_DIR=...
export QT_ROOT_DIR="$HOME/Qt/6.12.0/macos" # 或 -DQt_INSTALL_DIR=...

# Qt 主版本（当前工程固定为 6；不支持 Qt 5）
# set(QT_VERSION_MAJOR 6 CACHE STRING "Major version of Qt")
```

编译选项配置：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_RENDERX` | ON | 3D 渲染引擎（Renderx） |
| `BUILD_UI2D` | ON | 2D 用户界面；关闭会直接触发 configure 失败 |
| `BUILD_UI_DIMENSION` | `BOTH` | UI 维度门控：`2D` / `3D` / `BOTH`，派生 `BUILD_UI3D` 与默认视口 |
| `BUILD_NESTING` | ON | 套料/排版算法 |
| `BUILD_VISION` | ON | 视觉模块 |
| `BUILD_CAM` | ON | CAM 工艺编译层（路径规划） |
| `BUILD_RASTER` | ON | 位图处理（OpenCV） |
| `BUILD_NETWORK` | ON | 网络同步模块 |
| `BUILD_HARDWARE` | ON | 激光硬件控制模块 |
| `BUILD_ENGRAVING` | ON | 雕刻工艺模块 |
| `BUILD_GEOMODELCORE` | ON | OpenCASCADE 几何建模 |
| `BUILD_CRASHHANDLER` | ON | 崩溃捕获与上报 |
| `BUILD_PYTHON` | ON | Python 集成框架 |
| `APR_UNITY_BUILD` | ON | Unity Build，加速编译 |
| `APR_ENABLE_LICENSE` | ON | 许可校验（默认已开启；运行期默认值由 License 库内部宏决定） |
| `APR_ENABLE_VULKAN` | ON | Vulkan 支持标记；后端尚未实现，选 Vulkan 会明确失败 |
| `BUILD_KEYGEN_TOOL` | OFF | 注册码生成工具 |
| `BUILD_PERF_BENCHMARK` | ON | 百万图元性能基准 |
| `BUILD_RENDER_BENCHMARK` | ON | 渲染帧路径基准 |
| `BUILD_TESTS` | ON | 全工程唯一测试开关；ON 按模块正常构建测试，OFF 不生成任何测试目标 |

全工程只有一个测试开关 `BUILD_TESTS`，不再有逐模块开关。独立构建入口（如 `cmake -S Vision -B ...`）只生成该入口声明归属的模块测试，兄弟模块零测试目标；GTest 缺失时自动降级为 OFF。以上默认值均以 `Config.cmake` 的 `option(...)` 为准。

### 编译

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake（默认 Release）
cmake ..

# 配置 CMake（带编译选项）
cmake .. -DBUILD_KEYGEN_TOOL=ON -DBUILD_TESTS=OFF

# 编译
cmake --build . --config Release

# 编译特定目标
cmake --build . --target April
cmake --build . --target MainTests
cmake --build . --target KeygenTool
```

### 运行

```bash
# Windows
cd bin_Qt6/Release
April.exe

# Linux
cd bin_Qt6/Release
./April

# macOS
cd bin_Qt6/Release
open April.app
```

---

## LicenseManager 许可证模块

基于 RSA-2048 非对称加密 + 机器指纹绑定的离线注册码系统。

详细文档：[`Licensing/License/README.md`](Licensing/License/README.md)

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

后端由宿主在创建 RenderX 设备时显式指定（`DeviceDesc::backend`），
没有环境变量开关，也没有静默回退——`Backend::Auto` 是唯一允许自动选择的取值。

| 后端类型 | 说明 | 状态 |
|----------|------|------|
| OpenGL | 跨平台硬件加速 | 已实现；非 Apple 平台默认 |
| Metal | Apple 平台硬件加速 | 已实现（仅 Apple 编译）；macOS 默认 |
| Null | 不产生任何 GPU 调用，用于单测与无头环境 | 已实现 |
| Vulkan | 新一代图形 API | 未实现（Phase 8，枚举占位） |

选择未实现（Vulkan）或当前平台不可用（非 Apple 上的 Metal）的后端时，
`rxRuntimeCreate` / `createDevice` 会通过 `logCallback` 报明原因并返回 nullptr，
不会退化成 Null 后端画出一片黑屏；`Backend::Auto` 只在已实现的后端里挑选。

---

## 文档

架构与专题文档位于 `Docs/` 目录，入口见 [文档总览](Docs/00-索引与总览/文档总览.md)：

- [架构文档地图](Docs/00-索引与总览/架构文档地图.md)
- [新渲染架构](Docs/03-渲染主链/新渲染架构.md)
- [渲染管线](Docs/03-渲染主链/渲染管线.md)
- [GPU 渲染边界](Docs/03-渲染主链/图形处理器渲染器边界.md)
- [3D 架构现状与约束](Docs/03-渲染主链/3D%20架构现状与约束.md)
- [许可证模块文档](Licensing/License/README.md)

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

- 仅通过 `RENDER_API` 等模块导出宏导出必要符号
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
| 渲染后端 | ✅ 稳定 | OpenGL / Metal(Apple) / Null 已实现；仅 Vulkan 为枚举占位，选中即报错 |
