#pragma once

#include "ImportReaderBase.h"

/// SVG 格式导入读取器
class SvgImportReader : public ImportReaderBase
{
public:
    SvgImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
