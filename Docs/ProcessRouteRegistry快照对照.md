# ProcessRouteRegistry.cpp 快照对照记录

本文件是 `UI/2D/Src/ProcessRouteRegistry.cpp` 的**可核对锚点**，用于事后追溯某段逻辑的来源。
该源文件是他人创建的未跟踪文件，本批曾在不知情的情况下整文件覆盖；随后按下面的快照做了**功能超集重建**，不是“原样还原”。

## 1. 快照锚点

| 项目 | 值 |
|---|---|
| 收录 ref | `refs/heads/backup/2026-11-04-ui` |
| commit | `38051b6ca01f34ba40e2e2f75b5bd3f9ac15adf7` |
| blob | `5ff4121149ed2e80459ac7d403ec6c6ef9bc6687` |
| 路径 | `2D/Src/ProcessRouteRegistry.cpp`（相对 `UI/` 子仓根） |
| 原版规模 | 12651 字节 / 331 行 |
| 当前规模 | 30207 字节 / 636 行（2026-11-10 复测；功能超集重建后又经多次增量提交，不再是 21853 字节 / 552 行） |

未被收录的 ref（已核对为空）：`refs/heads/sync/ui-rebased-20261104`、`refs/remotes/origin/ui-sync-20261102`、`refs/stash`。

取回原版（任何人在任何机器上可复现）：

```powershell
git -C UI show refs/heads/backup/2026-11-04-ui:2D/Src/ProcessRouteRegistry.cpp > prr-before.cpp
```

## 2. 逐函数对照

存放位置：本文件第 2 节（`Docs/ProcessRouteRegistry快照对照.md`）。质疑某段逻辑时，先按第 1 节命令取回原版，再与本节逐行比对。

| 函数/成员 | 快照原版 | 重建版 | 行为是否等价 | 差异 |
|---|---|---|---|---|
| `BitmapProcessRoute` 构造/id/name/capabilities | 有 | 有 | 等价 | `Capabilities m_cap` 默认构造，两版相同 |
| `BitmapProcessRoute::match` | 有 | 有 | 等价 | 逐行一致：`requestEvenOdd` → `noteEvenOdd`；`requestColor` → `noteColor`；`thresholdMode==1` 且 `fixedThreshold` 越界拒绝 |
| `BitmapProcessRoute::execute` | 有（单图） | 有（多图） | 等价（超集） | 原版快照 `[int w][int h][data]`；重建版 v2 快照 `[ver][count]` + 每图 `[w][h][ch][res][quad8][gray]`。单图输出契约不变（`sizes` 2 个 int、1 条像素缓冲）；原版长度/尺寸/截断校验由 `parseSnapshot` 等价覆盖；`Raster_CreateImage + memcpy` 改为 `Raster_CreateImageFromView`（同为 GRAY8） |
| `BitmapProcessRoute` `BUILD_RASTER=OFF` 分支 | 有 | 有 | 等价 | 文案相同 |
| `VectorizeBitmapRoute::match` | 有（恒 true） | 有 | 超集 | 新增非有限 `kerfMm` 拒绝；不再拒绝 kerf |
| `VectorizeBitmapRoute::execute` | 有（`"not implemented yet (Phase 2)"`） | 有（完整链路） | 本批新增 | Vectorize → Kerf → ApplyPathParams → 轮廓收集/minArea/索引 |
| `RasterizeVectorRoute::match` | 有（仅 EvenOdd 拒绝） | 有 | 本批新增 | 增加 `noteUnavailable`，在能力协商阶段显式拒绝桩路由 |
| `RasterizeVectorRoute::execute` | 有（stub Error） | 有（stub Error） | 等价 | 按结论保留桩 |
| `IRoute::match` 默认实现 | 有 | 有 | 等价 | 相同 |
| `instance/registerRoute/find/registeredRouteIds/clear` | 有 | 有 | 等价 | `Q_UNUSED(s_initialized)` → `(void)s_initialized`（本文件未包含 Qt 头，行为一致） |
| capability 矩阵（两条矢量路由） | 有（聚合初始化） | 有（逐字段） | 非行为差异 | 原版 `Vectorize` 声明 `HoleTopology=false`（实际输出孔）；`Rasterize` 聚合位置错位声明 EvenOdd/GrayOnly=true。重建版按实际能力声明 |

结论：**原版有、重建版无的行为逻辑为 0 条**，因此没有“补回”动作；重建版独有的实现为表中“本批新增”。

## 3. 合并时保留的其它批改动

- `Request::kerfWidthMm` → `kerfMm`（及其注释）
- Kerf 桥接接线（`Adapter/KerfCompensatorBridge.h` 的引用）

## 4. 无法确认的部分

该文件从未进入任何提交，除上表 backup ref 外没有更早版本可比，无法确认 331 行之前是否还有更早的手改历史。
