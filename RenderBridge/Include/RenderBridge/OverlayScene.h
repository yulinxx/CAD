#pragma once
/**
 * @file OverlayScene.h
 * @brief 覆盖层：按层持有交互/选择装饰，逐帧合成到瞬态环
 *
 * ## 它取代了什么
 *
 * 覆盖层原先由 UI2D 的 `OverlayState`（一个装了 7 组 `has*` 标志的扁平结构）
 * 累积、由 `OverlaySceneBuilder::build` 翻译成 DrawCommand。扁平结构的问题是
 * 每次修改都得理解整体：`SelectionGizmo` 一次重绘会连续触发四个回调，
 * 任何一处误整体重置就会抹掉别人的成果（历史上出过「选中后虚线和选择框都不显示」）。
 *
 * 现在每一组装饰是一个**独立的层**：谁产生谁设置，谁设置谁清除，
 * 层与层之间互不可见。提交时按 `OverlayLayerId` 的枚举顺序合成 ——
 * 枚举顺序即绘制叠放顺序，这一点写死在类型里，不再靠一个共享的自增序号。
 *
 * ## 为什么走瞬态环而不是几何仓
 *
 * 覆盖层（橡皮筋、手柄、流水虚线）**每帧都变**，这正是瞬态环
 * （`rxSessionAllocTransient`）的目标负载：每帧全量重传，帧末自动回收。
 * 塞进 GeometryStore 只会让「常驻」这个前提失效，每帧都要 alloc/free 一遍，
 * 反而比瞬态环慢（`PersistentGeometryStore` 服务的是场景图元，不是这一类）。
 *
 * ## 分层判据
 *
 * - 覆盖层坐标一律是**世界坐标**，由 World 管线经 uView 变换。
 * - 「手柄 / 旋转点 / 吸附指示器」这类**标记**走 `RenderSpace::WorldPinned`：
 *   锚点跟随平移，尺寸恒定为若干物理像素，不随缩放变化。像素换算在
 *   `world_pinned_p3o2c4.vert` 里完成，业务层不参与（见新渲染架构 §15）。
 * - 需要透明度（半透明填充）统一用 P3C4；WorldPinned 用 P3O2C4。
 * - `pipelineIndex` 留 0，让 Runtime 按 (格式, 空间, 拓扑) 自行解析 ——
 *   默认管线已开启 SrcAlpha/OneMinusSrcAlpha 混合。
 * - 排序层级固定 `layer=200, transparent=1`，保证绘制在图元（layer=100）之上。
 *
 * ## 逐帧参数
 *
 * 虚线节距/相位与捕捉标记尺寸是**像素基准**，而顶点是世界坐标或像素偏移，
 * 因此离散化时必须知道本帧的 `pixelToWorld` 与 `devicePixelRatio`。
 * 这两个值由视口在提交前通过 `setFrameParams` 注入，不由层自己持有 ——
 * 否则每层都要跟着窗口缩放改一遍。
 */

#include "RenderBridge/RenderBridgeAPI.h"

#include "Render/RenderTypes.h"
#include "render/renderx.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RenderBridge
{
    /**
     * @brief 覆盖层层号
     *
     * **枚举顺序即提交顺序即绘制叠放顺序**：排在后面的压在前面的上面。
     * 覆盖层统一在 `layer=200`，因此这里的顺序只决定覆盖层内部的层次。
     */
    enum class OverlayLayerId : uint8_t
    {
        SelectionBox = 0,      ///< 选中集总包围盒（只描边不填充）
        SelectionHighlight,    ///< 文字选区高亮（半透明四边形，可多块）
        SelectionRect,         ///< 框选预览矩形
        SelectionOutlines,     ///< 流水虚线轮廓（像素基准，逐帧离散）
        SelectionHandles,      ///< 缩放/变换手柄（屏幕定尺寸）
        PointMarkers,          ///< 点标记：旋转手柄 / 编辑器控制点（屏幕定尺寸）
        SnapIndicator,         ///< 捕捉指示器（屏幕定尺寸）
        ToolPreview,           ///< 绘制预览折线
        ControlLines,          ///< 辅助线（切线手柄、十字准星、文字光标条）
        Count
    };

    /// 捕捉标记形状（每个捕捉类型对应唯一形状，颜色由调用方决定）
    enum class SnapMarkerShape : uint8_t
    {
        Circle,
        Square,
        Diamond,
        Cross,
        X,
        Triangle,
        Arc,
        RightAngle,
        Dot,
        Star,
        Hexagon,
        Hourglass
    };

    /**
     * @brief 一组屏幕定尺寸方块标记
     *
     * 选择手柄与点标记除了「读哪几个字段」之外逐行同构，用这一份描述归并成
     * 同一段消费代码。每个标记可单独给颜色（悬停/激活高亮），为空则用统一色。
     */
    struct OverlayMarkerGroup
    {
        /// 世界锚点
        std::vector<Render::Vec2f> anchors;
        /// 锚点之上再叠加的像素偏移；为空或长度不足时按 (0,0) 处理
        std::vector<Render::Vec2f> offsetsPx;
        /// 方块全宽（物理像素）。传 0 会被夹到下限，而不是画出零面积几何
        float sizePx = 0.0f;
        Render::Color fill{ 1.0f, 1.0f, 1.0f, 1.0f };
        Render::Color border{ 0.0f, 0.0f, 0.0f, 1.0f };
        std::vector<Render::Color> perMarkerFills;
        std::vector<Render::Color> perMarkerBorders;

        bool empty() const { return anchors.empty(); }
    };

    /// 逐帧参数：像素↔世界的换算比例与流水虚线相位
    struct OverlayFrameParams
    {
        /// 一个屏幕像素对应多少世界单位。像素基准的节距/相位靠它换算到世界坐标
        float pixelToWorld = 1.0f;
        /// 流水虚线相位（像素），由视口的动画定时器推进
        float dashOffsetPx = 0.0f;
    };

    /**
     * @brief 覆盖层的层容器与提交器
     *
     * 生命周期与视口一致：各层跨帧持有，只有被显式 `clearLayer` / `clear` 或
     * 被同一个 setter 再次设置时才变。顶点每帧重新离散并写进瞬态环。
     */
    class RENDERBRIDGE_API OverlayScene
    {
    public:
        OverlayScene() = default;

        OverlayScene(const OverlayScene&) = delete;
        OverlayScene& operator=(const OverlayScene&) = delete;

        // ---------- 逐层设置：设一层只影响那一层 ----------

        /// 选中集总包围盒。bbox 退化（无选中）即清除本层
        void setSelectionBox(const Render::BBox2d& box, const Render::Color& border);

        /**
         * @brief 文字选区高亮：N 块半透明四边形
         *
         * `quadCorners` 每 4 个顶点一块（逆时针，世界坐标），长度必须是 4 的倍数。
         * 传空即清除本层。跨行选区在这里表达为「每行一块」，而不是一个跨行的并集
         * 大矩形 —— 并集会把两行之间未选中的部分一起涂上颜色。
         */
        void setSelectionHighlight(const std::vector<Render::Vec2f>& quadCorners,
            const Render::Color& fill, const Render::Color& border);

        /// 框选预览矩形（半透明填充 + 描边）。bbox 退化即清除本层
        void setSelectionRect(const Render::BBox2d& rect, const Render::Color& fill,
            const Render::Color& border);

        /// 流水虚线轮廓。传空即清除本层
        void setSelectionOutlines(std::vector<Render::SelectionOutlinePath> paths);

        /// 虚线样式（像素基准）。样式本身不是几何，只影响轮廓层的离散化
        void setSelectionDashStyle(const Render::SelectionDashStyle& style);

        /// 缩放/变换手柄层。传空 group 即清除本层
        void setSelectionHandles(OverlayMarkerGroup group);

        /// 点标记层（旋转手柄 / 编辑器控制点 / 绘制控制点）。传空 group 即清除本层
        void setPointMarkers(OverlayMarkerGroup group);

        /// 捕捉指示器。形状与颜色由调用方解析（它才认识捕捉类型）
        void setSnapIndicator(const Render::Vec2f& worldPos, bool visible,
            SnapMarkerShape shape, const Render::Color& color);

        /// 绘制预览折线（世界坐标，相邻点连成线段）。传空即清除本层几何
        void setToolPreview(const std::vector<Render::Vec2f>& points);
        /// 预览折线颜色。**与几何分开设置**：调用方只在自己携带了颜色时才覆盖它
        /// （绘制色是图层色，不是每帧必给的参数）
        void setToolPreviewColor(const Render::Color& color);

        /// 辅助线（世界坐标，两两成段；切线手柄、十字准星、文字光标条）。传空即清除本层几何
        void setControlLines(const std::vector<Render::Vec2f>& points);
        /// 辅助线颜色。与几何分开设置，理由同预览折线；且它**不**跟随绘制色 ——
        /// 辅助线是绘制过程的参考物，必须始终看得见，所以有自己的通道
        void setControlLinesColor(const Render::Color& color);

        // ---------- 清除 ----------

        /// 清除某一层
        void clearLayer(OverlayLayerId id);
        /// 清除全部层
        void clear();

        // ---------- 帧参数与查询 ----------

        void setFrameParams(const OverlayFrameParams& params);

        /**
         * @brief 设置控件 devicePixelRatio
         *
         * 与 `setFrameParams` 分开：DPR 只有在渲染帧里读控件才知道（只有视口那一处
         * 能读到），而 pixelToWorld / 虚线相位是视口算好一起给的，两者时机不同。
         *
         * WorldPinned 的像素偏移是**物理**像素，而捕捉指示器的尺寸按**逻辑**像素
         * 声明，因此要乘 DPR 换算，才能在 Retina / 非 Retina 上屏幕视觉尺寸一致。
         */
        void setDevicePixelRatio(float devicePixelRatio);

        /// 当前是否有需要推进虚线相位的轮廓层（决定视口要不要跑动画定时器）
        bool hasAnimatedSelectionOutlines() const;

        /// 某层当前是否有内容，用于诊断
        bool hasLayer(OverlayLayerId id) const;

        /**
         * @brief 把各层合成到瞬态环并追加 DrawCommand
         *
         * 必须由视口在 `rxSessionBeginFrame` / `rxSessionEndFrame` 之间调用。
         * 环容量不足时丢弃该批（DLL 返回无效句柄），宁可这一帧少画一笔，
         * 也不画出错位几何。
         */
        void submit(Render::RT::SessionHandle session, std::vector<Render::RT::DrawCommand>& out) const;

    private:
        struct SelectionBoxLayer
        {
            bool valid = false;
            Render::BBox2d box;
            Render::Color border{ 1.0f, 0.55f, 0.0f, 0.86f };
        };

        struct HighlightLayer
        {
            std::vector<Render::Vec2f> quadCorners;
            Render::Color fill{ 0.0f, 0.47f, 1.0f, 0.27f };
            Render::Color border{ 0.0f, 0.47f, 1.0f, 0.78f };
        };

        struct SelectionRectLayer
        {
            bool valid = false;
            Render::BBox2d rect;
            Render::Color fill{ 0.0f, 0.6f, 1.0f, 0.15f };
            Render::Color border{ 0.0f, 0.6f, 1.0f, 1.0f };
        };

        struct OutlineLayer
        {
            std::vector<Render::SelectionOutlinePath> paths;
            Render::SelectionDashStyle dash;
        };

        struct MarkerLayer
        {
            OverlayMarkerGroup group;
        };

        struct SnapLayer
        {
            bool visible = false;
            Render::Vec2f worldPos;
            SnapMarkerShape shape = SnapMarkerShape::Circle;
            Render::Color color{ 0.0f, 1.0f, 0.0f, 1.0f };
        };

        struct LineLayer
        {
            std::vector<Render::Vec2f> points;
            Render::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        SelectionBoxLayer m_selectionBox;
        HighlightLayer m_highlight;
        SelectionRectLayer m_selectionRect;
        OutlineLayer m_outlines;
        MarkerLayer m_selectionHandles;
        MarkerLayer m_pointMarkers;
        SnapLayer m_snap;
        LineLayer m_toolPreview{ {}, Render::Color(0.2f, 0.2f, 1.0f, 1.0f) };
        LineLayer m_controlLines{ {}, Render::Color(1.0f, 0.2f, 0.2f, 0.8f) };
        OverlayFrameParams m_frameParams;
        /// 本帧控件 DPR，由 setDevicePixelRatio 注入（见该函数的说明）
        float m_devicePixelRatio = 1.0f;
    };
}  // namespace RenderBridge
