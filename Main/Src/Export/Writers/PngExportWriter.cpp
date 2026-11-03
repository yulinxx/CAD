/**
 * @file PngExportWriter.cpp
 * @brief PNG 导出写入器实现。
 *
 * 与 BmpExportWriter 共用 RasterExportHelper::rasterizeExport() 光栅化链路；
 * 差异只在于 FileFormat 常量、扩展名和 formatName。
 */

#include "PngExportWriter.h"
#include "RasterExportHelper.h"

PngExportWriter::PngExportWriter()
    : ExportWriterBase(Fio::FileFormat::PNG, { QStringLiteral("png") }, QStringLiteral("PNG"), QStringLiteral("png"))
{
}

ExportResult PngExportWriter::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    return RasterExportHelper::rasterizeExport(context, entities, "PNG");
}
