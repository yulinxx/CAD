#pragma once

#include "ImportContext.h"
#include "ImportResult.h"
#include "FileIO/FileFormat.h"
#include "FileIO/IFileParser.h"

/// 导入读取器支持的维度类型
enum class ImportDimension
{
    Both,   // 通用（图片等 2D/3D 都支持）
    Dim2D,  // 2D 格式
    Dim3D,  // 3D 格式
};

/// 导入读取器接口：每种格式的导入适配器需要实现此接口
class IImportReader
{
public:
    virtual ~IImportReader() = default;

    /// 返回此读取器支持的格式
    virtual Fio::FileFormat format() const = 0;

    /// 返回此读取器支持的文件扩展名列表（小写，不含点号）
    virtual QStringList supportedExtensions() const = 0;

    /// 返回此读取器支持的维度类型（2D/3D/通用）
    virtual ImportDimension dimension() const = 0;

    /// 返回格式名称（用于日志和 UI 提示）
    virtual QString formatName() const = 0;

    /// 执行导入操作
    /// @param context 导入上下文（含源路径和参数）
    /// @param outEntities 输出：导入的图元列表
    /// @return 导入结果
    virtual ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) = 0;
};
