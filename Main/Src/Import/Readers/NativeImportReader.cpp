#include "NativeImportReader.h"

#include <QFileInfo>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

ImportResult NativeImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[NativeImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    Fio::FileIOManager fileIO;

    std::string pathStr = context.sourcePath.toUtf8().toStdString();

    QStringList warns;
    Fio::FileIOManager::WarningCallback warningCb = [](const char* warning, void* ctx) {
        static_cast<QStringList*>(ctx)->append(QString::fromUtf8(warning));
    };

    // 检测是否为 3D 格式 (.syx)
    QFileInfo fi(context.sourcePath);
    bool is3D = (fi.suffix().toLower() == QStringLiteral("syx"));
    Fio::FileFormat format = is3D ? Fio::FileFormat::Native3D : Fio::FileFormat::Native;

    SY_INFOF("[NativeImportReader] Importing native format: %s (format=%d)",
        is3D ? "3D (.syx)" : "2D (.sy)",
        static_cast<int>(format));

    // 使用 FileIOManager 导入本地格式
    Eg::SyEntity** raw = nullptr;
    size_t count = 0;
    size_t layerCount = 0;
    char errBuf[1024] = { 0 };

    bool ok = fileIO.importFile(
        pathStr.c_str(), format, &raw, &count, errBuf, sizeof(errBuf),
        warningCb, &warns, &layerCount);

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        SY_ERRORF("[NativeImportReader] Failed: %s", msg.toUtf8().constData());

        ImportErrorType errorType = ImportErrorType::ParseFailed;
        if (msg.contains(QStringLiteral("file not found"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::FileNotFound;
        }
        else if (msg.contains(QStringLiteral("unit"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("scale"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::UnitIncompatible;
        }

        return ImportResult::fail(msg, errorType, warns);
    }

    outEntities.clear();
    outEntities.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        outEntities.emplace_back(raw[i]);
    }
    Fio::FileIOManager::freeEntityArray(raw);

    SY_INFOF("[NativeImportReader] read END: success=%d, entities=%zu, layers=%zu",
        ok ? 1 : 0, count, layerCount);

    QString formatDesc = is3D ? QStringLiteral("SanYi 3D Native") : QStringLiteral("SanYi 2D Native");
    return ImportResult::ok(
        formatDesc + QStringLiteral(" import successful"),
        static_cast<int>(count),
        static_cast<int>(layerCount),
        warns);
}
