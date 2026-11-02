/**
 * @file RenderBridgeTypes.h
 * @brief RenderBridge 模块自己的中性 POD 类型
 *
 * 本文件是 RenderBridge 模块唯一的类型来源：
 * - 数学类型 Vec2f/Color/BBox2d/Mat3f 直接用 Utility 命名空间 Ut:: 的
 *   纯 POD（无 DLL 依赖），避免 RenderBridge 的公共头再 include RenderTypes.h
 *   或 Engine/TextItem.h 等上层模块的头文件
 * - 覆盖层描述结构体（SelectionOutlinePath / SelectionDashStyle / TextLayoutInfo）
 *   在本模块内一次性定义，取代此前 UICommon 提供的同名结构体（Render:: 下的别名）
 *
 * 这种收束让 RenderBridge 成为真正的类型中性模块：下游（UI2D/UI3D/Main/Engine2D）
 * 不再因 include RenderBridge 公共头而被迫引入 UICommon 和 EngineCommon 的 include 路径。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Ut/BBox2d.h"
#include "Ut/Color.h"
#include "Ut/Mat.h"
#include "Ut/Vec.h"

namespace RenderBridge
{
    // ======================================================================
    // 数学类型：直接使用 Utility 的 Ut:: 命名空间，本模块内不再有 Render:: 别名
    // ======================================================================

    using Vec2f = Ut::Vec2f;
    using Color = Ut::Color;
    using BBox2d = Ut::BBox2d;
    using Mat3f = Ut::Mat3f;

    // ======================================================================
    // 覆盖层虚线相关
    // ======================================================================

    /// 虚线上单个顶点：位置 + 沿轮廓累计弧长（供虚线相位换算用）
    struct SelectionOutlineVertex
    {
        Ut::Vec2f position;
        float arcLength = 0;
    };

    /// 一条带颜色的虚线轮廓折线
    struct SelectionOutlinePath
    {
        std::vector<SelectionOutlineVertex> vertices;
        Ut::Color color{ 1.0f, 0.55f, 0.0f, 0.86f };
    };

    /// 流水虚线的样式参数（全部像素基准，OverlayScene::setSelectionDashStyle 设置）
    struct SelectionDashStyle
    {
        bool enabled{ true };
        bool animated{ true };
        float dashLengthPx{ 10.0f };
        float gapLengthPx{ 8.0f };
        float speedPxPerSec{ 48.0f };
    };

    // ======================================================================
    // 中性文本排版信息：替代 Eg::TextItem，供 TextQuadBuilder::addText 使用
    //
    // 与 Eg::UiTextCoordMode / Eg::UiTextHAlign / Eg::UiTextVAlign 的
    // 语义与底层类型保持一致，只是换了 uint8_t 让 RenderBridge 不再
    // 依赖 EngineCommon 的头文件。
    // ======================================================================

    /// 坐标模式：0=像素坐标（左上原点，y向下），1=世界坐标 + 像素字号
    constexpr uint8_t kCoordModePixel = 0;
    constexpr uint8_t kCoordModeWorldPos = 1;

    /// 水平对齐：0=左，1=居中，2=右
    constexpr uint8_t kHAlignLeft = 0;
    constexpr uint8_t kHAlignCenter = 1;
    constexpr uint8_t kHAlignRight = 2;

    /// 垂直对齐：0=顶，1=中，2=底
    constexpr uint8_t kVAlignTop = 0;
    constexpr uint8_t kVAlignMiddle = 1;
    constexpr uint8_t kVAlignBottom = 2;

    /**
     * @brief 一条待渲染的文本排版描述
     *
     * utf8 指针由调用方持有，RenderBridge 只在 addText 调用的作用域内读取它；
     * 字串必须零终止。排版规则（对齐、锚点换算、像素对齐）全部在 TextQuadBuilder.cpp 里，
     * 这里只承载原始输入。
     */
    struct TextLayoutInfo
    {
        /// 零终止 UTF-8 字符串（调用方保证生命周期长于本次 addText 调用）
        const char* utf8 = "";
        /// 锚点世界坐标（coordMode=1 时由 view 矩阵换算到像素）
        Ut::Vec2f worldPos;
        /// 像素字号
        int fontSize = 10;
        /// RGBA 颜色
        Ut::Color color;
        /// 0=像素坐标，1=世界坐标 + 像素字号（与 Eg::UiTextCoordMode 语义一致）
        uint8_t coordMode = 0;
        /// 水平对齐（0=左，1=居中，2=右）
        uint8_t hAlign = 0;
        /// 垂直对齐（0=顶，1=居中，2=底）
        uint8_t vAlign = 0;
    };

}  // namespace RenderBridge
