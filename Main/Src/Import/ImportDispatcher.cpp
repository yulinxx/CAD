#include "ImportDispatcher.h"
#include "Log/SyLogger.h"
#include "FileIO/FormatRegistry.h"

#include <QFileInfo>

void ImportDispatcher::registerReader(std::unique_ptr<IImportReader> reader)
{
    if (!reader)
    {
        return;
    }

    Fio::FileFormat fmt = reader->format();
    m_formatMap[fmt] = reader.get();
    m_readers.push_back(std::move(reader));

    // SY_INFOF("[ImportDispatcher] Registered reader for format=%d", static_cast<int>(fmt));
}

ImportResult ImportDispatcher::dispatch(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    // 文件存在性检查
    QFileInfo fi(context.sourcePath);
    if (!fi.exists())
    {
        QString msg = QStringLiteral("File not found: %1").arg(context.sourcePath);
        SY_ERRORF("[ImportDispatcher] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FileNotFound);
    }

    // 如果未指定格式，自动检测
    Fio::FileFormat fmt = context.format;
    if (fmt == Fio::FileFormat::Unknown)
    {
        fmt = detectFormat(context.sourcePath);
    }

    if (fmt == Fio::FileFormat::Unknown)
    {
        QString msg = QStringLiteral("Unsupported file format: %1").arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportDispatcher] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    IImportReader* reader = findReader(fmt);
    if (!reader)
    {
        QString msg = QStringLiteral("No reader registered for format=%1").arg(static_cast<int>(fmt));

        SY_ERRORF("[ImportDispatcher] %s", msg.toUtf8().constData());

        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 构造完整上下文（注入检测到的格式）
    ImportContext fullCtx = context;
    fullCtx.format = fmt;

    SY_INFOF("[ImportDispatcher] Dispatching import: format=%d, path=%s",
        static_cast<int>(fmt),
        context.sourcePath.toUtf8().constData());

    return reader->read(fullCtx, outEntities);
}

Fio::FileFormat ImportDispatcher::detectFormat(const QString& filePath)
{
    return Fio::FormatRegistry::instance().detectFormat(filePath.toUtf8().constData());
}

QStringList ImportDispatcher::supportedExtensions() const
{
    QStringList exts;
    for (const auto& r : m_readers)
    {
        exts.append(r->supportedExtensions());
    }
    return exts;
}

bool ImportDispatcher::canImport(const QString& filePath) const
{
    Fio::FileFormat fmt = detectFormat(filePath);
    return findReader(fmt) != nullptr;
}

IImportReader* ImportDispatcher::findReader(Fio::FileFormat format) const
{
    auto it = m_formatMap.find(format);
    return (it != m_formatMap.end()) ? it->second : nullptr;
}