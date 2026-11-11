# libdxfrw 本地修改溯源记录

本目录用于记录 `ThirdParty/libdxfrw` 相对上游的修改，使「已修改的 GPL 副本」这一事实
可以被外部核对。GPLv2 第 2 条 a 款要求分发修改版本时标明修改内容与日期，本目录即为该记录。

## 1. 副本标识

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/LibreCAD/libdxfrw.git |
| 上游基准（origin/master） | `d68fb1ed937ad2a74a54f6208277ec10a1b031b0` |
| 本地 HEAD | `1ac1cd658dd6bcc1b1d16031419aa0fcf1572ad3` |
| 本地分支 | `master`（领先上游 5 个提交） |
| 版本号 | `DXFRW 2.0.0`（见 `CMakeLists.txt` 的 `project()`） |
| 上游许可证 | **GPL-2.0-or-later**（`COPYING` 为 GPLv2 正文，`src/libdxfrw.h` 头部声明 version 2 or later） |
| 记录生成时间 | 2026-11-11 |

## 2. 本地提交清单（领先 origin/master 的 5 个提交）

| # | 提交 | 日期 | 标题 | 文件数 | 增/删行 |
|---|---|---|---|---|---|
| 1 | `c5437859744bccd8d0312a61b3bfe215b56c3c5c` | 2026-08-22 | Windows 菜单修改后编译运行 | 20 | +9921 / -5522 |
| 2 | `bb94e406eba091408d701c933180dda4198a20c3` | 2026-09-18 | win render ok | 5 | +177 / -17 |
| 3 | `33b92e9d0651a4280dedea80dcade3cd24c8ef26` | 2026-10-13 | Win 编译通过 | 145 | +159305 / -30792 |
| 4 | `1062e7e51c2245d7fb1e8ed8c681d2b4e06e1943` | 2026-10-21 | VS格式化 | 70 | +74794 / -57871 |
| 5 | `1ac1cd658dd6bcc1b1d16031419aa0fcf1572ad3` | 2026-10-21 | Merge branch 'master' of https://github.com/LibreCAD/libdxfrw | 1 | +8 / -0 |

说明：

- 第 1、3、4 个提交的主要目的是让 libdxfrw 在 MSVC 下编译通过并统一代码格式，改动集中在 `src/*.cpp`、`src/*.h` 与 `src/intern/*`。
- 第 3 个提交同时删除了上游的 `tests/`、`tools/`、`metadata/`、`.github/workflows/` 等只用于上游自测的目录，因此删除行数很大。
- 第 5 个提交是合并上游 `master` 到本地分支的合并提交。

## 3. 未提交改动

| 文件 | 状态 | 说明 |
|---|---|---|
| `README.md` | 已修改 | 删除上游 "Upstream and contributing" 段落共 8 行，完整 diff 见 `uncommitted-changes.diff` |
| `src/drw_interface.h` | 已修改 | `git status` 标记为已修改，但 `git diff` 无内容差异（行尾/文件属性差异），**无实际代码改动** |

## 4. 文件级改动清单

`changed-files-vs-upstream.txt` 为 `git diff --name-status origin/master..HEAD` 的完整输出，共 264 条，
覆盖 `src/`、`tests/`、`tools/`、`metadata/` 等全部增删改文件。

## 5. 复现与导出完整补丁

在仓库根目录执行：

```bash
cd ThirdParty/libdxfrw
git log --format='%H|%ad|%an|%s' --date=iso origin/master..HEAD   # 提交清单
git diff --name-status origin/master..HEAD                        # 文件级清单
git diff origin/master..HEAD > libdxfrw-local-changes.patch        # 完整补丁
git diff > libdxfrw-uncommitted.patch                              # 未提交改动
```

完整补丁约 24 万行（主要来自上游 `tests/`、`metadata/` 目录的删除），不适合作为文件入库，
因此本目录采用「提交标识 + 文件级清单 + 复现命令 + 未提交 diff」的记录形态；
需要完整补丁时按上述命令即时导出即可得到与本地 HEAD 完全一致的结果。

## 6. 分发义务提示

当前工程把 libdxfrw 源码直接编译进 `FileIO.dll`，与闭源主程序处于同一进程，属于最强的结合形态。
对外分发前必须由法务确认合规路径，并按 GPLv2 随包提供许可证全文与源代码获取方式，
具体见仓库根目录的 `THIRD-PARTY-NOTICES.md`。
