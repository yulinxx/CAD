#include "ExportWriterBase.h"

#include <utility>
#include <vector>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

ExportWriterBase::ExportWriterBase(
    Fio::FileFormat format, QStringList extensions, QString formatName, QString defaultExtension)
    : m_format(format)
    , m_extensions(std::move(extensions))
    , m_formatName(std::move(formatName))
    , m_defaultExtension(std::move(defaultExtension))
{
}

Fio::FileFormat ExportWriterBase::format() const
{
    return m_format;
}

QStringList ExportWriterBase::supportedExtensions() const
{
    return m_extensions;
}

QString ExportWriterBase::formatName() const
{
    return m_formatName;
}

QString ExportWriterBase::defaultExtension() const
{
    return m_defaultExtension;
}

Fio::FileFormat ExportWriterBase::resolveFormat(const Fio::VecSyEntityPtr& /*entities*/) const
{
    return m_format;
}

QString ExportWriterBase::successMessage() const
{
    return QStringLiteral("%1 export successful").arg(m_formatName);
}

ExportResult ExportWriterBase::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;

    std::vector<const Eg::SyEntity*> raw;
    raw.reserve(entities.size());
    for (const auto& entity : entities)
    {
        raw.push_back(entity.get());
    }

    const Fio::FileFormat fmt = resolveFormat(entities);

    char errBuf[1024] = { 0 };
    bool ok = fileIO.exportFile(
        context.targetPath.toUtf8().toStdString().c_str(), fmt, raw.data(), raw.size(), errBuf, sizeof(errBuf));

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        SY_ERRORF("[%s] Failed: %s", m_formatName.toUtf8().constData(), msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    return ExportResult::ok(successMessage(), static_cast<int>(entities.size()));
}