/**
 * @file DxfExportWriter.cpp
 * @brief DXF 导出写入器实现
 */
#include "DxfExportWriter.h"

DxfExportWriter::DxfExportWriter()
    : ExportWriterBase(Fio::FileFormat::DXF, { QStringLiteral("dxf") }, QStringLiteral("DXF"), QStringLiteral("dxf"))
{
}