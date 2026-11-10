#pragma once

#include "ExportWriterBase.h"

/// BMP 格式导出写入器
///
/// 覆写 write()：不走基类 ExportWriterBase → FileIOManager → FileWriterFactory 链路，
/// 因为 FileWriterFactory 没有注册 BMP 的 ILegacyWriter。
/// 自己走 RasterExportHelper 的光栅化链路（Tessellator 离散化 → Raster 光栅化 → 原子写盘）。
class BmpExportWriter : public ExportWriterBase
{
public:
    BmpExportWriter();

    ExportResult write(const ExportContext& context, const Fio::VecAprEntityPtr& entities) override;
};
