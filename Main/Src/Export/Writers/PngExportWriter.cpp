#include "PngExportWriter.h"

PngExportWriter::PngExportWriter()
    : ExportWriterBase(Fio::FileFormat::PNG, { QStringLiteral("png") }, QStringLiteral("PNG"), QStringLiteral("png"))
{
}
