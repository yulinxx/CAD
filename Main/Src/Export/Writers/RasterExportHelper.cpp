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

#include "Log/SyLogger.h"

#include <QCoreApplication>
#include <QFile>

#include <atomic>
#include <cmath>
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
                return true;
            default:
                return false;
        }
    }

    /** @brief 计算实体的世界坐标 BBox。 */
    void computeWorldBBox(const Fio::VecSyEntityPtr& entities,
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
}  // namespace

ExportResult RasterExportHelper::rasterizeExport(const ExportContext& context,
                                                  const Fio::VecSyEntityPtr& entities,
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
    const double scale = (context.scaleFactor > 0.0) ? context.scaleFactor : 1.0;
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
    rParams.fillRule = RASTER_FILL_NONE;
    rParams.strokeJoin = RASTER_JOIN_MITER;
    rParams.strokeCap = RASTER_CAP_BUTT;
    rParams.antiAlias = 1;
    rParams.lineWidth = 1.5;
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
    QString target = context.targetPath;
    QString tempPath = target + QStringLiteral(".tmp");

    s = Raster_SaveImage(canvas, tempPath.toUtf8().constData());
    Raster_DestroyImage(canvas);

    if (s != RASTER_OK) {
        QFile::remove(tempPath);
        return ExportResult::fail(QStringLiteral("Raster_SaveImage failed: %1").arg(static_cast<int>(s)));
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
