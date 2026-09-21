#pragma once

#include <functional>
#include <QString>
#include <QStringList>

#include "FileIO/FileFormat.h"

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}

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

    /// 场景管理器指针（用于 Native 格式导出时借用实体，避免深拷贝）
    /// 为 nullptr 时导出服务会使用 clone 方式（向后兼容）
    Eg::SceneManager* sceneManager{ nullptr };
    /// 3D 场景管理器指针（用于 3D 网格图元借用）
    Eg::SceneManager3D* sceneManager3D{ nullptr };

    /// 进度回调（可选，导出过程中报告进度 0.0~1.0）
    std::function<void(float progress, const char* stage)> progressCallback;
};
