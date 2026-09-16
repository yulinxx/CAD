/**
 * @file TextQuadBuilder.cpp
 * @brief TextQuadBuilder 实现（见头文件说明）
 *
 * 排版规则全部在这里，DLL 一概不知。三处旧实现的缺陷在此修正：
 *
 * 1. UTF-8。旧 buildTextQuads 逐**字节**当码点（`for (const char* p = text; *p; ++p)`），
 *    ASCII 正常，任何中文会被拆成 2~3 个 0x80~0xFF 的伪码点，画出乱码方块。
 * 2. vAlign 编号。旧实现用 1=Top / 2=Middle / 3=Bottom，而 Eg::UiTextVAlign 是
 *    0=Top / 1=Middle / 2=Bottom，整体错位一位。这里直接对 Eg 的枚举做 switch，
 *    不再有第二套编号。
 * 3. 行高用字体级度量（ascent/descent）而不是「本行字形包围盒」。后者会让
 *    「100」和「120」因为有没有下伸部而落在不同基线上，标尺数字看起来在抖。
 */
#include "RenderBridge/TextQuadBuilder.h"

#include "Log/SyLogger.h"
#include "RenderBridge/TextLayoutUtf8.h"

#include <cmath>
#include <cstring>

namespace Render
{
    namespace
    {
        /// 字形图集边长。标尺数字 + 常见标注远小于此，一张够用。
        constexpr uint32_t kAtlasSide = 1024;

        /// 世界坐标 → 物理像素。view 是列主序的世界→NDC 矩阵（Ut::Mat3f）
        void worldToPixel(const Render::Mat3f& view, float wx, float wy, uint32_t vpWidth,
            uint32_t vpHeight, float& outX, float& outY)
        {
            // 列主序：data[col * 3 + row]，与 RenderWidget::mat3ToMat4 的取法一致
            const float ndcX = view.data[0] * wx + view.data[3] * wy + view.data[6];
            const float ndcY = view.data[1] * wx + view.data[4] * wy + view.data[7];
            outX = (ndcX * 0.5f + 0.5f) * static_cast<float>(vpWidth);
            // 屏幕空间 y 向下，NDC y 向上，故取反
            outY = (0.5f - ndcY * 0.5f) * static_cast<float>(vpHeight);
        }
    }  // namespace

    TextQuadBuilder::~TextQuadBuilder()
    {
        shutdown();
    }

    bool TextQuadBuilder::initialize(Render::RT::RuntimeHandle runtime, const uint8_t* fontData,
        size_t bytes)
    {
        shutdown();
        if (!Render::RT::rxValid(runtime) || !fontData || bytes == 0)
        {
            return false;
        }
        m_runtime = runtime;
        m_fontData.assign(fontData, fontData + bytes);
        return true;
    }

    void TextQuadBuilder::shutdown()
    {
        for (Batch& batch : m_batches)
        {
            if (Render::RT::rxValid(batch.font))
            {
                Render::RT::rxFontDestroy(m_runtime, batch.font);
            }
        }
        m_batches.clear();
        m_fontData.clear();
        m_runtime = Render::RT::RuntimeHandle::Invalid;
        m_warnedFontFailure = false;
    }

    void TextQuadBuilder::beginFrame()
    {
        // 只清顶点，不动 FontHandle：图集是跨帧资源，重建等于每帧重新光栅化。
        for (Batch& batch : m_batches)
        {
            batch.verts.clear();
        }
    }

    TextQuadBuilder::Batch* TextQuadBuilder::batchFor(int pixelHeight)
    {
        for (Batch& batch : m_batches)
        {
            if (batch.pixelHeight == pixelHeight)
            {
                return Render::RT::rxValid(batch.font) ? &batch : nullptr;
            }
        }

        Render::RT::FontDesc desc{};
        desc.data = m_fontData.data();
        desc.dataBytes = m_fontData.size();
        desc.pixelHeight = static_cast<float>(pixelHeight);
        desc.atlasWidth = kAtlasSide;
        desc.atlasHeight = kAtlasSide;

        Render::RT::FontHandle font = Render::RT::FontHandle::Invalid;
        const Render::RT::RxResult result = Render::RT::rxFontCreate(m_runtime, &desc, &font);
        if (result != Render::RT::RxResult::Ok || !Render::RT::rxValid(font))
        {
            if (!m_warnedFontFailure)
            {
                m_warnedFontFailure = true;
                // 字号 %d 的字体创建失败（错误码 %d），文字不会显示
                SY_ERRORF("TextQuadBuilder: font creation failed for size %d (error %d), text will not be displayed",
                    pixelHeight, static_cast<int>(result));
            }
            // 仍然登记这一项：否则每帧每条文本都会重试一次创建。
            m_batches.push_back(Batch{ pixelHeight, Render::RT::FontHandle::Invalid, {} });
            return nullptr;
        }

        m_batches.push_back(Batch{ pixelHeight, font, {} });
        return &m_batches.back();
    }

    void TextQuadBuilder::addText(const Eg::TextItem& item, const Render::Mat3f& view,
        uint32_t vpWidth, uint32_t vpHeight)
    {
        if (!valid() || item.text[0] == '\0' || item.fontSize <= 0 || vpWidth == 0 || vpHeight == 0)
        {
            return;
        }

        Batch* batch = batchFor(item.fontSize);
        if (!batch)
        {
            return;
        }

        Render::RT::FontMetrics metrics{};
        if (Render::RT::rxFontMetrics(m_runtime, batch->font, &metrics) != Render::RT::RxResult::Ok)
        {
            return;
        }

        const size_t textLength = std::strlen(item.text);

        // 第一遍：量总宽。对齐需要它，而 advance 只能逐字形问出来。
        float totalWidth = 0.0f;
        for (size_t i = 0; i < textLength;)
        {
            uint32_t codepoint = 0;
            i += decodeUtf8(item.text + i, textLength - i, codepoint);
            Render::RT::GlyphInfo glyph{};
            if (Render::RT::rxFontGlyph(m_runtime, batch->font, codepoint, &glyph)
                != Render::RT::RxResult::Ok)
            {
                continue;
            }
            totalWidth += glyph.advance;
        }
        if (totalWidth <= 0.0f)
        {
            return;
        }

        // 锚点换算到物理像素
        float anchorX = item.x;
        float anchorY = item.y;
        if (item.coordMode == Eg::UiTextCoordMode::WorldPos_PixelSize)
        {
            worldToPixel(view, item.x, item.y, vpWidth, vpHeight, anchorX, anchorY);
        }

        float penX = anchorX;
        switch (item.hAlign)
        {
        case Eg::UiTextHAlign::Center:
            penX -= totalWidth * 0.5f;
            break;
        case Eg::UiTextHAlign::Right:
            penX -= totalWidth;
            break;
        case Eg::UiTextHAlign::Left:
        default:
            break;
        }

        // descent 为负值，文本高度 = ascent - descent
        const float textHeight = metrics.ascent - metrics.descent;
        float baselineY = anchorY;
        switch (item.vAlign)
        {
        case Eg::UiTextVAlign::Top:
            baselineY = anchorY + metrics.ascent;
            break;
        case Eg::UiTextVAlign::Middle:
            baselineY = anchorY - textHeight * 0.5f + metrics.ascent;
            break;
        case Eg::UiTextVAlign::Bottom:
            baselineY = anchorY + metrics.descent;
            break;
        default:
            break;
        }

        // 对齐到整像素。图集里是按整像素光栅化的位图，落在半像素上会被
        // 双线性插值糊掉一圈，小字号下尤其明显。
        penX = std::round(penX);
        baselineY = std::round(baselineY);

        const float cr = item.color.r();
        const float cg = item.color.g();
        const float cb = item.color.b();
        const float ca = item.color.a();

        for (size_t i = 0; i < textLength;)
        {
            uint32_t codepoint = 0;
            i += decodeUtf8(item.text + i, textLength - i, codepoint);

            Render::RT::GlyphInfo glyph{};
            if (Render::RT::rxFontGlyph(m_runtime, batch->font, codepoint, &glyph)
                != Render::RT::RxResult::Ok)
            {
                continue;
            }
            // 空白字形（空格）与字体缺字：只推进笔位置，不产四边形
            if (glyph.width > 0.0f && glyph.height > 0.0f)
            {
                const float x0 = penX + glyph.bearingX;
                const float y0 = baselineY + glyph.bearingY;
                const float x1 = x0 + glyph.width;
                const float y1 = y0 + glyph.height;

                const GVertex lt{ x0, y0, glyph.u0, glyph.v0, cr, cg, cb, ca };
                const GVertex rt{ x1, y0, glyph.u1, glyph.v0, cr, cg, cb, ca };
                const GVertex rb{ x1, y1, glyph.u1, glyph.v1, cr, cg, cb, ca };
                const GVertex lb{ x0, y1, glyph.u0, glyph.v1, cr, cg, cb, ca };

                // 两个三角形而不是 TriangleFan：RT::PrimitiveTopology 没有 Fan
                // （Metal 不支持该拓扑），这也是所有字形能合成一笔的前提。
                batch->verts.push_back(lt);
                batch->verts.push_back(rt);
                batch->verts.push_back(rb);
                batch->verts.push_back(lt);
                batch->verts.push_back(rb);
                batch->verts.push_back(lb);
            }
            penX += glyph.advance;
        }
    }

    void TextQuadBuilder::flush(Render::RT::SessionHandle session, uint8_t layer, uint16_t& seq,
        std::vector<Render::RT::DrawCommand>& out)
    {
        if (!valid() || !Render::RT::rxValid(session))
        {
            return;
        }

        const uint16_t glyphPipeline =
            Render::RT::rxPipelineGetDefault(m_runtime, Render::RT::DefaultPipeline::ScreenGlyph);

        for (Batch& batch : m_batches)
        {
            if (batch.verts.empty() || !Render::RT::rxValid(batch.font))
            {
                continue;
            }

            // 本帧新出现的字形还只在 CPU 影子里，必须先上传，否则采样到空白。
            Render::RT::rxFontFlushAtlas(m_runtime, batch.font);

            const uint64_t bytes = static_cast<uint64_t>(batch.verts.size()) * sizeof(GVertex);
            Render::RT::TransientAlloc alloc{};
            Render::RT::rxSessionAllocTransient(session, bytes, &alloc);
            // 环容量不足时 DLL 返回无效句柄；丢弃本批而不是画出错误几何。
            if (!Render::RT::rxValid(alloc.buffer) || !alloc.cpuPtr)
            {
                continue;
            }
            std::memcpy(alloc.cpuPtr, batch.verts.data(), static_cast<size_t>(bytes));

            Render::RT::DrawCommand cmd{};
            cmd.vertexBuffer = alloc.buffer;
            // 与 TransientAlloc::offset 同为**字节**偏移，原样填入，不除 stride
            cmd.vertexOffset = alloc.offset;
            cmd.vertexCount = static_cast<uint32_t>(batch.verts.size());
            cmd.instanceCount = 1;
            cmd.topology = Render::RT::PrimitiveTopology::Triangles;
            cmd.space = Render::RT::RenderSpace::Screen;
            cmd.vertexFormat = Render::RT::VertexFormat::P2T2C4;
            cmd.indexType = Render::RT::IndexType::None;
            cmd.texture = Render::RT::rxFontAtlas(m_runtime, batch.font);
            // 必须显式指定管线：字形与位图同为 P2T2C4 + Screen + Triangles，
            // 让 Runtime 自行解析会命中 ScreenTextured，把 R8 覆盖率当 RGBA 采样，
            // 结果是纯红色的字（g/b 通道恒为 0）。
            cmd.pipelineIndex = glyphPipeline;
            cmd.sortKey = Render::RT::rxMakeSortKey(layer, 1, 0, seq++);
            out.push_back(cmd);
        }
    }
}  // namespace Render
