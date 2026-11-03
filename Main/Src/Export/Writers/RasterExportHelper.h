/**
 * @file RasterExportHelper.h
 * @brief BMP/PNG 位图导出共用 helper。
 *
 * 供 BmpExportWriter / PngExportWriter 在覆写 write() 时调用。
 * 包含：Tessellator 离散化、世界→像素变换、Raster 光栅化、进度回调、取消检查、原子落盘。
 */

#pragma once

#include "../ExportContext.h"
#include "../ExportResult.h"
#include "../IExportWriter.h"

class RasterExportHelper
{
public:
    /**
     * @brief 矢量实体 → 位图文件的完整链路。
     *
     * @param context     导出上下文（含 targetPath / scaleFactor / progressCallback / cancelFlag）
     * @param entities    要导出的图元列表（主线程已 clone 好的快照，后台线程只读）
     * @param formatName  格式名称（仅用于日志和成功消息）
     * @return 导出结果（success / message / exportedEntityCount）
     */
    static ExportResult rasterizeExport(const ExportContext& context,
                                        const Fio::VecSyEntityPtr& entities,
                                        const char* formatName);
};
