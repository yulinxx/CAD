#include "DxfExportWriter.h"

DxfExportWriter::DxfExportWriter()
    : ExportWriterBase(Fio::FileFormat::DXF, { QStringLiteral("dxf") }, QStringLiteral("DXF"), QStringLiteral("dxf"))
{
}
