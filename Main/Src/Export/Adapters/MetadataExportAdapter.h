#pragma once

#include <QString>

/// 元数据导出适配器：在导出操作前后记录文档元数据
class MetadataExportAdapter
{
public:
    MetadataExportAdapter() = default;

    /// 设置元数据应用回调
    /// @param callback 参数为目标路径、格式和图元数量
    void setApplyCallback(std::function<void(const QString&, const QString&, int)> callback);

    /// 应用导出元数据（记录导出信息到数据库或日志）
    /// @param targetPath 目标路径
    /// @param format 导出格式
    /// @param entityCount 导出的图元数量
    void apply(const QString& targetPath, const QString& format, int entityCount);

private:
    /// 元数据应用回调
    std::function<void(const QString&, const QString&, int)> m_applyCallback;
};
