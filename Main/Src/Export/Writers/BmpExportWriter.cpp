#include "BmpExportWriter.h"

BmpExportWriter::BmpExportWriter()
    : ExportWriterBase(Fio::FileFormat::BMP, { QStringLiteral("bmp") }, QStringLiteral("BMP"), QStringLiteral("bmp"))
{
}