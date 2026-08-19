#pragma once

#include "ImportReaderBase.h"

/// STL 格式导入读取器
class StlImportReader : public ImportReaderBase
{
public:
    StlImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
