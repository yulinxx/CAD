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
