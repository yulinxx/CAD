/**
 * @file BmpExportWriter.cpp
 * @brief BMP 导出写入器实现。
 *
 * 覆写 write()：不走基类 ExportWriterBase → FileIOManager → FileWriterFactory 链路，
 * 因为 FileWriterFactory 没有注册 BMP 的 ILegacyWriter。
 * 自己走 RasterExportHelper 的光栅化链路。
 */

#include "BmpExportWriter.h"
#include "RasterExportHelper.h"

BmpExportWriter::BmpExportWriter()
    : ExportWriterBase(Fio::FileFormat::BMP, { QStringLiteral("bmp") }, QStringLiteral("BMP"), QStringLiteral("bmp"))
{
}

ExportResult BmpExportWriter::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    return RasterExportHelper::rasterizeExport(context, entities, "BMP");
}
