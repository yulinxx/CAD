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