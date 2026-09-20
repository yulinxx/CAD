/**
 * @file PdfImportReader.cpp
 * @brief PDF格式导入读取器实现
 */
#include "PdfImportReader.h"

#include "Log/SyLogger.h"

PdfImportReader::PdfImportReader()
    : ImportReaderBase(Fio::FileFormat::PDF, { QStringLiteral("pdf") }, QStringLiteral("PDF"), ImportDimension::Dim2D)
{
}

ImportResult PdfImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    // PdfBasedParser 仅实现 IR 路径（PDF→SVG→SvgParser::parseToIR），无旧路径回退
    return readViaIR(context, Fio::FileFormat::PDF, outEntities, false);
}