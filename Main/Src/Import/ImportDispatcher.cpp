#include "ImportDispatcher.h"
#include "Log/SyLogger.h"
#include "FileIO/FormatRegistry.h"

#include <chrono>

#include <QFileInfo>

void ImportDispatcher::registerReader(std::unique_ptr<IImportReader> reader)
{
    if (!reader)
    {
        SY_ERROR("[ImportDispatcher] registerReader ignored: null reader");
        return;
    }

    Fio::FileFormat fmt = reader->format();
    const QString formatName = reader->formatName();

    // 同格式重复注册会静默覆盖先注册者，这里必须留痕：注册顺序决定最终生效的读取器
    auto existing = m_formatMap.find(fmt);
    if (existing != m_formatMap.end())
    {
        SY_WARNF("[ImportDispatcher] Reader for format=%d already registered ('%s'), overriding with '%s'",
            static_cast<int>(fmt),
            existing->second->formatName().toUtf8().constData(),
            formatName.toUtf8().constData());
    }

    m_formatMap[fmt] = reader.get();
    m_readers.push_back(std::move(reader));
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
        SY_INFOF("[ImportDispatcher] Format detected from path: suffix='%s' -> format=%d",
            fi.suffix().toUtf8().constData(),
            static_cast<int>(fmt));
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
        SY_ERRORF("[ImportDispatcher] %s (suffix='%s', %zu reader(s) registered)",
            msg.toUtf8().constData(),
            fi.suffix().toUtf8().constData(),
            m_readers.size());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 构造完整上下文（注入检测到的格式）
    ImportContext fullCtx = context;
    fullCtx.format = fmt;

    SY_INFOF("[ImportDispatcher] Dispatching to reader '%s': format=%d, size=%lld bytes, path=%s",
        reader->formatName().toUtf8().constData(),
        static_cast<int>(fmt),
        static_cast<long long>(fi.size()),
        context.sourcePath.toUtf8().constData());

    const auto startTime = std::chrono::steady_clock::now();
    ImportResult result = reader->read(fullCtx, outEntities);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();

    // 读取器耗时单独记一条：这是排查"导入很慢"时唯一能区分解析层与落地层的地方
    if (result.success)
    {
        SY_INFOF("[ImportDispatcher] Reader '%s' succeeded: %zu entity(ies), %zu layer(s), %zu group(s), %lld ms",
            reader->formatName().toUtf8().constData(),
            outEntities.size(),
            result.importedLayers.size(),
            result.importedGroups.size(),
            static_cast<long long>(elapsedMs));
    }
    else
    {
        SY_ERRORF("[ImportDispatcher] Reader '%s' failed after %lld ms: errorType=%d, message=%s",
            reader->formatName().toUtf8().constData(),
            static_cast<long long>(elapsedMs),
            static_cast<int>(result.errorType),
            result.message.toUtf8().constData());
    }

    return result;
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