# 待办事项

## SceneEnvGeometryDesc — 让 Renderx 直通消费 SceneEnvGeometry

**状态：✅ 已完成**

当前实现：

- `Renderx` 提供纯 POD 描述符 `SceneEnvGeometryDesc` / `EnvLayerDesc`（`Renderx/include/render/render_types.h`），零 Engine 依赖。
- C API 提供 `renderSetSceneEnvDirect` 直通入口（`Renderx/src/c_api/`）。
- UI 侧 `RenderWidget::submitSceneEnvGeometry(const Eg::SceneEnvGeometry&)` 由 `Renderx` 内部完成逐顶点转换，不再拆散为平行数组。
- `UI/2D` 两处调用点（`submitSceneFromDataSource` / `submitDefaultSceneEnv`）已统一走直通提交。
- 标尺文字继续走 `renderSetScreenTexts` 通道，与几何解耦。

后续维护提醒：

- `UI/Common/Include/Render/RenderTypes.h` 为已标记 DEPRECATED 的旧渲染桥接类型，仍被 `RenderWidget` / `ViewRenderCoordinator` 使用，待桥接层完全迁移到 Renderx 后整文件移除。
- `RenderFrameUpdate` 中的 `hasBitmaps`/`bitmaps` 与 `RenderFrame::bitmaps`、`RenderBitmapQuad`、`UiTextItemList` 为未消费的死代码，`ui_texts` 走 `RenderOverlayUpdate`；清理时一并删除。