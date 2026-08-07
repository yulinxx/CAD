#include "DxfImportReader.h"

#include <QFileInfo>

#include "FileIO/FileIOManager.h"
#include "Import/FioEntityConverter.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

ImportResult DxfImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[DxfImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    Fio::FileIOManager fileIO;
    SY_INFO("[DxfImportReader] Creating FileIOManager");

    std::string pathStr = context.sourcePath.toUtf8().toStdString();
    SY_INFOF("[DxfImportReader] Calling fileIO.importFile: path=%s, format=%d",
        pathStr.c_str(), static_cast<int>(Fio::FileFormat::DXF));

    QStringList warns;
    Fio::FileIOManager::WarningCallback warningCb = [](const char* warning, void* ctx) {
        static_cast<QStringList*>(ctx)->append(QString::fromUtf8(warning));
        };

    // ===== 主链路：中立 IR 导入（parseToIR → FioEntityConverter） =====
    // FileIO 不再直接实例化 Engine 对象，IR 为跨 DLL 安全的 POD；
    // 若 IR 路径失败（如解析器未实现 IR 或文件不支持），自动回退旧路径。
    {
        Fio::FioParseResult ir;
        char irErrBuf[1024] = { 0 };
        bool irOk = fileIO.importToIR(
            pathStr.c_str(),
            Fio::FileFormat::DXF,
            &ir,
            irErrBuf, sizeof(irErrBuf));

        if (irOk && ir.entityCount > 0)
        {
            auto converted = FioEntityConverter::convertAll(ir);
            if (!converted.empty())
            {
                outEntities.clear();
                outEntities.reserve(converted.size());
                for (auto& e : converted)
                    outEntities.emplace_back(std::move(e));

                SY_INFOF("[DxfImportReader] IR path succeeded: entities=%zu, layers=%u",
                    outEntities.size(), ir.layerCount);

                return ImportResult::ok(
                    QStringLiteral("DXF import successful"),
                    static_cast<int>(outEntities.size()),
                    static_cast<int>(ir.layerCount),
                    warns);
            }
        }
        SY_WARNF("[DxfImportReader] IR path unavailable, falling back to legacy: %s",
            irErrBuf[0] ? irErrBuf : "no entities");
    }

    // ===== 回退路径：旧版直接实例化 Engine 对象 =====
    Eg::SyEntity** raw = nullptr;
    size_t count = 0;
    size_t layerCount = 0;
    char errBuf[1024] = { 0 };

    bool ok = fileIO.importFile(
        pathStr.c_str(),
        Fio::FileFormat::DXF,
        &raw, &count,
        errBuf, sizeof(errBuf),
        warningCb, &warns,
        &layerCount);

    SY_INFOF("[DxfImportReader] fileIO.importFile returned: success=%d, entities=%zu",
        ok ? 1 : 0, count);

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        SY_ERRORF("[DxfImportReader] Failed: %s", msg.toUtf8().constData());

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

    outEntities.clear();
    outEntities.reserve(count);
    for (size_t i = 0; i < count; ++i)
        outEntities.emplace_back(raw[i]);
    Fio::FileIOManager::freeEntityArray(raw);

    SY_INFOF("[DxfImportReader] read END: success, entities=%zu, layers=%zu",
        count, layerCount);

    return ImportResult::ok(
        QStringLiteral("DXF import successful"),
        static_cast<int>(count),
        static_cast<int>(layerCount),
        warns);
}