/**
 * @file BmpExportWriter.cpp
 * @brief BMP 导出写入器实现
 */
#include "BmpExportWriter.h"

BmpExportWriter::BmpExportWriter()
    : ExportWriterBase(Fio::FileFormat::BMP, { QStringLiteral("bmp") }, QStringLiteral("BMP"), QStringLiteral("bmp"))
{
}