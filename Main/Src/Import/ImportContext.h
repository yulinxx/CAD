#pragma once

#include <QString>
#include <QStringList>

#include "FileIO/FileFormat.h"

/// 统一导入上下文：承载导入操作所需的所有参数和状态信息
struct ImportContext
{
    /// 源文件完整路径
    QString sourcePath;
    /// 导入格式（由 ImportDispatcher 根据扩展名自动识别）
    Fio::FileFormat format{ Fio::FileFormat::Unknown };
    /// 目标工作台 ID（"2D" / "3D"），导入后自动切换
    QString targetWorkbenchId;
    /// 单位（"mm" / "cm" / "inch" 等），空字符串表示使用文件内置单位
    QString unit;
    /// PDF 页面索引（0 表示第一页，-1 表示所有页）
    int pageIndex{ 0 };
    /// 缩放因子，1.0 表示原始比例
    double scaleFactor{ 1.0 };
    /// 是否保留源文件的图层结构
    bool preserveLayers{ true };
    /// 是否保留源文件的颜色信息
    bool preserveColors{ true };
    /// 是否保留文本信息（SVG/PDF 标注类）
    bool preserveText{ true };
    /// 附加元数据（扩展用）
    QStringList warnings;
};
