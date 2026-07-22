#pragma once

#include <QString>
#include <QStringList>

/// 导出结果：封装导出操作的结果状态和统计信息
struct ExportResult
{
    /// 导出是否成功
    bool success{ false };
    /// 主消息（成功或失败描述）
    QString message;
    /// 警告列表
    QStringList warnings;
    /// 导出的实体数量
    int exportedEntityCount{ 0 };
    /// 导出的节点数量
    int exportedNodeCount{ 0 };
    /// 导出的图层数量
    int exportedLayerCount{ 0 };

    /// 创建成功结果
    static ExportResult ok(const QString& msg = QString(),
        int entities = 0, int layers = 0,
        const QStringList& warns = {})
    {
        return { true, msg, warns, entities, 0, layers };
    }

    /// 创建失败结果
    static ExportResult fail(const QString& msg,
        const QStringList& warns = {})
    {
        return { false, msg, warns, 0, 0, 0 };
    }

    /// 追加警告
    void addWarning(const QString& warn)
    {
        warnings.append(warn);
    }
};
