#pragma once
#include "IRenderTypes.h"

namespace RenderAbstraction {

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    /// 后端名（如 "OpenGL"），用于 3D 视口的 GL 信息面板
    virtual const char* backendName() const = 0;
    /// 后端报告的设备名（如 "Apple M1 Pro"），同上
    virtual const char* deviceName() const = 0;

    // 这里曾有 maxLineWidth / maxTextureSize 两条能力查询，已删：有实现、无调用者。
    // 线宽上限只有后端内部用（Renderx 自己钳制），UI 要粗线时自己三角化；
    // 纹理上限没人看，纹理由 UI 自建，越界在 createTexture 时就失败了。
    // 留着等于把后端能力表照搬到抽象层，与已删的 getDefaultPipeline(格式,空间,拓扑)
    // 同性质。需要时再加回。

    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    virtual void updateTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) = 0;

    // ---------- 常驻几何仓 ----------

    /// 创建几何仓（无效句柄表示失败）。单仓有容量硬顶，超出后调用方应另开一仓，
    /// 见 GeometryAllocResult::StoreFull
    virtual GeometryStoreHandle createGeometryStore(const GeometryStoreDesc& desc) = 0;
    virtual void destroyGeometryStore(GeometryStoreHandle store) = 0;

    /// 分配一块。StoreFull 表示本仓已满，换仓后重试即可
    virtual GeometryAllocResult allocGeometry(GeometryStoreHandle store, uint64_t bytes,
                                              GeometryBlock& out) = 0;

    /// 写入块内数据。只标脏区间，不立即上传
    virtual bool writeGeometry(GeometryStoreHandle store, uint64_t blockId, uint32_t byteOffset,
                               uint32_t sizeBytes, const void* data) = 0;

    /// 释放一块。空闲表会与相邻空洞合并，避免碎片累积
    virtual void freeGeometry(GeometryStoreHandle store, uint64_t blockId) = 0;

    /// 主动把累积的脏区间刷到 GPU。帧内提交时场景会自动刷，
    /// 只有在帧外批量建场景时才需要
    virtual void flushGeometry(GeometryStoreHandle store) = 0;

    virtual GeometryStoreStats geometryStoreStats(GeometryStoreHandle store) = 0;

    /// 取具名内建管线（见 PipelineKind）。0 表示该后端没有这条管线。
    /// DrawInstruction::pipelineIndex 留 0 则由后端按 (格式, 空间, 拓扑) 自行解析
    virtual uint32_t defaultPipeline(PipelineKind kind) = 0;

    /// 按描述创建自定义管线（0 表示失败）。返回值填 DrawInstruction::pipelineIndex
    virtual uint32_t createPipeline(const PipelineDesc& desc) = 0;

    // ---------- 材质 ----------

    /// 创建材质，返回其编号（0 表示失败）。该编号填 DrawInstruction::materialIndex
    virtual uint32_t createMaterial(const MaterialDesc& desc) = 0;
    /// 原地更新材质。每次都 Add 会把材质表撑成「图元数 × 编辑次数」
    virtual bool updateMaterial(uint32_t material, const MaterialDesc& desc) = 0;

    // ---------- 字体 ----------

    /// 创建字体（光栅化器 + 专属字形图集）。
    /// 图集是懒填充的：创建时不预烘，字形在首次 fontGlyph 时才光栅化
    virtual FontHandle createFont(const FontDesc& desc) = 0;
    virtual void destroyFont(FontHandle font) = 0;
    virtual bool fontMetrics(FontHandle font, FontMetrics& out) = 0;

    /// 查字形；未光栅化则就地光栅化并写入图集。
    /// 只改 CPU 侧图集影子，不碰 GPU —— 上传统一由 flushFontAtlas 做
    virtual bool fontGlyph(FontHandle font, uint32_t codepoint, GlyphInfo& out) = 0;

    /// 把图集脏区上传。必须在提交引用该图集的命令之前完成，否则新字形采样到空白
    virtual bool flushFontAtlas(FontHandle font) = 0;

    /// 取图集纹理，填进 DrawInstruction::texture。字体销毁后该句柄立即失效
    virtual TextureHandle fontAtlas(FontHandle font) = 0;
};

} // namespace RenderAbstraction
