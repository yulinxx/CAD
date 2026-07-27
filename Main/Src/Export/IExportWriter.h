#pragma once

#include "ExportContext.h"
#include "ExportResult.h"
#include "FileIO/FileFormat.h"

/// 导出写入器接口：每种格式的导出适配器需要实现此接口
class IExportWriter
{
public:
    virtual ~IExportWriter() = default;

    /// 返回此写入器支持的格式
    virtual Fio::FileFormat format() const = 0;

    /// 返回此写入器支持的文件扩展名列表（小写，不含点号）
    virtual QStringList supportedExtensions() const = 0;

    /// 返回格式名称（用于日志和 UI 提示）
    virtual QString formatName() const = 0;

    /// 返回默认文件扩展名（用于保存对话框）
    virtual QString defaultExtension() const = 0;

    /// 执行导出操作
    /// @param context 导出上下文（含目标路径和参数）
    /// @param entities 要导出的图元列表
    /// @return 导出结果
    virtual ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) = 0;
};
