/**
 * @file PdfExportWriter.cpp
 * @brief PDF格式导出写入器实现
 */
#include "PdfExportWriter.h"

PdfExportWriter::PdfExportWriter()
    : ExportWriterBase(Fio::FileFormat::PDF, { QStringLiteral("pdf") }, QStringLiteral("PDF"), QStringLiteral("pdf"))
{
}