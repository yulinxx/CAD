/**
 * @file SvgExportWriter.cpp
 * @brief SVG 导出写入器实现
 */
#include "SvgExportWriter.h"

SvgExportWriter::SvgExportWriter()
    : ExportWriterBase(Fio::FileFormat::SVG, { QStringLiteral("svg") }, QStringLiteral("SVG"), QStringLiteral("svg"))
{
}