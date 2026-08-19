#pragma once

#include "ImportReaderBase.h"

/// STEP 格式导入读取器
class StepImportReader : public ImportReaderBase
{
public:
    StepImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
