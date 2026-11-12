# Git Hooks

本目录存放项目级 Git 钩子，用于在提交前自动检查代码规范。

## 已启用钩子

### pre-commit

**作用：** 防止修改第三方库（`ThirdParty` 子模块）的源码。

`ThirdParty` 中的所有文件（libdxfrw、poppler、ghostscript、QrCodeGen、SVGnest 等）均来自上游第三方库，禁止直接修改。
如需调整第三方库的行为，请在我方业务代码（`Engine`、`UI`、`FileIO` 等模块）中处理。

**拦截条件：** `ThirdParty` 子模块工作区存在未提交的修改（`git -C ThirdParty status --porcelain` 非空）。

### 子模块指针校验（pre-commit 内调用）

**作用：** 暂存的子模块 gitlink 必须指向该子模块远端真实存在的提交，否则干净克隆执行 `git submodule update --init` 会失败。

实现位于 `Tools/submodule-pointers.sh`，pre-commit 会以 `check` 模式调用：

- 逐个读取暂存的 gitlink，按其路径在 `.gitmodules` 中反查 URL；
- 用 `git -C <子模块路径> fetch <url> <sha>` + `cat-file -e` 确认该提交在对应子模块远端可达；
- 任一不可达即拒绝提交。

更新指针时建议直接用守卫脚本，避免手工 `git add` 写错：

```bash
Tools/submodule-pointers.sh stage Python PythonHost Renderx
```

`stage` 模式显式用 `git -C <子模块路径> rev-parse HEAD` 取 HEAD，先校验全部目标可达，再统一写入索引；任一不可达则不写入任何指针。

### 代码 CJK 校验（pre-commit 内调用）

**作用：** 落实“注释用中文，代码和日志用英文”。判定标准是**去掉注释后**不含 CJK，注释里的中文允许存在。

实现位于 `Tools/check-cjk-in-code.py`，pre-commit 会以 `--staged` 模式调用：

- 先剥离注释（`//`、`/* */`、C++ 原始字符串、Python 三引号、CMake `#[[ ]]`、PowerShell `<# #>`），字符串里的 `//` 不会被当作注释；
- 再扫描剩余文本中的 Han / Hiragana / Katakana / Hangul 字符；
- 命中即输出 `文件:行号: 内容` 并以退出码 1 拒绝提交。

手工检查：

```bash
python3 Tools/check-cjk-in-code.py --staged          # 只查暂存文件
python3 Tools/check-cjk-in-code.py --all             # 查本仓库全部跟踪的代码文件
python3 Tools/check-cjk-in-code.py Engine UI CAM     # 指定目录或文件（子模块需显式传入）
```

排除范围：`build/` 等构建目录、`ThirdParty`、`Docs`、`node_modules`、`CMakeFiles`、生成文件（`moc_*`、`ui_*`、`qrc_*`、`*.pb.*`）。子模块是独立仓库，默认不进入 `--all`，需要时按路径显式检查。

按项目决定保持现状、不参与检查也不报告的路径（脚本内 `EXCLUDE_PATH_PREFIXES` / `EXCLUDE_PATH_EXACT`）：

- `Tools/` 整个目录
- `gitManager.py`

翻译数据脚本白名单（`DATA_SCRIPT_WHITELIST`，中文就是数据本身）：`UI/Common/Translations/` 下的 `translation_data_new.py`、`update_zh.py`、`zh_bulk.py`、`translation_data_props.py`，以及 `Python/scripts/update_zh_ts.py`。

显式例外登记在脚本的 `EXCEPTIONS` 中，格式为 `(相对路径, 行内必须包含的内容)`，内容为 `None` 表示整文件豁免；只登记真实字体族名、语言自称、材料别名、中文输入/渲染样张、示例程序路径、随系统语言变化的菜单标题等数据性中文，新增例外必须写明原因：

| 文件 | 保留内容 |
|---|---|
| `Engine/Common/Src/Text/FontManager.cpp` | 宋体、新宋体、黑体、微软雅黑、楷体、仿宋（真实字体族名） |
| `UI/Common/Include/UI/LanguageManager.h` | 中文、日本語、한국어（语言自称） |
| `Engraving/Engraving/Src/LaserStrategy.cpp` | 木材、金属、亚克力、皮革、玻璃（材料别名） |
| `UI/2D/Src/UI/Dlg/TextInputDlg.cpp`、`QRCodeInputDlg.cpp` | 整文件（中文输入样张） |
| `Utility/Example/main.cpp`、`GeoModelCore/Example/main.cpp` | 整文件（示例程序） |
| `Main/Src/Platform/MacMenuCleanup.mm` | `@"编辑"`、`@"編集"`（macOS 系统菜单标题匹配） |
| `UI/Common/Src/Dlg/FontSettingsTab.cpp` | 字体渲染预览样张 |
| `Renderx/CMake/EmbedShaders.cmake` | 写入生成文件的中文注释模板 |

> 钩子环境说明：该脚本只在父仓库内运行，必须沿用钩子传入的 `GIT_INDEX_FILE` 等变量；只有进入子模块仓库执行 git 命令时才像指针校验那样用 `env -u` 清除 `GIT_*`。

**跳过方式（仅在你清楚自己在做什么时使用）：**
```bash
git commit --no-verify
```

## 安装方法

在仓库根目录执行：

```powershell
.\Tools\Install-GitHooks.ps1
```

该脚本会将 git 的 `core.hooksPath` 指向 `Tools/git-hooks`，使本目录下的钩子对所有开发者生效。

> 注意：`core.hooksPath` 是本地 git 配置，每个开发者都需要执行一次安装脚本。
