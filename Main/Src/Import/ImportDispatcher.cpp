/**
 * @file ImportDispatcher.cpp
 * @brief 导入调度器实现
 */
#include "ImportDispatcher.h"
#include "Log/SyLogger.h"
#include "FileIO/FormatRegistry.h"

#include <chrono>

#include <QCoreApplication>
#include <QFileInfo>

void ImportDispatcher::registerReader(std::unique_ptr<IImportReader> reader)
{
    if (!reader)
    {
        SY_WARN("[ImportDispatcher] registerReader ignored: null reader");
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
        QString msg = QCoreApplication::translate("ImportDispatcher", "File not found: %1").arg(context.sourcePath);
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
        QString msg =
            QCoreApplication::translate("ImportDispatcher", "Unsupported file format: %1").arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportDispatcher] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    IImportReader* reader = findReader(fmt);
    if (!reader)
    {
        QString msg = QCoreApplication::translate("ImportDispatcher", "No reader registered for format=%1")
                          .arg(static_cast<int>(fmt));
        SY_ERRORF("[ImportDispatcher] %s (suffix='%s', %zu reader(s) registered)",
            msg.toUtf8().constData(),
            fi.suffix().toUtf8().constData(),
            m_readers.size());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 构造完整上下文（注入检测到的格式）
    ImportContext fullCtx = context;
    fullCtx.format = fmt;

    const auto startTime = std::chrono::steady_clock::now();
    ImportResult result = reader->read(fullCtx, outEntities);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();

    // Reader result
    if (result.success)
    {
        // Import succeeded
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

QStringList ImportDispatcher::supportedExtensions(const QString& workbenchId) const
{
    QStringList exts;

    const bool is2DWorkbench = (workbenchId == QStringLiteral("2D"));
    const bool is3DWorkbench = (workbenchId == QStringLiteral("3D"));

    for (const auto& r : m_readers)
    {
        const ImportDimension dim = r->dimension();

        // 判断该读取器是否与当前工作台兼容
        bool compatible = false;
        if (dim == ImportDimension::Both)
        {
            // 通用格式（如图片、Native 同时支持 2D/3D）总是兼容
            compatible = true;
        }
        else if (is2DWorkbench && dim == ImportDimension::Dim2D)
        {
            compatible = true;
        }
        else if (is3DWorkbench && dim == ImportDimension::Dim3D)
        {
            compatible = true;
        }

        if (compatible)
        {
            exts.append(r->supportedExtensions());
        }
    }

    return exts;
}

bool ImportDispatcher::canImport(const QString& filePath) const
{
    Fio::FileFormat fmt = detectFormat(filePath);
    return findReader(fmt) != nullptr;
}

bool ImportDispatcher::canImport(const QString& filePath, const QString& workbenchId) const
{
    // 先检查是否可以被任何读取器导入
    if (!canImport(filePath))
    {
        return false;
    }

    // 根据工作台过滤
    const QStringList exts = supportedExtensions(workbenchId);
    const QString ext = QFileInfo(filePath).suffix().toLower();
    return exts.contains(ext);
}

IImportReader* ImportDispatcher::findReader(Fio::FileFormat format) const
{
    auto it = m_formatMap.find(format);
    return (it != m_formatMap.end()) ? it->second : nullptr;
}