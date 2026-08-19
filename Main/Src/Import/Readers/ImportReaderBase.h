#pragma once

#include "../IImportReader.h"

/// 导入读取器基类：统一格式元数据与 IR 主链路，消除各 Reader 的重复样板代码
class ImportReaderBase : public IImportReader
{
public:
    ImportReaderBase(Fio::FileFormat format, QStringList extensions, QString formatName);
    ~ImportReaderBase() override = default;

    Fio::FileFormat format() const override;
    QStringList supportedExtensions() const override;
    QString formatName() const override;

protected:
    /// 尝试 IR 主链路：parseToIR → FioEntityConverter → 构建结果
    /// 成功返回 true 并填充 result；失败返回 false 并带回错误信息
    /// @param collectLayers 是否收集图层表与图元→图层映射
    bool tryImportViaIR(const ImportContext& context,
                        Fio::FileFormat format,
                        Fio::VecSyEntityPtr& outEntities,
                        bool collectLayers,
                        ImportResult* result,
                        QString* errMsg) const;

    /// 纯 IR 读取：成功返回 ok 结果；失败返回 fail 结果（含错误分类）
    ImportResult readViaIR(const ImportContext& context,
                           Fio::FileFormat format,
                           Fio::VecSyEntityPtr& outEntities,
                           bool collectLayers) const;

    /// 旧路径读取：importFile → 构建结果（含错误分类与警告收集）
    ImportResult readViaLegacy(const ImportContext& context,
                               Fio::FileFormat format,
                               Fio::VecSyEntityPtr& outEntities) const;

    /// 错误分类：根据错误信息判断 ImportErrorType
    static ImportErrorType classifyError(const QString& msg);

    /// 成功消息（子类可覆写）
    virtual QString successMessage(Fio::FileFormat format) const;

    /// 空结果消息（子类可覆写）
    virtual QString noEntitiesMessage() const;

    /// 错误提示增强（子类可覆写，如 UG 的 .prt 说明、AI 的外部工具提示）
    virtual void decorateError(QString& msg, const ImportContext& context) const;

private:
    Fio::FileFormat m_format;
    QStringList m_extensions;
    QString m_formatName;
};
