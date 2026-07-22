#pragma once

#include <QString>
#include <QStringList>

/// 导入结果：封装导入操作的结果状态和统计信息
struct ImportResult
{
    /// 导入是否成功
    bool success{ false };
    /// 主消息（成功或失败描述）
    QString message;
    /// 警告列表（解析过程中的非致命问题）
    QStringList warnings;
    /// 导入的实体数量
    int entityCount{ 0 };
    /// 导入的节点数量（3D 模型树节点）
    int nodeCount{ 0 };
    /// 导入的图层数量
    int layerCount{ 0 };
    /// 导入后使用的工作台 ID
    QString usedWorkbenchId;

    /// 创建成功结果
    static ImportResult ok(const QString& msg = QString(),
        int entities = 0, int layers = 0,
        const QStringList& warns = {})
    {
        return { true, msg, warns, entities, 0, layers, {} };
    }

    /// 创建失败结果
    static ImportResult fail(const QString& msg,
        const QStringList& warns = {})
    {
        return { false, msg, warns, 0, 0, 0, {} };
    }

    /// 追加警告
    void addWarning(const QString& warn)
    {
        warnings.append(warn);
    }
};
