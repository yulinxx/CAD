#include "PltImportReader.h"

#include <QFileInfo>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ImportResult PltImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[PltImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    Fio::FileIOManager fileIO;
    SY_INFO("[PltImportReader] Creating FileIOManager");

    std::string pathStr = context.sourcePath.toUtf8().toStdString();
    SY_INFOF("[PltImportReader] Calling fileIO.importFile: path=%s, format=%d",
        pathStr.c_str(), static_cast<int>(Fio::FileFormat::PLT));

    Fio::ParseResult parseResult = fileIO.importFile(
        pathStr,
        Fio::FileFormat::PLT,
        outEntities);

    SY_INFOF("[PltImportReader] fileIO.importFile returned: success=%d, entities=%zu",
        parseResult.success ? 1 : 0, outEntities.size());

    if (!parseResult.success)
    {
        QString msg = QString::fromStdString(parseResult.errorMessage);
        SY_ERRORF("[PltImportReader] Failed: %s", msg.toUtf8().constData());

        QStringList warns;
        for (const auto& w : parseResult.warnings)
            warns.append(QString::fromStdString(w));

        ImportErrorType errorType = ImportErrorType::ParseFailed;
        if (msg.contains(QStringLiteral("file not found"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::FileNotFound;
        }

        SY_INFO("[PltImportReader] read END: fail");
        return ImportResult::fail(msg, errorType, warns);
    }

    QStringList warns;
    for (const auto& w : parseResult.warnings)
        warns.append(QString::fromStdString(w));

    SY_INFOF("[PltImportReader] read END: success, entities=%zu",
        outEntities.size());

    return ImportResult::ok(
        QStringLiteral("PLT import successful"),
        static_cast<int>(outEntities.size()),
        0,
        warns);
}
