/**
 * @file RasterExportHelper.cpp
 * @brief BMP/PNG 位图导出共用的光栅化 + 原子写盘实现。
 *
 * ExportWriterBase::write() 默认走 FileIOManager::exportFile → FileWriterFactory 路由，
 * 但 FileWriterFactory 没有为 BMP/PNG 注册 ILegacyWriter（只有矢量格式 DXF/SVG/PLT 等），
 * 所以 BmpExportWriter / PngExportWriter 必须覆写 write()，自己走光栅化链路。
 *
 * 本文件实现两者共用的 helper：Tessellator 离散化 → 世界坐标→像素坐标变换 →
 * Raster_DrawPathOnImage 逐实体叠加 → 临时文件原子落盘 → 取消检查 + 进度回调。
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RasterExportHelper.h"

#include "FileIO/FileIOManager.h"

#include "UiEngineAccess/Tessellator.h"
#include "Ut/Vec.h"
#include "Ut/BBox2d.h"

#include <Raster/RasterApi.h>

#include "Log/AprLogger.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QFile>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    /** @brief TessellatedEntity → 闭合标志。 */
    bool teIsClosed(const Eg::TessellatedEntity& te)
    {
        switch (te.primitiveType) {
            case Eg::TessellatedEntity::PrimitiveType::LineLoop:
            case Eg::TessellatedEntity::PrimitiveType::TriangleFan:
            case Eg::TessellatedEntity::PrimitiveType::Triangles:
                return true;
            default:
                return false;
        }
    }

    /** @brief 计算实体的世界坐标 BBox。 */
    void computeWorldBBox(const Fio::VecAprEntityPtr& entities,
                          double& minX, double& minY, double& maxX, double& maxY)
    {
        minX = std::numeric_limits<double>::max();
        minY = std::numeric_limits<double>::max();
        maxX = std::numeric_limits<double>::lowest();
        maxY = std::numeric_limits<double>::lowest();
        bool hasAny = false;
        for (const auto& ent : entities) {
            if (!ent) continue;
            const Ut::BBox2d bb = ent->computeBBox();
            if (bb.minPt.x() < minX) minX = bb.minPt.x();
            if (bb.maxPt.x() > maxX) maxX = bb.maxPt.x();
            if (bb.minPt.y() < minY) minY = bb.minPt.y();
            if (bb.maxPt.y() > maxY) maxY = bb.maxPt.y();
            hasAny = true;
        }
        if (!hasAny) {
            minX = minY = maxX = maxY = 0.0;
        }
    }

    /** @brief 三角扇表示实心区域，需要按填充还原而不是只描边。 */
    bool teIsFilled(const Eg::TessellatedEntity& te)
    {
        // 实心区域（三角形列表）也按填充处理，否则色块实体导出后只剩描边
        return te.primitiveType == Eg::TessellatedEntity::PrimitiveType::TriangleFan
            || te.primitiveType == Eg::TessellatedEntity::PrimitiveType::Triangles;
    }

    /** @brief PNG 用的标准 CRC32（多项式 0xEDB88320）。 */
    uint32_t pngCrc32(const uint8_t* data, size_t length)
    {
        static uint32_t table[256] = {};
        static bool initialized = false;
        if (!initialized)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                {
                    c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            initialized = true;
        }
        uint32_t crc = 0xFFFFFFFFU;
        for (size_t i = 0; i < length; ++i)
        {
            crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFU;
    }

    /** @brief 构造 PNG 数据块（length + type + data + crc）。 */
    QByteArray pngChunk(const char* type, const QByteArray& payload)
    {
        QByteArray chunk;
        const uint32_t length = static_cast<uint32_t>(payload.size());
        chunk.reserve(static_cast<int>(payload.size()) + 12);
        chunk.append(static_cast<char>((length >> 24) & 0xFF));
        chunk.append(static_cast<char>((length >> 16) & 0xFF));
        chunk.append(static_cast<char>((length >> 8) & 0xFF));
        chunk.append(static_cast<char>(length & 0xFF));
        chunk.append(type, 4);
        chunk.append(payload);

        QByteArray crcInput = chunk.mid(4);
        const uint32_t crc = pngCrc32(
            reinterpret_cast<const uint8_t*>(crcInput.constData()),
            static_cast<size_t>(crcInput.size()));
        chunk.append(static_cast<char>((crc >> 24) & 0xFF));
        chunk.append(static_cast<char>((crc >> 16) & 0xFF));
        chunk.append(static_cast<char>((crc >> 8) & 0xFF));
        chunk.append(static_cast<char>(crc & 0xFF));
        return chunk;
    }

    /**
     * @brief 给 PNG 补写 pHYs 块，写入物理分辨率
     *
     * OpenCV 的 imwrite 不接受 DPI 参数，导出后按 PNG 规范在 IHDR 之后插入
     * pHYs（像素/米，单位=1），保证下游软件识别到的 DPI 与导出设置一致。
     * 文件里已有 pHYs 时先剔除再插入，避免重复块。
     */
    bool patchPngDpi(const QString& path, int dpi)
    {
        if (dpi <= 0) return true;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;
        const QByteArray data = file.readAll();
        file.close();

        const int signatureSize = 8;
        const int ihdrDataSize = 13;
        const int ihdrChunkSize = 12 + ihdrDataSize;
        if (data.size() < signatureSize + ihdrChunkSize) return false;
        if (std::memcmp(data.constData(), "\x89PNG\r\n\x1a\n", 8) != 0) return false;

        const uint32_t ppm = static_cast<uint32_t>(
            std::llround(static_cast<double>(dpi) * 10000.0 / 254.0));
        QByteArray physPayload;
        physPayload.append(static_cast<char>((ppm >> 24) & 0xFF));
        physPayload.append(static_cast<char>((ppm >> 16) & 0xFF));
        physPayload.append(static_cast<char>((ppm >> 8) & 0xFF));
        physPayload.append(static_cast<char>(ppm & 0xFF));
        physPayload.append(static_cast<char>((ppm >> 24) & 0xFF));
        physPayload.append(static_cast<char>((ppm >> 16) & 0xFF));
        physPayload.append(static_cast<char>((ppm >> 8) & 0xFF));
        physPayload.append(static_cast<char>(ppm & 0xFF));
        physPayload.append(static_cast<char>(1));
        const QByteArray physChunk = pngChunk("pHYs", physPayload);

        // IHDR 本身已在上面被整块复制，pHYs 必须紧跟其后插入；
        // 之后再顺序搬运剩余块，遇到已有的 pHYs 直接丢弃
        QByteArray out = data.left(signatureSize + ihdrChunkSize);
        out.append(physChunk);
        int pos = signatureSize + ihdrChunkSize;
        while (pos + 12 <= data.size())
        {
            const uint32_t length =
                (static_cast<uint32_t>(static_cast<uint8_t>(data[pos])) << 24)
                | (static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 1])) << 16)
                | (static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 2])) << 8)
                | static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 3]));
            const QByteArray type = data.mid(pos + 4, 4);
            const int chunkSize = static_cast<int>(length) + 12;
            if (chunkSize < 12 || pos + chunkSize > data.size())
            {
                return false;
            }

            // 已有 pHYs 直接丢弃，统一用本次导出的 DPI
            if (type != QByteArrayLiteral("pHYs"))
            {
                out.append(data.mid(pos, chunkSize));
            }
            pos += chunkSize;
        }

        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        const bool ok = file.write(out) == out.size();
        file.close();
        return ok;
    }
}  // namespace

ExportResult RasterExportHelper::rasterizeExport(const ExportContext& context,
                                                  const Fio::VecAprEntityPtr& entities,
                                                  const char* formatName)
{
    if (entities.empty()) {
        QString msg = QCoreApplication::translate("RasterExport", "No entities to rasterize");
        return ExportResult::fail(msg);
    }

    auto progress = context.progressCallback;
    auto* cancel = context.cancelFlag;
    auto checkCancel = [cancel]() { return cancel && cancel->load(); };
    auto reportProgress = [progress](float p, const char* stage) {
        if (progress) progress(p, stage);
    };

    // Phase 1: 离散化（世界坐标）
    reportProgress(0.01f, "tessellate");
    Eg::Tessellator tess;
    Eg::TessellationResult tessResult;
    tessResult.entities.reserve(entities.size());
    for (const auto& ent : entities) {
        if (!ent) continue;
        Eg::TessellatedEntity& te = tessResult.entities.emplace_back();
        tess.tessellate(ent.get(), te);
    }

    if (checkCancel()) {
        return ExportResult::fail(QCoreApplication::translate("RasterExport", "Export canceled"));
    }
    reportProgress(0.05f, "compute_bbox");

    // Phase 2: BBox + 画布尺寸 + 缩放
    double minX, minY, maxX, maxY;
    computeWorldBBox(entities, minX, minY, maxX, maxY);
    const double worldW = (std::max)(1e-9, maxX - minX);
    const double worldH = (std::max)(1e-9, maxY - minY);

    const int32_t padding = 10;
    // 世界坐标单位是毫米：按导出 DPI 换算像素/毫米，scaleFactor 仅作为额外倍率
    const double dpi = (context.dpi > 0) ? static_cast<double>(context.dpi) : 96.0;
    const double baseScale = (context.scaleFactor > 0.0) ? context.scaleFactor : 1.0;
    const double scale = baseScale * dpi / 25.4;
    int32_t imgW = static_cast<int32_t>(std::ceil(worldW * scale)) + padding * 2;
    int32_t imgH = static_cast<int32_t>(std::ceil(worldH * scale)) + padding * 2;
    if (imgW < 16) imgW = 16;
    if (imgH < 16) imgH = 16;

    reportProgress(0.10f, "create_canvas");

    // Phase 3: 创建画布 + 逐实体光栅化
    RasterImage canvas = nullptr;
    RasterStatus s = Raster_CreateImage(imgW, imgH, RASTER_FORMAT_GRAY8, &canvas);
    if (s != RASTER_OK) {
        return ExportResult::fail(QStringLiteral("Raster_CreateImage failed: %1").arg(static_cast<int>(s)));
    }

    RasterRasterizeParams rParams{};
    rParams.structSize = sizeof(RasterRasterizeParams);
    rParams.strokeJoin = RASTER_JOIN_MITER;
    rParams.strokeCap = RASTER_CAP_BUTT;
    rParams.antiAlias = 1;
    rParams.fillGray = -1;
    rParams.strokeGray = 255;
    rParams.backgroundColor = 0;
    rParams.reserved = 0;

    const int totalEntities = static_cast<int>(tessResult.entities.size());
    const double pxOrigin = static_cast<double>(padding);

    for (int i = 0; i < totalEntities; ++i) {
        if (checkCancel()) {
            Raster_DestroyImage(canvas);
            return ExportResult::fail(QCoreApplication::translate("RasterExport", "Export canceled"));
        }

        const auto& te = tessResult.entities[i];
        if (te.vertices.empty()) continue;

        const bool closed = teIsClosed(te);
        const bool filled = teIsFilled(te);
        // 线宽按实体自身的像素宽度还原（该字段由渲染层以像素使用，不再乘 DPI 比例），
        // 实心区域按绕数填充
        rParams.lineWidth = (std::max)(1.0, static_cast<double>(te.lineWidth));
        rParams.fillRule = filled ? RASTER_FILL_WINDING : RASTER_FILL_NONE;
        rParams.fillGray = 255;
        std::vector<double> pixelPts;
        pixelPts.reserve(te.vertices.size() * 2);
        for (const Ut::Vec2d& v : te.vertices) {
            const double px = (v.x() - minX) * scale + pxOrigin;
            const double py = (maxY - v.y()) * scale + pxOrigin;
            pixelPts.push_back(px);
            pixelPts.push_back(py);
        }

        s = Raster_DrawPathOnImage(canvas, pixelPts.data(),
                                   static_cast<int32_t>(te.vertices.size()),
                                   closed, &rParams);
        if (s != RASTER_OK) {
            Raster_DestroyImage(canvas);
            return ExportResult::fail(QStringLiteral("Raster_DrawPathOnImage failed: %1").arg(static_cast<int>(s)));
        }

        const float progressPhase = 0.1f + 0.7f * static_cast<float>(i + 1) / static_cast<float>(totalEntities);
        reportProgress(progressPhase, "rasterize");
    }

    reportProgress(0.82f, "writing");

    // Phase 4: 原子写盘（先 .tmp 成功再 rename，取消/失败不产出半成品）
    // 临时文件必须保留原扩展名，否则 OpenCV 无法根据后缀选择编码器
    QString target = context.targetPath;
    QString formatLower = QString::fromLatin1(formatName).toLower();
    QString tempPath = target + QStringLiteral(".tmp.") + formatLower;

    s = Raster_SaveImage(canvas, tempPath.toUtf8().constData());
    Raster_DestroyImage(canvas);

    if (s != RASTER_OK) {
        QFile::remove(tempPath);
        return ExportResult::fail(QStringLiteral("Raster_SaveImage failed: %1").arg(static_cast<int>(s)));
    }

    // PNG 规范里没有 DPI 参数，导出后补 pHYs 块
    if (formatLower == QStringLiteral("png")) {
        patchPngDpi(tempPath, static_cast<int>(std::llround(dpi)));
    }

    reportProgress(0.95f, "finalizing");

    if (checkCancel()) {
        QFile::remove(tempPath);
        return ExportResult::fail(QCoreApplication::translate("RasterExport", "Export canceled"));
    }

    QFile::remove(target);
    if (!QFile::rename(tempPath, target)) {
        QFile::remove(tempPath);
        return ExportResult::fail(QCoreApplication::translate("RasterExport", "Failed to move temp file"));
    }

    reportProgress(1.0f, "done");

    QString successMsg = QCoreApplication::translate("RasterExport", "%1 export successful (%2 entities)")
        .arg(QString::fromLocal8Bit(formatName)).arg(static_cast<int>(entities.size()));
    return ExportResult::ok(successMsg, static_cast<int>(entities.size()));
}
