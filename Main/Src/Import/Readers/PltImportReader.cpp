/**
 * @file PltImportReader.cpp
 * @brief PLT格式导入读取器实现
 */
#include "PltImportReader.h"

#include "Log/SyLogger.h"

PltImportReader::PltImportReader()
    : ImportReaderBase(Fio::FileFormat::PLT,
          { QStringLiteral("plt"), QStringLiteral("hpgl") },
          QStringLiteral("PLT"),
          ImportDimension::Dim2D)
{
}

ImportResult PltImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    // PltParser 仅实现 IR 路径（PLT 逻辑简单，无需旧路径回退）
    return readViaIR(context, Fio::FileFormat::PLT, outEntities, false);
}