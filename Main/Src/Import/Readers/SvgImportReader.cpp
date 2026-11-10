#include "SvgImportReader.h"

#include "Log/AprLogger.h"

SvgImportReader::SvgImportReader()
    : ImportReaderBase(Fio::FileFormat::SVG,
          { QStringLiteral("svg"), QStringLiteral("svgz") },
          QStringLiteral("SVG"),
          ImportDimension::Dim2D)
{
}

ImportResult SvgImportReader::read(const ImportContext& context, Fio::VecAprEntityPtr& outEntities)
{
    // SvgParser 仅实现 IR 路径（SVG path 采样为 Polyline），无旧路径回退
    return readViaIR(context, Fio::FileFormat::SVG, outEntities, true);
}