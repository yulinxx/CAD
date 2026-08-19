#pragma once

#include "ImportReaderBase.h"

/// PLT (HPGL) 格式导入读取器
class PltImportReader : public ImportReaderBase
{
public:
    PltImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
