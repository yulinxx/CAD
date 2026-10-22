# Tools 目录说明

本目录包含 SanYi CAD 白标交付与客户管理的自动化工具。

---

## 1. NewClient.ps1 / new-client.sh — 客户脚手架生成

从 `Clients/_template` 生成新客户仓库骨架。

### Windows (PowerShell)
```powershell
# 基础用法
.\Tools\NewClient.ps1 -ClientId acme -ProductName "AcmeCut"

# 完整参数
.\Tools\NewClient.ps1 `
  -ClientId acme `
  -ProductName "AcmeCut" `
  -CompanyName "Acme Technologies" `
  -OrganizationDomain "acme.example" `
  -Website "https://acme.example" `
  -SupportEmail "support@acme.example" `
  -ClientsRoot "C:\path\to\Clients" `
  -Force
```

### Linux/macOS (Bash)
```bash
# 基础用法
./Tools/new-client.sh -c acme -p "AcmeCut"

# 完整参数
./Tools/new-client.sh \
  -c acme \
  -p "AcmeCut" \
  --company "Acme Technologies" \
  --domain "acme.example" \
  --website "https://acme.example" \
  --email "support@acme.example" \
  -r /path/to/Clients \
  -f
```

### 生成内容
```
Clients/acme/
├── CMakeLists.txt              # sanyi_add_client_plugin(...)
├── configs/acme.json           # 菜单/面板/状态栏差异 (extends base)
├── branding/acme/brand.json    # 产品名/公司/图标/主题/版权
├── src/ClientPlugin.cpp        # 面板插件 (注册 __CLIENT_ID__.welcome 面板)
└── README.md
```

### 后续步骤
```bash
cd Clients/acme
git init
git add .
git commit -m "Initial client plugin"

# 在核心仓库注册为 submodule
git submodule add <acme-repo-url> Clients/acme
```

---

## 2. PackageClient.ps1 / package-client.sh — 交付包构建

将构建产物 + 客户配置/品牌/插件打包为可直接分发的目录/压缩包。

### Windows (PowerShell)
```powershell
# 自动检测 build/bin_Qt6/Release
.\Tools\PackageClient.ps1 -ClientId acme

# 指定构建目录、输出目录
.\Tools\PackageClient.ps1 `
  -ClientId acme `
  -BuildDir "build\bin_Qt6\Release" `
  -OutDir "dist" `
  -NoZip `
  -NoShortcut
```

### Linux/macOS (Bash)
```bash
# 自动检测 build/bin_qt6/Release
./Tools/package-client.sh -c acme

# 指定构建目录、输出目录
./Tools/package-client.sh \
  -c acme \
  -b build/bin_qt6/Release \
  -o dist \
  --no-zip \
  --no-shortcut
```

### 产出结构
```
dist/acme/
├── SanYiCAD.exe                    # 主程序
├── client.id                       # 部署真源
├── configs/
│   ├── base.json
│   └── acme.json
├── branding/acme/
│   ├── brand.json
│   ├── app.ico / logo.svg / splash.png ...
│   └── icons/
├── plugins/clients/acme/
│   └── SanYiClientAcme.dll         # 面板插件
├── <Qt runtime dlls...>
├── AcmeCut.lnk / AcmeCut.desktop / AcmeCut.app  # 平台快捷方式
└── acme-20261021.tar.gz / .zip     # 归档 (可选)
```

### 关键行为
- **配置校验**：`configs/acme.json` 与 `branding/acme/brand.json` 的 `clientId` 必须一致
- **基线回退**：自动拷贝 `Main/Src/UI/ClientConfig/configs/base.json`
- **插件拷贝**：从 `build/.../plugins/clients/<id>/` 拷贝插件 DLL/so
- **快捷方式**：
  - Windows：`.lnk` (图标取 `brand.appIcon` 或 exe 自带)
  - Linux：`.desktop` (含分类、注释、图标)
  - macOS：简易 `.app` bundle (`Info.plist` + 图标转 `.icns`)
- **归档**：`tar.gz` (Linux/macOS) / `.zip` (Windows)，文件名含日期戳

---

## 3. KeygenTool / PerfBenchmark / RenderBenchmark / crash_* — 内部工具

| 工具 | 用途 |
|------|------|
| `KeygenTool` | 离线生成 License Key / Hardware ID 绑定 |
| `PerfBenchmark` | 核心几何/拓扑算法微基准 |
| `RenderBenchmark` | 2D/3D 视口渲染吞吐基准 |
| `crash_*` | CrashHandler 采样/符号化/上传测试 |

> 以上工具非白标交付流程必需，按需构建 (`-DBUILD_KEYGEN_TOOL=ON` 等)。

---

## 依赖要求

| 平台 | 必需工具 |
|------|----------|
| Windows | PowerShell 5.1+, `jq` (可选，PackageClient 内部用 .NET JSON) |
| Linux | `bash`, `jq`, `patchelf` (AppImage 时), `desktop-file-utils` |
| macOS | `bash`, `jq`, `sips` (图标转 icns), `hdiutil` (DMG 可选) |

CI 环境已在 `.github/workflows/client-matrix.yml` 自动安装。

---

## 典型白标交付流程

```mermaid
flowchart LR
    A[新建客户] -->|NewClient.ps1| B[Clients/acme/]
    B --> C[编辑 configs/branding/]
    C --> D[提交并注册 submodule]
    D --> E[CI 矩阵构建]
    E --> F[PackageClient 自动打包]
    F --> G[dist/acme.zip 交付客户]
    G --> H[客户解压即运行]
```

---

## 常见问题

**Q: 客户配置改了，不想重新编译主程序？**
A: 只需替换部署包里的 `configs/<id>.json` / `branding/<id>/` / `plugins/clients/<id>/`，并更新 `client.id`，重启即可。

**Q: 如何给客户不同的启动图标？**
A: 在 `branding/<id>/brand.json` 设置 `appIcon: "app.ico"`，打包脚本会自动用于快捷方式/`.desktop`/`.app` 图标。

**Q: 插件 DLL 版本不匹配怎么排查？**
A: 插件目录下有 `abi.txt` (Qt 版本/编译器/架构)，启动时不匹配会拒绝加载并报错。

**Q: 如何跳过某客户的 CI 构建？**
A: 在 `client-matrix.yml` 手动触发时填 `clients` 字段，或在 `Clients/<id>/` 下建 `.ci-skip` 空文件（工作流会自动过滤）。