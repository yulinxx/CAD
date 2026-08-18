#include "ExportDispatcher.h"

#include "Log/SyLogger.h"
#include "FileIO/FormatRegistry.h"

void ExportDispatcher::registerWriter(std::unique_ptr<IExportWriter> writer)
{
    if (!writer)
    {
        return;
    }

    Fio::FileFormat fmt = writer->format();
    m_formatMap[fmt] = writer.get();
    m_writers.push_back(std::move(writer));

    SY_INFOF("[ExportDispatcher] Registered writer for format=%d", static_cast<int>(fmt));
}

ExportResult ExportDispatcher::dispatch(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    // 如果未指定格式，自动检测
    Fio::FileFormat fmt = context.format;
    if (fmt == Fio::FileFormat::Unknown)
    {
        fmt = detectFormat(context.targetPath);
    }

    if (fmt == Fio::FileFormat::Unknown)
    {
        QString msg = QStringLiteral("Cannot detect format for: %1").arg(context.targetPath);
        SY_ERRORF("[ExportDispatcher] %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    IExportWriter* writer = findWriter(fmt);
    if (!writer)
    {
        QString msg = QStringLiteral("No writer registered for format=%1").arg(static_cast<int>(fmt));
        SY_ERRORF("[ExportDispatcher] %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    // 构造完整上下文（注入检测到的格式）
    ExportContext fullCtx = context;
    fullCtx.format = fmt;

    SY_INFOF("[ExportDispatcher] Dispatching export: format=%d, path=%s",
        static_cast<int>(fmt),
        context.targetPath.toUtf8().constData());

    return writer->write(fullCtx, entities);
}

Fio::FileFormat ExportDispatcher::detectFormat(const QString& filePath)
{
    return Fio::FormatRegistry::instance().detectFormat(filePath.toUtf8().constData());
}

QStringList ExportDispatcher::supportedExtensions() const
{
    QStringList exts;
    for (const auto& w : m_writers)
    {
        exts.append(w->supportedExtensions());
    }

    return exts;
}

bool ExportDispatcher::canExport(const QString& filePath) const
{
    Fio::FileFormat fmt = detectFormat(filePath);
    return findWriter(fmt) != nullptr;
}

QString ExportDispatcher::defaultExtension(Fio::FileFormat format) const
{
    IExportWriter* writer = findWriter(format);
    return writer ? writer->defaultExtension() : QString();
}

IExportWriter* ExportDispatcher::findWriter(Fio::FileFormat format) const
{
    auto it = m_formatMap.find(format);
    return (it != m_formatMap.end()) ? it->second : nullptr;
}