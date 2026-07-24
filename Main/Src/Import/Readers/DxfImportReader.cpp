#include "DxfImportReader.h"

#include <QFileInfo>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ImportResult DxfImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[DxfImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    Fio::FileIOManager fileIO;
    SY_INFO("[DxfImportReader] Creating FileIOManager");

    std::string pathStr = context.sourcePath.toUtf8().toStdString();
    SY_INFOF("[DxfImportReader] Calling fileIO.importFile: path=%s, format=%d",
        pathStr.c_str(), static_cast<int>(Fio::FileFormat::DXF));

    Fio::ParseResult parseResult = fileIO.importFile(
        pathStr,
        Fio::FileFormat::DXF,
        outEntities);

    SY_INFOF("[DxfImportReader] fileIO.importFile returned: success=%d, entities=%zu",
        parseResult.success ? 1 : 0, outEntities.size());

    if (!parseResult.success)
    {
        QString msg = QString::fromStdString(parseResult.errorMessage);
        SY_ERRORF("[DxfImportReader] Failed: %s", msg.toUtf8().constData());

        QStringList warns;
        for (const auto& w : parseResult.warnings)
            warns.append(QString::fromStdString(w));

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
        else if (msg.contains(QStringLiteral("coordinate"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("axis"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::CoordinateSystemIncompatible;
        }

        SY_INFO("[DxfImportReader] read END: fail");
        return ImportResult::fail(msg, errorType, warns);
    }

    QStringList warns;
    for (const auto& w : parseResult.warnings)
        warns.append(QString::fromStdString(w));

    SY_INFOF("[DxfImportReader] read END: success, entities=%zu, layers=%zu",
        outEntities.size(), parseResult.dxfLayers.size());

    return ImportResult::ok(
        QStringLiteral("DXF import successful"),
        static_cast<int>(outEntities.size()),
        static_cast<int>(parseResult.dxfLayers.size()),
        warns);
}