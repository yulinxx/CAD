#include "ImportDispatcher.h"

#include <QFileInfo>

#include "Log/SyLogger.h"

void ImportDispatcher::registerReader(std::unique_ptr<IImportReader> reader)
{
    if (!reader)
        return;

    Fio::FileFormat fmt = reader->format();
    m_formatMap[fmt] = reader.get();
    m_readers.push_back(std::move(reader));

    // SY_INFOF("[ImportDispatcher] Registered reader for format=%d", static_cast<int>(fmt));
}

ImportResult ImportDispatcher::dispatch(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
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
        fmt = detectFormat(context.sourcePath);

    if (fmt == Fio::FileFormat::Unknown)
    {
        QString msg = QStringLiteral("Unsupported file format: %1").arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportDispatcher] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    IImportReader* reader = findReader(fmt);
    if (!reader)
    {
        QString msg = QStringLiteral("No reader registered for format=%1")
            .arg(static_cast<int>(fmt));
        SY_ERRORF("[ImportDispatcher] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 构造完整上下文（注入检测到的格式）
    ImportContext fullCtx = context;
    fullCtx.format = fmt;

    SY_INFOF("[ImportDispatcher] Dispatching import: format=%d, path=%s",
        static_cast<int>(fmt), context.sourcePath.toUtf8().constData());

    return reader->read(fullCtx, outEntities);
}

Fio::FileFormat ImportDispatcher::detectFormat(const QString& filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == QStringLiteral("dxf"))     return Fio::FileFormat::DXF;
    if (ext == QStringLiteral("svg"))     return Fio::FileFormat::SVG;
    if (ext == QStringLiteral("pdf"))     return Fio::FileFormat::PDF;
    if (ext == QStringLiteral("plt") ||
        ext == QStringLiteral("hpgl"))    return Fio::FileFormat::PLT;
    if (ext == QStringLiteral("stp") ||
        ext == QStringLiteral("step"))    return Fio::FileFormat::STEP;
    if (ext == QStringLiteral("ai"))      return Fio::FileFormat::AI;
    if (ext == QStringLiteral("prt") ||
        ext == QStringLiteral("igs") ||
        ext == QStringLiteral("iges"))    return Fio::FileFormat::UG;
    if (ext == QStringLiteral("sy"))      return Fio::FileFormat::Native;
    if (ext == QStringLiteral("syx"))     return Fio::FileFormat::Native3D;
    if (ext == QStringLiteral("obj"))     return Fio::FileFormat::Unknown; // OBJ 暂用 Unknown，由 ObjImportReader 特殊处理
    if (ext == QStringLiteral("bmp"))     return Fio::FileFormat::BMP;
    if (ext == QStringLiteral("png"))     return Fio::FileFormat::PNG;

    return Fio::FileFormat::Unknown;
}

QStringList ImportDispatcher::supportedExtensions() const
{
    QStringList exts;
    for (const auto& r : m_readers)
        exts.append(r->supportedExtensions());
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