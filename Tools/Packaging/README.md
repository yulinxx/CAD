# 三平台打包脚本（Tools/Packaging）

把 `April` 的 Release 产物、Qt 运行时和工程动态库封装成可分发包。

| 平台 | 脚本 | 默认产物 |
| --- | --- | --- |
| macOS | `package-macos.sh` | `.app` + `.dmg`（可选 `.zip`） |
| Linux | `package-linux.sh` | `.AppImage`（缺工具时回退 `.tar.gz`，也可强制指定） |
| Windows | `package-windows.ps1` | 便携目录 `.zip`（可选 NSIS `-setup.exe`） |

公共部分：

- `package.conf`：三平台共享的应用名、可执行名、默认版本、Bundle ID，bash 与 PowerShell 都读取这一份，避免多处硬编码漂移。
- `common.sh`：macOS / Linux 共用的日志、构建目录探测、Qt 定位、版本读取、安全清理、归档函数。
- Windows 使用独立 PowerShell 实现，不依赖 bash，避免在未装 Git Bash 的机器上跑不起来。

脚本直接使用 `build/bin_Qt*/Release` 的构建产物，而不是 `cmake --install`：顶层 CMake 虽有 `install()` 规则与 `CMake/Packaging.cmake` 的 CPack 配置，但未覆盖 Qt 插件部署、macOS Bundle 修补、Linux AppImage 等平台封装细节。

所有脚本遵循：

- 失败即停（bash `set -euo pipefail`，PowerShell `$ErrorActionPreference = "Stop"`）。
- 可重复执行：只清理 `--out-dir` 内部的 staging 目录，绝不删除构建目录、源码或用户数据。
- 面向用户的输出与错误信息为英文，注释为中文。
- 不修改 CMake / 源码 / 子模块；产物不参与提交。

---

## 通用参数

bash 脚本（macOS / Linux）：

| 参数 | 说明 |
| --- | --- |
| `-b, --build-dir <path>` | CMake 构建目录；缺省自动探测 `build/` 或 `build-*` |
| `-o, --out-dir <path>` | 输出目录；缺省 `<仓库>/dist` |
| `--version <ver>` | 包版本；缺省读取顶层 `CMakeLists.txt` 的 `project(... VERSION)` |
| `--qt-dir <path>` | Qt 安装前缀；缺省依次读取 `QT_DIR` / `QTDIR` / `Qt6_DIR`、CMakeCache 的 `Qt_INSTALL_DIR`、`qmake -query`、Homebrew |
| `--skip-build` | 跳过构建，直接复用现有产物 |
| `-j, --jobs <n>` | 并行构建任务数 |
| `-h, --help` | 帮助 |

共享环境变量：`QT_DIR`、`QTDIR`、`NO_COLOR`（关闭彩色输出）。

签名与公证的凭据、CI Secret 注入方式与验证命令见
[代码签名与公证](../../Docs/07-工程与合规/代码签名与公证.md)。凭据缺失时脚本只告警跳过，
不会让打包失败。

Windows 参数：`-BuildDir`、`-OutDir`、`-Version`、`-Triplet`、`-Arch`、`-QtDir`、`-SkipBuild`、`-Configuration`、`-Jobs`、`-SignThumbprint`、`-MakeInstaller`，详见 `Get-Help .\package-windows.ps1 -Full`。

---

## macOS

### 前置依赖

- Xcode Command Line Tools + Metal 工具链（项目渲染需要）。
- CMake 4.3+、Ninja；官方 Qt 6 SDK（自带 `macdeployqt`）。
- 已初始化的子模块与可用的 vcpkg 依赖。

### 用法

```bash
# 构建 + 打包（默认 dmg）
./Tools/Packaging/package-macos.sh

# 复用现有产物，同时产出 dmg 和 zip
./Tools/Packaging/package-macos.sh --skip-build --format both

# Developer ID 签名（启用 hardened runtime 与安全时间戳）
./Tools/Packaging/package-macos.sh \
  --sign "Developer ID Application: Example Corp (TEAMID)"

# 身份也可以来自环境变量；公证凭据同样只读环境变量
export APPLE_SIGN_IDENTITY="Developer ID Application: Example Corp (TEAMID)"
export NOTARYTOOL_PROFILE="AprilNotary"
./Tools/Packaging/package-macos.sh --format dmg
```

有 Developer ID 与公证凭据时，`sign-macos.sh` 会完成嵌套组件签名、`notarytool` 公证、
staple 与 `spctl` 校验；缺少任一凭据时告警跳过对应步骤，构建仍成功。

流程：`cmake --build` → 复制 `.app` 到 staging → 收拢 `bin_Qt*/Release` 下的工程 dylib 到 `Contents/Frameworks` → `macdeployqt -libpath=...` 部署 Qt Framework/插件 → 清理构建机专属 `LC_RPATH` → 签名 → `codesign`/`otool` 校验 → 生成 dmg/zip。

签名说明：本工程的着色器、字体、翻译文件放在 `Contents/MacOS`（运行期按 `applicationDirPath` 读取），`codesign` 不带 `--deep` 时会把这些非 Mach-O 资源当成未签名子组件而拒绝签名，因此最终对 `.app` 使用 `--deep`。这是打包侧的必要变通，未修改源码布局。

### 本机实测记录（2026-11-06）

- `--skip-build --format both` 与完整（含构建）流程均跑通，退出码 0。
- 产物：`dist/April-1.0.0-macos-arm64.dmg`（约 117 MB）、`dist/April-1.0.0-macos-arm64.zip`（约 106 MB）。该次实测时应用名仍为 April，归档名随 `package.conf` 的 `APP_NAME` 变化。
- 校验：ad-hoc `codesign --verify` 通过；`otool -L` 确认已无 `build/`、Qt 安装目录等构建机绝对路径；可执行文件的 `@rpath` 依赖均能在 `Contents/Frameworks` 内解析。
- 未自动启动 GUI（避免写入用户配置/数据库），因此"能打包、签名、依赖闭合"已验证，"界面级启动"需人工双击 dmg 或 zip 内的 app 确认。
- 本机已知环境项：Metal 工具链缺失与 CAM/Python/Raster 子模块未初始化。本次构建目标为已启用的模块，未受影响；如后续启用这些特性，需先补齐环境后再打包。

---

## Linux

### 前置依赖

- CMake 4.3+、Ninja、GCC/Clang、patchelf。
- Qt 6 SDK；AppImage 方案额外需要 `linuxdeploy` 与 `linuxdeploy-plugin-qt`。

```bash
sudo apt install patchelf
# linuxdeploy / linuxdeploy-plugin-qt 从 GitHub Releases 下载后放入 PATH
```

### 用法

```bash
# 自动选择：有 linuxdeploy 出 AppImage，否则 patchelf 手工部署出 tar.gz
./Tools/Packaging/package-linux.sh

# 强制 tar.gz（只需 patchelf）
./Tools/Packaging/package-linux.sh --skip-build --format targz

# 强制 AppImage
./Tools/Packaging/package-linux.sh --format appimage --qt-dir /opt/Qt/6.11.1/gcc_64
```

两种后端：

1. **AppImage**：`linuxdeploy` 创建 AppDir，`linuxdeploy-plugin-qt` 部署 Qt 库与插件，`--output appimage` 生成单文件。
2. **tar.gz**：`ldd` 递归收集非系统共享库到 `lib/`，`patchelf` 写入 `$ORIGIN/lib` / `$ORIGIN` rpath，复制 Qt 插件并生成 `qt.conf`，最后打包整个 `April/` 目录。

`--arch` 可覆盖产物架构名；`--triplet` 仅用于日志（如 `x64-linux`）；`--icon` 可给 AppImage 提供 PNG 图标。

> **验证状态：未在本机验证。** 本机为 macOS，无法执行 Linux 构建与 AppImage 打包。脚本已通过 `bash -n` 语法检查；需要在 Linux CI 或 Linux 主机上确认：`--format both` 能产出 AppImage 与 tar.gz、解压后 `./April` 能启动、以及无头环境下 AppImage 不依赖 FUSE（或改挂载方式）。

---

## Windows

### 前置依赖

- Visual Studio 2022（C++ 桌面工作负载）与 Windows SDK（签名时需要 `signtool`）。
- CMake 4.3+；Qt 6 SDK（自带 `windeployqt`）。
- 可选：NSIS（`choco install nsis`）用于安装包；代码签名证书。

### 用法

```powershell
# 构建 + 便携 zip
.\Tools\Packaging\package-windows.ps1

# 复用现有产物
.\Tools\Packaging\package-windows.ps1 -SkipBuild

# 附带 NSIS 安装包
.\Tools\Packaging\package-windows.ps1 -MakeInstaller

# 签名（证书需已导入当前用户证书存储）
.\Tools\Packaging\package-windows.ps1 -SignThumbprint "ABCDEF..." -TimestampUrl "http://timestamp.digicert.com"

# 或从环境变量读取（CI 推荐，凭据不落仓库）
$env:APR_WIN_SIGN_THUMBPRINT = "ABCDEF..."
.\Tools\Packaging\package-windows.ps1 -MakeInstaller
```

Windows 也支持 `APR_WIN_SIGN_PFX` + `APR_WIN_SIGN_PASSWORD` 的 PFX 模式；缺少凭据时
脚本打印告警并跳过签名，打包继续。详见
[代码签名与公证](../../Docs/07-工程与合规/代码签名与公证.md)。

流程：`cmake --build` → 复制 exe、工程 DLL 与运行期目录（vcpkg 在本工程为静态链接，无额外第三方 DLL）→ `windeployqt --release` → 可选 `signtool` → `Compress-Archive` 生成 zip → 可选 `makensis` 生成安装包。

> **验证状态：未在本机验证。** 本机为 macOS，仅完成 PowerShell 语法解析检查，并验证了非 Windows 主机会给出明确错误 `This script must run on Windows.`。需在 Windows 主机/CI 上确认：VS 2022 构建、`windeployqt` 部署完整（平台/图像格式/样式插件齐全）、zip 解压后可启动、NSIS 安装/卸载正常、签名后 `signtool verify` 通过。

---

## 常见失败原因

| 现象 | 原因 / 处理 |
| --- | --- |
| `Qt installation not found` | 显式传 `--qt-dir` / `-QtDir`，或设置 `QT_DIR`；确保是 Qt 6 官方 SDK 而非仅系统 Qt 运行时 |
| `macdeployqt not found` | Homebrew 的 `qt` 可能不带部署工具；安装官方 Qt SDK，或指向其前缀 |
| `linuxdeploy-plugin-qt not found` | 只装了 `linuxdeploy`；补装插件，或改用 `--format targz` |
| AppImage 启动报 FUSE 相关错误 | 目标机无 FUSE；改用 tar.gz，或 `--appimage-extract-and-run` |
| `patchelf` 缺失 | `sudo apt install patchelf`，或改用 AppImage 方案 |
| `windeployqt.exe not found` | `-QtDir` 指向的前缀不是对应 MSVC kit（需 `msvc*_64`） |
| `makensis.exe not found` | 安装 NSIS，或去掉 `-MakeInstaller` |
| `signtool.exe not found` | 安装 Windows SDK；或去掉 `-SignThumbprint`（仅本地测试） |
| 构建失败且提示 Metal / 子模块 | macOS 的 Metal 工具链缺失、CAM/Python/Raster 子模块未初始化；补齐环境后再打包，不要用伪造产物绕过 |
| 签名校验提示 `Contents/MacOS/*.frag` 未签名 | 属工程资源布局所致；脚本已对 `.app` 使用 `--deep`，无需手工处理 |
| 重复执行后产物不一致 | 脚本会清理 `--out-dir` 内 staging 并覆盖同名归档；构建目录不受影响 |

## 输出与安全边界

- 默认输出到 `<仓库>/dist`（未纳入版本控制；请勿提交产物）。
- staging 目录：macOS `dist/macos/April.app`，Linux `dist/linux/April`、`dist/linux/AppDir`，Windows `dist/windows/April`。
- 脚本只允许删除输出目录内部的 staging 路径；参数写错指向源码/构建目录时会直接报错退出。
