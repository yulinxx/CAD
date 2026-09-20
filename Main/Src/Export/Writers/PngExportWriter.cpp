/**
 * @file PngExportWriter.cpp
 * @brief PNG 导出写入器实现
 */
#include "PngExportWriter.h"

PngExportWriter::PngExportWriter()
    : ExportWriterBase(Fio::FileFormat::PNG, { QStringLiteral("png") }, QStringLiteral("PNG"), QStringLiteral("png"))
{
}