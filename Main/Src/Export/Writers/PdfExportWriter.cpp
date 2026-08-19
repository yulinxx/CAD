#include "PdfExportWriter.h"

PdfExportWriter::PdfExportWriter()
    : ExportWriterBase(Fio::FileFormat::PDF, { QStringLiteral("pdf") }, QStringLiteral("PDF"), QStringLiteral("pdf"))
{
}
