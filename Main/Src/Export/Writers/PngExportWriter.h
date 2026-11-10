#pragma once

#include "ExportWriterBase.h"

/// PNG 格式导出写入器
///
/// 与 BmpExportWriter 共用 RasterExportHelper::rasterizeExport() 光栅化链路；
/// 差异只在于 FileFormat 常量、扩展名和 formatName。
class PngExportWriter : public ExportWriterBase
{
public:
    PngExportWriter();

    ExportResult write(const ExportContext& context, const Fio::VecAprEntityPtr& entities) override;
};
