#pragma once

#include <QString>
#include <QStringList>

/// 导入错误类型：区分不同的失败原因，便于上层展示和处理
enum class ImportErrorType
{
    None,                    ///< 无错误（成功）
    FileNotFound,            ///< 文件不存在或路径无效
    FormatNotSupported,      ///< 文件格式不支持
    ParseFailed,             ///< 解析失败（文件损坏、格式错误）
    UnitIncompatible,        ///< 单位不兼容
    CoordinateSystemIncompatible, ///< 坐标系不兼容
    Canceled,                ///< 用户取消导入
    Unknown                  ///< 未知错误
};

/// 导入结果：封装导入操作的结果状态和统计信息
struct ImportResult
{
    /// 导入是否成功
    bool success{ false };
    /// 错误类型（失败时有效）
    ImportErrorType errorType{ ImportErrorType::None };
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
        ImportResult r;
        r.success = true;
        r.errorType = ImportErrorType::None;
        r.message = msg;
        r.warnings = warns;
        r.entityCount = entities;
        r.layerCount = layers;
        return r;
    }

    /// 创建失败结果（带错误类型）
    static ImportResult fail(const QString& msg,
        ImportErrorType errorType = ImportErrorType::Unknown,
        const QStringList& warns = {})
    {
        ImportResult r;
        r.success = false;
        r.errorType = errorType;
        r.message = msg;
        r.warnings = warns;
        return r;
    }

    /// 追加警告
    void addWarning(const QString& warn)
    {
        warnings.append(warn);
    }
};
