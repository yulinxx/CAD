#pragma once

#include "ImportReaderBase.h"

/// PDF 格式导入读取器
class PdfImportReader : public ImportReaderBase
{
public:
    PdfImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
