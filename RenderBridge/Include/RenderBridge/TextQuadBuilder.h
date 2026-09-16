/**
 * @file TextQuadBuilder.h
 * @brief 把 Eg::TextItem 翻译成字形四边形（P2T2C4 + 屏幕空间）
 *
 * 排版归应用层，字形归 DLL：RenderX 只出「某个码点在图集哪个位置、
 * 度量是多少」（rxFontGlyph），UTF-8 解码、字距推进、水平/垂直对齐、
 * 世界坐标→像素的换算都在这里做。renderx.h 的职责边界里明写着
 * 「不做文本布局排版」，被删除的 core/textAtlas 正是反过来做的：宿主递
 * 字符串、DLL 内部排版并自己下 draw call，于是文本无法与其他图元一起
 * 参与 sortKey 排序与批次合并。
 *
 * 坐标系：产物是 RenderSpace::Screen，**物理像素**、原点左上、y 向下
 * （与 screen_tex_p2t2c4.vert 的 `1.0 - aPos.y / uViewport.y * 2.0` 一致，
 * 也与 RenderWidget::backingSize 送进 uViewport 的值一致）。
 *
 * 用法（必须在 rxSessionBeginFrame / rxSessionEndFrame 之间）：
 *
 *     builder.beginFrame();
 *     for (const auto& item : envGeo.rulerTexts) { builder.addText(item, view, w, h); }
 *     builder.flush(session, layer, seq, outCommands);
 *
 * 一个字号一批：同字号的所有字形共用一张图集，合成一笔 DrawCommand。
 * 按项各出一笔会让 40 个刻度数字变成 40 次绘制调用。
 */
#pragma once

#include "RenderBridge/RenderBridgeAPI.h"

#include "Engine/TextItem.h"
#include "Render/RenderTypes.h"
#include "render/renderx.h"

#include <cstdint>
#include <vector>

namespace Render
{
    /**
     * 屏幕空间文字构建器（覆盖率图集），2D 与 3D 视口共用。
     *
     * 原本住在 UI/2D（`Render/TextQuadBuilder.h`），迁到 RenderBridge 是因为
     * 3D 视口也要在右下角显示帧率，而 UI3D 不依赖 UI2D（模块边界）。这个类本身
     * 与 2D 无关：输入是 `Eg::TextItem`（EngineCommon）、输出是 Screen 空间的
     * P2T2C4 字形四边形（Renderx），不碰任何 2D 场景概念。
     */
    class RENDERBRIDGE_API TextQuadBuilder
    {
    public:
        TextQuadBuilder() = default;
        ~TextQuadBuilder();

        TextQuadBuilder(const TextQuadBuilder&) = delete;
        TextQuadBuilder& operator=(const TextQuadBuilder&) = delete;

        /**
         * @brief 注入字体字节
         *
         * DLL 不做文件 IO，字体数据由宿主自己读（qrc / 磁盘）。这里保留一份
         * 副本，因为每出现一个新字号就要用它建一个新的 FontHandle——图集里
         * 存的是位图而非矢量，换字号必须重新光栅化。
         */
        bool initialize(Render::RT::RuntimeHandle runtime, const uint8_t* fontData, size_t bytes);
        void shutdown();

        bool valid() const
        {
            return Render::RT::rxValid(m_runtime) && !m_fontData.empty();
        }

        /// 清空上一帧累积的顶点。保留 FontHandle 与图集（跨帧复用）。
        void beginFrame();

        /**
         * @brief 累积一条文本的字形四边形（只算 CPU，不碰 GPU）
         *
         * @param view    世界 → NDC 矩阵，仅 coordMode 为 WorldPos_PixelSize 时使用
         * @param vpWidth/vpHeight 视口**物理像素**尺寸
         */
        void addText(const Eg::TextItem& item, const Render::Mat3f& view, uint32_t vpWidth,
            uint32_t vpHeight);

        /**
         * @brief 把累积的顶点写入瞬态环，每个字号产出一笔 DrawCommand
         *
         * 顺便把各字体图集的脏区上传（rxFontFlushAtlas）——必须在提交引用
         * 该图集的命令之前完成，否则本帧新出现的字符会采样到空白。
         */
        void flush(Render::RT::SessionHandle session, uint8_t layer, uint16_t& seq,
            std::vector<Render::RT::DrawCommand>& out);

    private:
        /// P2T2C4：位置（像素）+ UV + RGBA，32 字节
        struct GVertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };
        static_assert(sizeof(GVertex) == Render::RT::rxVertexStride(Render::RT::VertexFormat::P2T2C4),
            "GVertex 必须与 DLL 的 P2T2C4 步长一致");

        struct Batch
        {
            int pixelHeight = 0;
            Render::RT::FontHandle font = Render::RT::FontHandle::Invalid;
            std::vector<GVertex> verts;
        };

        /// 取（必要时创建）该像素高度对应的批次；失败返回 nullptr
        Batch* batchFor(int pixelHeight);

        Render::RT::RuntimeHandle m_runtime = Render::RT::RuntimeHandle::Invalid;
        std::vector<uint8_t> m_fontData;
        /// 按字号线性查找：一帧里的字号种类是个位数，哈希表反而更慢也更啰嗦
        std::vector<Batch> m_batches;
        /// 字体创建失败只告警一次，否则每个文本项一条日志会把日志刷爆
        bool m_warnedFontFailure = false;
    };
}  // namespace Render
