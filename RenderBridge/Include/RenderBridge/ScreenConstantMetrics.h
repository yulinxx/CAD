/**
 * @file ScreenConstantMetrics.h
 * @brief 屏幕定尺寸装饰的尺寸常量：唯一声明处
 *
 * 装饰在世界里有锚点，但大小恒为 N 个像素。换算在着色器中统一完成。
 *
 * 新增定尺寸装饰时的规矩是：**先在这里加一个常量**，再在业务侧引用它，
 * 不要就地写字面量。
 *
 * 单位默认是**物理像素**（与 `uViewport` = backingSize 同一坐标系，
 * 已含 devicePixelRatio），不是逻辑像素。唯一的例外是捕捉指示器
 * `kSnapIndicatorRadiusPx`：它是逻辑像素，由 OverlayScene 乘 DPR 后再送渲染。
 * `SelectionGizmo`、`BaseEditor` 的命中容差）与渲染侧（`RenderSceneBuilder`、
 * 覆盖层构建器）。覆盖层与定尺寸标记几何都要在 RenderBridge 内落地，
 * 而 UI2D 依赖 RenderBridge，常量若留在 UI2D 就成环。因此与
 * `PinnedMarkerGeometry.h` 一起下沉到这里，**命名空间仍是 `Render::`**：
 * 它是应用层绘制词汇的一部分，改名只会让几十个调用点白改一遍。
 */
#pragma once

namespace Render
{
    namespace ScreenMetrics
    {
        /// 选择缩放手柄（8 个白底蓝边方块）的全宽。
        /// 必须等于 2 × SelectionGizmo::handlePixelRadius()，否则可见方块与命中区错位。
        constexpr float kSelectionHandleSizePx = 16.0f;

        /// 旋转手柄（角点外侧绿点）的全宽
        constexpr float kRotationHandleSizePx = 14.0f;

        /// 旋转手柄相对包围盒角点向外的像素偏移。
        /// 必须原样以像素传到渲染层，不能在业务侧乘 pixelToWorldScale 固化成世界长度——
        /// 那样缩放后圆点会相对选择框漂移，而命中判定用的是当前比例。
        constexpr double kRotationHandleOffsetPx = 28.0;

        /// 图元编辑器控制点（端点/中点等）的全宽
        constexpr float kEditorControlPointSizePx = 12.0f;

        /// 绘制过程中显示的控制点（贝塞尔手柄、样条控制点、已落点…）的全宽。
        /// 比编辑器控制点小一号：绘制时点位多且在动，太大反而挡住曲线本身。
        constexpr float kDrawControlPointSizePx = 9.0f;

        /// 吸附指示器尺寸基准（**逻辑像素/点**）。各捕捉类型以它为半径基准缩放成不同形状；
        /// 与其它「物理像素」常量不同，它在 OverlayScene 里乘 devicePixelRatio
        /// 后才送入 WorldPinned，从而在 Retina / 非 Retina 上屏幕视觉尺寸一致。
        constexpr float kSnapIndicatorRadiusPx = 16.0f;

        /// **点图元本体**（SyPoint，已提交的图元）的直径。
        ///
        /// 点是唯一「图元几何本身就是屏幕定尺寸」的图元：它没有世界尺寸可言，
        /// 一个像素的点既看不见也点不中。走 DrawCommand::pointSize → gl_PointSize，
        /// 片段着色器把方形 sprite 裁成圆形（point_p3c3.frag），因此最终是**实心圆**
        /// 且不随视图缩放变化。
        ///
        /// 比绘制控制点（kDrawControlPointSizePx）略小：控制点是临时装饰要抓眼，
        /// 点图元是正式图元，跟线宽在同一视觉量级更自然。
        constexpr float kEntityPointSizePx = 7.0f;

        /// 定尺寸标记的下限：再小就点不中，也看不出形状。
        /// 业务侧传 0 或负数（未初始化）时兜底到这个值，而不是画出零面积几何。
        constexpr float kMinMarkerSizePx = 4.0f;
    }  // namespace ScreenMetrics
}  // namespace Render
