# 待办事项

## SystemThemeDetector — 系统深色/浅色模式跟随

**状态：✅ 已完成**

新增 `AppTheme::System` 主题模式，支持自动跟随操作系统深色/浅色模式切换。

核心变更：

- 新增 `SystemThemeDetector` 类（`UI/Common/Include/UI/SystemThemeDetector.h` + `Src/SystemThemeDetector.cpp`），跨平台检测系统深色模式：
  - macOS：通过 `[NSApp effectiveAppearance]` 检测，`NSDistributedNotificationCenter` 监听变化
  - Windows：读取注册表 `AppsUseLightTheme`，`QAbstractNativeEventFilter` 监听 `WM_SETTINGCHANGE`
  - Linux：读取 GTK 主题名称（gsettings / settings.ini），`QTimer` 轮询检测变化
- `ThemeManager` 新增 `AppTheme::System` 枚举值，构造函数中连接 `SystemThemeDetector::systemThemeChanged` 信号
- `ThemeLabels.h` 新增 `"System (Follow OS)"` 翻译标签
- `ConfigManager` 遗留映射支持 `"system"` 字符串

硬编码样式修复：

- `LicenseDialog.cpp`：5 处硬编码颜色（`#f0f0f0`、`color: red`、`color: gray`）替换为 `TM->colors()` 语义色
- `UiWorkbench.cpp`：1 处硬编码背景替换为 `TM->colors()` 语义色
- `FillDialog.cpp`：`ColorButton::updateStyle()` 中禁用态硬编码颜色替换为 `TM->colors()` 语义色

后续维护提醒：

- `SystemThemeDetector` 使用 QTimer 轮询（Linux）/ 原生通知（macOS/Windows）检测变化，性能开销极低
- 新增主题时需同步更新 `ThemeManager` 的 `applyColorsForTheme`、`iconFlavorFor`、`loadStylesheet`、`applyPaletteForTheme`、`themeName`、`supportedThemes` 方法
- 硬编码颜色是主题适配的主要风险点，新增 UI 控件时应始终使用 `TM->colors()` 语义色

## SceneEnvGeometryDesc — 让 Renderx 直通消费 SceneEnvGeometry

**状态：✅ 已完成**

当前实现：

- `Renderx` 提供纯 POD 描述符 `SceneEnvGeometryDesc` / `EnvLayerDesc`（`Renderx/include/render/RenderTypes.h`），零 Engine 依赖。
- C API 提供 `renderSetSceneEnvDirect` 直通入口（`Renderx/src/c_api/`）。
- UI 侧 `RenderWidget::submitSceneEnvGeometry(const Eg::SceneEnvGeometry&)` 由 `Renderx` 内部完成逐顶点转换，不再拆散为平行数组。
- `UI/2D` 两处调用点（`submitSceneFromDataSource` / `submitDefaultSceneEnv`）已统一走直通提交。
- 标尺文字继续走 `renderSetScreenTexts` 通道，与几何解耦。

后续维护提醒：

- `UI/Common/Include/Render/RenderTypes.h` 为已标记 DEPRECATED 的旧渲染桥接类型，仍被 `RenderWidget` / `ViewRenderCoordinator` 使用，待桥接层完全迁移到 Renderx 后整文件移除。
- `RenderFrameUpdate` 中的 `hasBitmaps`/`bitmaps` 与 `RenderFrame::bitmaps`、`RenderBitmapQuad`、`UiTextItemList` 为未消费的死代码，`ui_texts` 走 `RenderOverlayUpdate`；清理时一并删除。

---

# 待办 Backlog（来自全代码审查，按优先级）

## P0（✅ 已修复）

1. **GeoModelCore 圆弧参数化双倍半径** — `GmcCurve::makeArcOfCircle` 曾对 OCCT 弧参数（弧度角）乘 `radius`，已改为直接传角度。`GmcCurve.cpp`。
2. **GmcLaw::sampleUniformLength 负数 reserve 崩溃** — `includeEndpoints=false` 且短曲线时 `endIdx-startIdx+1` 为负转 `size_t`，已钳制为 0。`GmcLaw.cpp`。并新增回归测试 `GmcLawTest.SampleUniformLengthShortCurveNoEndpoints`。
3. **GmcShapeHealing::repair 覆盖失败为成功** — 验证失败后仍无条件 `success()`，现改为失败即返回且不写无效形状。`GmcShapeHealing.cpp`。

## P1（建议尽快处理）

4. **macOS GL 4.6 GPU 剔除不可用**（✅ 已修复）— `culling.comp` 已从 `#version 460` 降为 `#version 430`（compute 最低版本，兼容 Win/Linux 4.6）；`createComputePipeline` 在不支持时给出平台提示。macOS 最高 GL 4.1 **无计算着色器**，后续接入 GPU 剔除时须先判 `Capabilities::computeShaders`，为 0 降级 CPU（`rxSessionQueryVisibility`/`DrawList::resolve`）。若未来需 460 专属特性，应提供 430/460 双变体按能力选择，勿再整体抬到 460。
5. **Network 令牌明文存储** — `ConfigManager.cpp:21,63-93` 用 `QSettings(IniFormat)` 明文存 access/refresh token，建议系统 keychain（macOS Keychain / Windows DPAPI）或加密后落盘。
6. **Hardware 密码学不达标** — `SafetyManager.cpp:245-246` 固定全局盐 `"sanyi_salt_v1"` + SHA-256（非 KDF）+ 非常量时间比较 + 默认凭据 `admin/admin123`。应改 bcrypt/scrypt/argon2、每用户独立盐、常量时间比较、强制首登改密并移除默认账号。
7. **CI 仅覆盖 Windows 且 /analyze 为软门** — `.github/workflows/ci.yml`：加 macOS/Linux runner（构建路径已就绪但从未在 CI 验证）；去掉 `static-analysis-windows` 的 `continue-on-error: true`。
8. **libdxfrw 在非 MSVC 上 `-Werror`**（✅ 已修复）— `ThirdParty/libdxfrw/CMakeLists.txt:15` 移除 `-Werror`（保留 `-Wall -Wextra -pedantic`），并注释说明如需严格告警应在 CI 里对该第三方固定版本基线单独开启，避免个别 GCC/Clang 警告掐断整个构建。
9. **GeoModelCore 网格为 double + flat-normal** — `GmcMeshData`（`GmcTypes.h`）用 `vector<double>`（显存 2×）；`GmcMesh.cpp` 法线按三角形面算，无法平滑着色。建议换成 float 存储 + 顶点法线生成（P2 性能项，功能正确）。

## P2（性能/健壮性/结构）

10. **GmcIntersection::relation 返回 Touch/Contains**（✅ 已修复）— 原实现只回 None/Intersect/Unknown，`Touch`/`Contains` 永远不可达，`intersects()` 把 Touch 当相交却走不到该分支。现：
    - AABB 互斥 → `None`；一方 AABB 完全包住另一方 → `Contains`（包围盒级近似，注释已注明非严格）。
    - 距离 ≤ tol → `Touch`（原为 `Intersect`；`intersects()` 对两者同为 true，边界语义无回归）。
    - 非 solid/非闭合对仍按原距离语义。`GmcIntersection.cpp`。
11. **Nesting 叉积精度与注释不符**（✅ 已修复）— `Geometry.cpp:398-405` 注释声称 `__int128` 实为 `double` 叉积。现 `__SIZEOF_INT128__` 平台用真正的 `__int128` 求符号（单调链凸包只消费方向），无 int128 平台回退 double 并注明适用范围；新增 `Nesting` 独立构建测试验证（89 用例绿）。
12. **`GmcBvh::intersectBroadphase` 交点不精确** — `GmcBvh.cpp:283-290` 命中点是两个三角形 AABB 中心的平均，非真实交点。当前仅用于“是否相交”粗判可接受；若被消费需改为真实三角形求交。
13. **`BRepBuilder` 棱柱仅取第一面**（✅ 已修复）— `BRepBuilder.cpp` 改为提取“顶面集”：面平面法向与拉伸方向一致（>0.999）的平面面片组成 compound 再 `BRepPrimAPI_MakePrism`，避免只取第一面漏掉轮廓；同时按面取向（REVERSED→取反）排除底面，且不再把侧壁一起扫出。无匹配面时退回旧的第一面行为。新增回归测试 `BRepBuilderTest.MakePrismMultiFaceBase`。
14. **`TopoShape` 死 API**（✅ 已修复）— `countFaces/countEdges/countVertices/getFace/getEdge/getVertex/forEachFace/forEachEdge/forEachVertex` 已用 `TopExp_Explorer` 实现；`makeFillet`/`makeChamfer` 走 `BRepFilletAPI`，`offsetShape`/`thicken` 走 `BRepOffsetAPI`（`PerformByJoin`/`MakeThickSolidByJoin`），`section` 委托 `GmcBoolean::section`，`repair` 委托 `GmcShapeHealing::repair`。`TopoShape.cpp`。附带修复：测试文件与头文件 API 漂移（`makeFillet({..})`/`getAllFaces` 等旧调用改齐），53 用例全绿。
15. **`PyBindCore::DocumentFacade::open` 是空桩**（待确认）— `DocumentFacade.cpp:50-53`（`save` 同为桩）。FileIO 已有完整 `Fio::SySerializer`/`SyDocument` 持久化栈，但 PyBindCore 目前未链接 FileIO，且缺 `SyDocument ↔ Eg::SceneManager` 实体转换桥。属功能开发而非缺陷修复，决定是否立项由产品拍板。
16. **PIMPL 原始指针** — 全项目（含 Renderx/GeoModelCore）`~Impl` 手工 `delete m_impl`，异常不安全。统一改为 `std::unique_ptr<Impl>`。
17. **Cross-DLL `delete` 依赖 `/MD` 共享堆** — `DocumentExportAdapter.cpp:23,49`、`ExportService.cpp:143`、`DeviceHost.cpp:229` 等。切 `/MT` 或静态 Qt 会静默崩溃，须守住一个既定查询+文档。
18. **Renderer 每帧 `stable_sort`**（已评估，维持现状）— `rxSession.cpp:562` 的排序只作用于显式 DrawPacket 提交路径（调用方每帧自主打包）；保留式列表 `DrawList::resolve` 已验证为脏标记惰性排序（`rxIncremental.cpp:627` `m_orderDirty` 守卫），不每帧重排，正是该路径相对 DrawPacket 的收益。静态大批图元应走 DrawList/GeometryStore 即可，无需改码。

## P3（清理/体验）

19. **`Tools/crash_handler.h` 脚本路径硬编码 `L"."`** — 依赖进程 CWD，改为取固定安装目录或 `argv[0]` 所在路径。
20. **Network 原始指针单例无析构顺序** — `NetworkManager.cpp:14-17`。
21. **UI 布局 builder 重复构建累积 QShortcut** — `UiLayoutBuilder` 已有去重守卫，仍建议重启时统一 `releaseBuiltShortcuts` 清理。
22. **`GmcSplit::splitByPlane` 双次全布尔** — `GmcSplit.cpp` 负半空间对正半空间再做 common/cut，需反复切片的大模型建议保留 BSP/半边结构。
23. **`GmcShapeHealing::removeDegeneratedEdges` 线性扫描** — `GmcShapeHealing.cpp:130-147` O(children × degen)，可用无序哈希提速。

> 以上条目若已处理请把对应行标记 **✅ 已完成** 并附上变更说明。