#include "SvgExportWriter.h"

SvgExportWriter::SvgExportWriter()
    : ExportWriterBase(Fio::FileFormat::SVG, { QStringLiteral("svg") }, QStringLiteral("SVG"), QStringLiteral("svg"))
{
}