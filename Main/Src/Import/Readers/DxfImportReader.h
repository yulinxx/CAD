#pragma once

#include "ImportReaderBase.h"

/// DXF 格式导入读取器
class DxfImportReader : public ImportReaderBase
{
public:
    DxfImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
