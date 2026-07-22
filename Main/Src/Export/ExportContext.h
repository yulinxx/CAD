#pragma once

#include <QString>
#include <QStringList>

#include "FileIO/FileFormat.h"

/// 统一导出上下文：承载导出操作所需的所有参数和状态信息
struct ExportContext
{
    /// 目标文件完整路径
    QString targetPath;
    /// 导出格式
    Fio::FileFormat format{ Fio::FileFormat::Unknown };
    /// 源文档 ID（用于元数据记录）
    QString sourceDocumentId;
    /// 是否包含图层信息
    bool includeLayers{ true };
    /// 是否包含元数据
    bool includeMetadata{ true };
    /// 是否只导出选中内容
    bool includeSelectionOnly{ false };
    /// 导出 DPI（位图格式适用）
    int dpi{ 300 };
    /// 缩放因子
    double scaleFactor{ 1.0 };
    /// 页面尺寸（PDF 等格式适用）
    QString pageSize;
    /// 是否保留源颜色
    bool preserveColors{ true };
    /// 是否保留文本
    bool preserveText{ true };
    /// 附加元数据（扩展用）
    QStringList warnings;
};
