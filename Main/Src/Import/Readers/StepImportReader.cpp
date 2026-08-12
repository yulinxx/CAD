#include "StepImportReader.h"

#include "FileIO/FileIOManager.h"
#include "Import/FioEntityConverter.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

ImportResult StepImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[StepImportReader] read START: path=%s",
        context.sourcePath.toUtf8().constData());

    Fio::FileIOManager fileIO;
    std::string pathStr = context.sourcePath.toUtf8().toStdString();

    // IR 主链路：parseToIR → FioEntityConverter
    // StepParser 仅实现 IR 路径（STEP 逻辑简单，无需旧路径回退）
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(
        pathStr.c_str(),
        Fio::FileFormat::STEP,
        &ir,
        errBuf, sizeof(errBuf));

    if (!ok || ir.entityCount == 0)
    {
        QString msg = QString::fromUtf8(errBuf);
        if (msg.isEmpty())
            msg = QStringLiteral("STEP import produced no entities");
        SY_ERRORF("[StepImportReader] Failed: %s", msg.toUtf8().constData());

        ImportErrorType errorType = ImportErrorType::ParseFailed;
        if (msg.contains(QStringLiteral("file not found"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::FileNotFound;
        }
        else if (msg.contains(QStringLiteral("unit"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::UnitIncompatible;
        }

        return ImportResult::fail(msg, errorType, QStringList{});
    }

    auto converted = FioEntityConverter::convertAll(ir);
    outEntities.clear();
    outEntities.reserve(converted.size());
    for (auto& e : converted)
        outEntities.emplace_back(std::move(e));

    SY_INFOF("[StepImportReader] read END: success, entities=%zu", outEntities.size());

    return ImportResult::ok(
        QStringLiteral("STEP import successful"),
        static_cast<int>(outEntities.size()),
        static_cast<int>(ir.layerCount),
        QStringList{});
}