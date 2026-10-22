# Git Hooks

本目录存放项目级 Git 钩子，用于在提交前自动检查代码规范。

## 已启用钩子

### pre-commit

**作用：** 防止修改第三方库（`ThirdParty` 子模块）的源码。

`ThirdParty` 中的所有文件（libdxfrw、poppler、ghostscript、QrCodeGen、SVGnest 等）均来自上游第三方库，禁止直接修改。
如需调整第三方库的行为，请在我方业务代码（`Engine`、`UI`、`FileIO` 等模块）中处理。

**拦截条件：** `ThirdParty` 子模块工作区存在未提交的修改（`git -C ThirdParty status --porcelain` 非空）。

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
