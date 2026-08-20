#include "SvgImportReader.h"

#include "Log/SyLogger.h"

SvgImportReader::SvgImportReader()
    : ImportReaderBase(Fio::FileFormat::SVG, { QStringLiteral("svg"), QStringLiteral("svgz") }, QStringLiteral("SVG"))
{
}

ImportResult SvgImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[SvgImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // SvgParser 仅实现 IR 路径（SVG path 采样为 Polyline），无旧路径回退
    return readViaIR(context, Fio::FileFormat::SVG, outEntities, true);
}