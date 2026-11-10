/**
 * @file StepImportReader.cpp
 * @brief STEP格式导入读取器实现
 */
#include "StepImportReader.h"

#include "Log/AprLogger.h"

StepImportReader::StepImportReader()
    : ImportReaderBase(Fio::FileFormat::STEP,
          { QStringLiteral("stp"), QStringLiteral("step") },
          QStringLiteral("STEP"),
          ImportDimension::Dim3D)
{
}

ImportResult StepImportReader::read(const ImportContext& context, Fio::VecAprEntityPtr& outEntities)
{
    // StepParser 仅实现 IR 路径（STEP 逻辑简单，无需旧路径回退）
    return readViaIR(context, Fio::FileFormat::STEP, outEntities, false);
}