#include "PltImportReader.h"

#include "Log/SyLogger.h"

PltImportReader::PltImportReader()
    : ImportReaderBase(Fio::FileFormat::PLT, { QStringLiteral("plt"), QStringLiteral("hpgl") }, QStringLiteral("PLT"))
{
}

ImportResult PltImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[PltImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // PltParser 仅实现 IR 路径（PLT 逻辑简单，无需旧路径回退）
    return readViaIR(context, Fio::FileFormat::PLT, outEntities, false);
}