#pragma once

#include "../IExportWriter.h"

/// 导出写入器基类：统一格式元数据与 FileIO 导出主链路，消除各 Writer 的重复样板代码
class ExportWriterBase : public IExportWriter
{
public:
    ExportWriterBase(Fio::FileFormat format, QStringList extensions, QString formatName, QString defaultExtension);
    ~ExportWriterBase() override = default;

    Fio::FileFormat format() const override;
    QStringList supportedExtensions() const override;
    QString formatName() const override;
    QString defaultExtension() const override;

    ExportResult write(const ExportContext& context, const Fio::VecSyEntityPtr& entities) override;

protected:
    /// 选择实际导出格式（默认返回构造时传入的格式；Native 按图元类型覆写）
    virtual Fio::FileFormat resolveFormat(const Fio::VecSyEntityPtr& entities) const;

    /// 成功消息（子类可覆写）
    virtual QString successMessage() const;

private:
    Fio::FileFormat m_format;
    QStringList m_extensions;
    QString m_formatName;
    QString m_defaultExtension;
};
