#include "StepImportReader.h"

#include "Log/SyLogger.h"

StepImportReader::StepImportReader()
    : ImportReaderBase(Fio::FileFormat::STEP,
          { QStringLiteral("stp"), QStringLiteral("step") },
          QStringLiteral("STEP"))
{
}

ImportResult StepImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[StepImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // StepParser 仅实现 IR 路径（STEP 逻辑简单，无需旧路径回退）
    return readViaIR(context, Fio::FileFormat::STEP, outEntities, false);
}
